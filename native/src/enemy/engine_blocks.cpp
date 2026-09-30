// Bank $A0 common enemy engine, part 4: enemy block collision, movement, Samus vs solid enemies.
#include "thor/enemy_engine.hpp"
#include "reference_labels.hpp"

namespace thor::enemy {
namespace {
uint16_t level_word(Game& g, uint32_t block_index) { return g.word_at(Game::LevelData_address + block_index * 2); }
uint8_t bts_at(Game& g, uint32_t block_index) { return g.byte_at(Game::BTS_address + block_index); }

// $12/$14 and DP temps of the movement routines, kept together.
struct MoveContext {
    uint16_t lo = 0, hi = 0;         // $12 sub-pixel, $14 pixel distance (rescaled by slopes)
    uint16_t boundary = 0;           // $1A target boundary
    uint16_t rows_left = 0;          // $1C
    uint16_t span = 0;               // $1E
    uint16_t flags = 0;              // $20: 8000 process slopes, 4000 slopes are walls
    uint16_t target = 0;             // $18 target position (vertical)
    uint16_t block = 0;              // CurrentBlockIndex
    uint16_t enemy = 0;              // EnemyIndex
};

bool enemy_spike(Game& g, MoveContext& c) {
    if ((bts_at(g, c.block) & 0x7F) != 15) return true;
    if (!g.spawn_plm) return true;
    const uint16_t width = g.RoomWidthBlocks();
    g.spawn_plm(g, uint16_t(labels::PLMEntries_EnemyBreakableBlock & 0xFFFF), uint16_t(c.block % width), uint16_t(c.block / width));
    return false;
}

bool square_slope(Game& g, MoveContext& c, bool horizontal) {
    const uint8_t bts = bts_at(g, c.block);
    const uint16_t base = uint16_t((bts & 0x1F) << 2), flips = bts >> 6;
    const uint16_t index = uint16_t(base + (((c.boundary & 8) >> 3) ^ flips));
    auto solid = [&](uint16_t i) { return (g.rom_b(labels::SquareSlopeDefinitions_BankA0 + i) & 0x80) != 0; };
    const uint16_t e = c.enemy;
    const uint16_t center = horizontal ? g.Enemy_YPosition(e) : g.Enemy_XPosition(e);
    const uint16_t radius = horizontal ? g.Enemy_YHitboxRadius(e) : g.Enemy_XHitboxRadius(e);
    const uint16_t other = horizontal ? 2 : 1;
    bool both = true;
    if (c.rows_left == 0) {
        if (!(uint16_t(center + radius - 1) & 8)) return solid(index);
    } else if (c.rows_left == c.span) {
        if ((uint16_t(center - radius) & 8)) both = false;   // only the far half matters
    }
    if (both && solid(index)) return true;
    return solid(uint16_t(index ^ other));
}

bool horizontal_slope(Game& g, MoveContext& c) {
    const uint8_t bts = bts_at(g, c.block);
    if ((bts & 0x1F) < 5) return square_slope(g, c, true);
    if (c.flags & 0x8000) {
        // Rescale the distance by the slope's adjusted distance multiplier (8.8 fixed point).
        const uint16_t mult = g.rom_w(labels::EnemyBlockCollisionReaction_Horizontal_Slope_NonSquare_adjustedDistanceMult + (bts & 0x1F) * 4);
        const bool negative = n16(c.hi);
        const uint16_t middle = uint16_t(((c.lo >> 8) | (c.hi << 8)) & 0xFFFF);   // $13.$14 as a word
        const uint32_t product = Multiplication_32bit(negative ? uint16_t(0 - middle) : middle, mult);
        const uint32_t result = negative ? uint32_t(0 - product) : product;
        c.lo = uint16_t(result); c.hi = uint16_t(result >> 16);
        return false;
    }
    return (c.flags & 0x4000) != 0;
}

bool vertical_slope(Game& g, MoveContext& c) {
    const uint8_t bts = bts_at(g, c.block);
    if ((bts & 0x1F) < 5) return square_slope(g, c, false);
    const uint16_t e = c.enemy;
    const uint16_t width = g.RoomWidthBlocks();
    if ((g.Enemy_XPosition(e) >> 4) != uint16_t(c.block % width)) return false;
    const bool down = !n16(c.hi);
    const uint16_t offset = down ? uint16_t((c.target + g.Enemy_YHitboxRadius(e) - 1) & 0xF)
                                 : uint16_t(((c.target - g.Enemy_YHitboxRadius(e)) & 0xF) ^ 0xF);
    const uint16_t base = uint16_t((bts & 0x1F) << 4);
    if (down ? (bts & 0x80) : !(bts & 0x80)) return false;   // ceilings don't stop a falling enemy (and vice versa)
    uint16_t x = g.Enemy_XPosition(e);
    if (bts & 0x40) x ^= 0xF;
    const uint16_t value = g.rom_b(labels::SlopeDefinitions_SlopeTopXOffsetByYPixel + base + (x & 0xF)) & 0x1F;
    const int16_t d = int16_t(value - offset - 1);
    if (down) {
        if (d > 0) return false;
        g.Enemy_YPosition(e) = uint16_t(d + c.target); g.Enemy_YSubPosition(e) = 0xFFFF;
    } else {
        if (d > 0) return false;
        g.Enemy_YPosition(e) = uint16_t(-d + c.target); g.Enemy_YSubPosition(e) = 0;
    }
    return true;
}

// $A0:C845 / $A0:C879 dispatch, including the extension blocks that redirect to another block.
bool block_reaction(Game& g, MoveContext& c, uint16_t block, bool horizontal) {
    for (unsigned guard = 0; guard < 16; ++guard) {
        switch (block >> 12) {
        case 0: case 2: case 3: case 4: case 6: case 7: return false;
        case 1: return horizontal ? horizontal_slope(g, c) : vertical_slope(g, c);
        case 5: case 0xD: {
            const uint8_t bts = bts_at(g, c.block);
            if (!bts) return false;
            if ((block >> 12) == 5) c.block = uint16_t(c.block + int8_t(bts));
            else c.block = uint16_t(c.block + int8_t(bts) * int(g.RoomWidthBlocks()));
            block = level_word(g, c.block);
            continue;
        }
        case 0xA: return enemy_spike(g, c);
        default: return true;   // 8 solid, 9 door, B special, C shootable, E grapple, F bombable
        }
    }
    return true;
}

uint32_t row_start(Game& g, uint16_t row, uint16_t column) {
    return uint32_t(((row & 0xFF) * (g.RoomWidthBlocks() & 0xFF)) + column);
}
}

bool CheckForHorizontalSolidBlockCollision(Game& g, uint16_t x, uint16_t lo, uint16_t& hi) {
    const uint16_t y = g.Enemy_YPosition(x), yr = g.Enemy_YHitboxRadius(x), xr = g.Enemy_XHitboxRadius(x);
    const uint16_t top = uint16_t((y - yr) & 0xFFF0);
    int16_t rows = int16_t((y + yr - 1 - top) >> 4);
    const uint32_t sum = (uint32_t(g.Enemy_XPosition(x)) << 16 | g.Enemy_XSubPosition(x)) + ((uint32_t(hi) << 16) | lo);
    const uint16_t target_x = uint16_t(sum >> 16);
    const uint16_t front = n16(hi) ? uint16_t(target_x - xr) : uint16_t(target_x + xr - 1);
    uint32_t index = row_start(g, uint16_t((y - yr) >> 4), uint16_t(front >> 4));
    for (; rows >= 0; --rows, index += g.RoomWidthBlocks()) {
        if (!(level_word(g, index) & 0x8000)) continue;
        if (!n16(hi)) {
            int16_t d = int16_t((front & 0xFFF0) - xr - g.Enemy_XPosition(x));
            hi = d >= 0 ? uint16_t(d) : 0;
        } else {
            int16_t d = int16_t(((front | 0xF) + 1 + xr) - g.Enemy_XPosition(x));
            hi = uint16_t(0 - (d < 0 ? d : 0));
        }
        return true;
    }
    return false;
}
bool CheckForVerticalSolidBlockCollision(Game& g, uint16_t x, uint16_t lo, uint16_t& hi) {
    const uint16_t xp = g.Enemy_XPosition(x), xr = g.Enemy_XHitboxRadius(x), yr = g.Enemy_YHitboxRadius(x);
    const uint16_t left = uint16_t((xp - xr) & 0xFFF0);
    int16_t columns = int16_t((xp + xr - 1 - left) >> 4);
    const uint32_t sum = (uint32_t(g.Enemy_YPosition(x)) << 16 | g.Enemy_YSubPosition(x)) + ((uint32_t(hi) << 16) | lo);
    const uint16_t target_y = uint16_t(sum >> 16);
    const uint16_t front = n16(hi) ? uint16_t(target_y - yr) : uint16_t(target_y + yr - 1);
    uint32_t index = row_start(g, uint16_t(front >> 4), uint16_t((xp - xr) >> 4));
    for (; columns >= 0; --columns, ++index) {
        if (!(level_word(g, index) & 0x8000)) continue;
        if (!n16(hi)) {
            int16_t d = int16_t((front & 0xFFF0) - yr - g.Enemy_YPosition(x));
            hi = d >= 0 ? uint16_t(d) : 0;
        } else {
            int16_t d = int16_t(((front | 0xF) + 1 + yr) - g.Enemy_YPosition(x));
            hi = uint16_t(0 - (d < 0 ? d : 0));
        }
        return true;
    }
    return false;
}

// ---- $A0:BF8A Vertical solid block probe used by skree/metaree (unsigned distance) ----------------------------
bool CheckForVerticalSolidBlockCollision_SkreeMetaree(Game& g, uint16_t x, bool down, uint16_t lo, uint16_t& hi) {
    const uint16_t xp = g.Enemy_XPosition(x), xr = g.Enemy_XHitboxRadius(x), yr = g.Enemy_YHitboxRadius(x);
    int16_t columns = int16_t((xp + xr - 1 - ((xp - xr) & 0xFFF0)) >> 4);
    uint16_t y_target;
    if (down) {
        const uint32_t sum = (uint32_t(g.Enemy_YPosition(x)) << 16 | g.Enemy_YSubPosition(x)) + ((uint32_t(hi) << 16) | lo);
        y_target = uint16_t((sum >> 16) + yr - 1);
    } else {
        const uint32_t diff = (uint32_t(g.Enemy_YPosition(x)) << 16 | g.Enemy_YSubPosition(x)) - ((uint32_t(hi) << 16) | lo);
        y_target = uint16_t((diff >> 16) - yr);
    }
    uint32_t index = row_start(g, uint16_t(y_target >> 4), uint16_t((xp - xr) >> 4));
    for (; columns >= 0; --columns, ++index) {
        if (!(level_word(g, index) & 0x8000)) continue;
        if (down) {
            const uint16_t r1 = uint16_t((y_target & 0xFFF0) - yr);
            const int32_t r2 = int32_t(r1) - int32_t(g.Enemy_YPosition(x)) - ((y_target & 0xFFF0) >= yr ? 0 : 1);
            hi = int16_t(r2) >= 0 ? uint16_t(r2) : 0;
        } else {
            const int16_t d = int16_t(((y_target | 0xF) + 1 + yr) - g.Enemy_YPosition(x));
            hi = uint16_t(0 - (d < 0 ? d : 0));
        }
        return true;
    }
    return false;
}

// ---- $A0:C6AD Move enemy right (horizontal, either sign) ---------------------------------------------------
static bool MoveEnemyRight(Game& g, uint16_t x, uint16_t lo, uint16_t hi, uint16_t flags) {
    if (!lo && !hi) return false;
    MoveContext c; c.lo = lo; c.hi = hi; c.flags = flags; c.enemy = x;
    const uint16_t y = g.Enemy_YPosition(x), yr = g.Enemy_YHitboxRadius(x), xr = g.Enemy_XHitboxRadius(x);
    const uint16_t top = uint16_t((y - yr) & 0xFFF0);
    c.rows_left = c.span = uint16_t((y + yr - 1 - top) >> 4);
    const uint32_t sum = (uint32_t(g.Enemy_XPosition(x)) << 16 | g.Enemy_XSubPosition(x)) + ((uint32_t(hi) << 16) | lo);
    const uint16_t target = uint16_t(sum >> 16);
    c.boundary = n16(hi) ? uint16_t(target - xr) : uint16_t(target + xr - 1);
    uint32_t index = row_start(g, uint16_t((y - yr) >> 4), uint16_t(c.boundary >> 4));
    for (int guard = 0; guard < 64; ++guard) {
        c.block = uint16_t(index);
        if (block_reaction(g, c, level_word(g, index), true)) {
            if (!n16(c.hi)) {
                const uint16_t edge = uint16_t((c.boundary & 0xFFF0) - xr);
                if (edge >= g.Enemy_XPosition(x)) g.Enemy_XPosition(x) = edge;
                g.Enemy_XSubPosition(x) = 0xFFFF;
            } else {
                const uint16_t edge = uint16_t((c.boundary | 0xF) + 1 + xr);
                if (edge <= g.Enemy_XPosition(x)) g.Enemy_XPosition(x) = edge;
                g.Enemy_XSubPosition(x) = 0;
            }
            return true;
        }
        index += g.RoomWidthBlocks();
        if (c.rows_left == 0) break;
        --c.rows_left;
    }
    const uint32_t moved = (uint32_t(g.Enemy_XPosition(x)) << 16 | g.Enemy_XSubPosition(x)) + ((uint32_t(c.hi) << 16) | c.lo);
    g.Enemy_XPosition(x) = uint16_t(moved >> 16); g.Enemy_XSubPosition(x) = uint16_t(moved);
    return false;
}
bool MoveEnemyRightBy_14_12_TreatSlopesAsWalls(Game& g, uint16_t x, uint16_t lo, uint16_t hi) { return MoveEnemyRight(g, x, lo, hi, 0x4000); }
bool MoveEnemyRightBy_14_12_ProcessSlopes(Game& g, uint16_t x, uint16_t lo, uint16_t hi) { return MoveEnemyRight(g, x, lo, hi, 0x8000); }
bool MoveEnemyRightBy_14_12_IgnoreSlopes(Game& g, uint16_t x, uint16_t lo, uint16_t hi) { return MoveEnemyRight(g, x, lo, hi, 0); }

// ---- $A0:C786 Move enemy down ---------------------------------------------------------------------------------
bool MoveEnemyDownBy_14_12(Game& g, uint16_t x, uint16_t lo, uint16_t hi) {
    if (!lo && !hi) return false;
    MoveContext c; c.lo = lo; c.hi = hi; c.enemy = x;
    const uint16_t xp = g.Enemy_XPosition(x), xr = g.Enemy_XHitboxRadius(x), yr = g.Enemy_YHitboxRadius(x);
    const uint16_t left = uint16_t((xp - xr) & 0xFFF0);
    c.rows_left = c.span = uint16_t((xp + xr - 1 - left) >> 4);   // block columns to test
    const uint32_t sum = (uint32_t(g.Enemy_YPosition(x)) << 16 | g.Enemy_YSubPosition(x)) + ((uint32_t(hi) << 16) | lo);
    c.target = uint16_t(sum >> 16);
    c.boundary = n16(hi) ? uint16_t(c.target - yr) : uint16_t(c.target + yr - 1);
    uint32_t index = row_start(g, uint16_t(c.boundary >> 4), uint16_t((xp - xr) >> 4));
    for (int guard = 0; guard < 64; ++guard) {
        c.block = uint16_t(index);
        if (block_reaction(g, c, level_word(g, index), false)) {
            if (!n16(hi)) {
                const uint16_t edge = uint16_t((c.boundary & 0xFFF0) - yr);
                if (edge >= g.Enemy_YPosition(x)) g.Enemy_YPosition(x) = edge;
                g.Enemy_YSubPosition(x) = 0xFFFF;
            } else {
                const uint16_t edge = uint16_t((c.boundary | 0xF) + 1 + yr);
                if (edge <= g.Enemy_YPosition(x)) g.Enemy_YPosition(x) = edge;
                g.Enemy_YSubPosition(x) = 0;
            }
            return true;
        }
        ++index;
        if (c.rows_left == 0) break;
        --c.rows_left;
    }
    g.Enemy_YSubPosition(x) = uint16_t(sum); g.Enemy_YPosition(x) = uint16_t(sum >> 16);
    return false;
}

// ---- $A0:C8AD Align enemy Y position with a non-square slope -----------------------------------------------------
bool AlignEnemyYPositionWithNonSquareSlope(Game& g, uint16_t x) {
    bool adjusted = false;
    auto slope_at = [&](uint16_t px, uint16_t py, uint16_t& block_index, uint8_t& bts) {
        block_index = uint16_t(((py >> 4) & 0xFF) * (g.RoomWidthBlocks() & 0xFF) + (px >> 4));
        bts = bts_at(g, block_index);
        return (level_word(g, block_index) & 0xF000) == 0x1000 && (bts & 0x1F) >= 5;
    };
    uint16_t block; uint8_t bts;
    const uint16_t yr = g.Enemy_YHitboxRadius(x);
    if (slope_at(g.Enemy_XPosition(x), uint16_t(g.Enemy_YPosition(x) + yr - 1), block, bts)) {
        adjusted = true;
        const uint16_t offset = uint16_t((g.Enemy_YPosition(x) + yr - 1) & 0xF);
        if (!(bts & 0x80)) {
            uint16_t px = g.Enemy_XPosition(x); if (bts & 0x40) px ^= 0xF;
            const uint16_t value = g.rom_b(labels::SlopeDefinitions_SlopeTopXOffsetByYPixel + uint32_t(bts & 0x1F) * 16 + (px & 0xF)) & 0x1F;
            const int16_t d = int16_t(value - offset - 1);
            if (d <= 0) g.Enemy_YPosition(x) = uint16_t(d + g.Enemy_YPosition(x));
        }
    }
    if (slope_at(g.Enemy_XPosition(x), uint16_t(g.Enemy_YPosition(x) - yr), block, bts)) {
        adjusted = true;
        const uint16_t offset = uint16_t(((g.Enemy_YPosition(x) - yr) & 0xF) ^ 0xF);
        if (bts & 0x80) {
            uint16_t px = g.Enemy_XPosition(x); if (bts & 0x40) px ^= 0xF;
            const uint16_t value = g.rom_b(labels::SlopeDefinitions_SlopeTopXOffsetByYPixel + uint32_t(bts & 0x1F) * 16 + (px & 0xF)) & 0x1F;
            const int16_t d = int16_t(value - offset - 1);
            if (d <= 0) g.Enemy_YPosition(x) = uint16_t(-d + g.Enemy_YPosition(x));
        }
    }
    return adjusted;
}

// ---- $A0:A8F0 Samus vs solid enemy collision detection ---------------------------------------------------------
bool Samus_vs_SolidEnemy_CollisionDetection(Game& g, uint16_t& pixels, uint16_t& sub, uint16_t& enemy_out) {
    if (!g.InteractiveEnemyIndicesStackPointer()) return false;
    const unsigned direction = g.CollisionMovementDirection() & 3;
    uint16_t tx, ty, txs, tys;
    auto dec_target = [](uint16_t v, bool borrow, bool zero_from_sub) {   // reproduces the DEC/BEQ/DEC sequence
        if (borrow) { --v; zero_from_sub = (v == 0); }
        if (!zero_from_sub) --v;
        return v;
    };
    auto inc_target = [](uint16_t v, bool carry, bool zero_from_sum) {
        if (carry) { ++v; zero_from_sum = (v == 0); }
        if (!zero_from_sum) ++v;
        return v;
    };
    switch (direction) {
    case 0: {   // left
        tx = uint16_t(g.SamusXPosition() - pixels);
        const bool borrow = g.SamusXSubPosition() < sub; const uint16_t diff = uint16_t(g.SamusXSubPosition() - sub);
        tx = dec_target(tx, borrow, diff == 0); ty = g.SamusYPosition(); tys = g.SamusYSubPosition(); txs = g.SamusXSubPosition(); break; }
    case 1: {   // right
        tx = uint16_t(pixels + g.SamusXPosition());
        const uint32_t s = uint32_t(sub) + g.SamusXSubPosition();
        tx = inc_target(tx, s > 0xFFFF, uint16_t(s) == 0); ty = g.SamusYPosition(); tys = g.SamusYSubPosition(); txs = g.SamusXSubPosition(); break; }
    case 2: {   // up
        ty = uint16_t(g.SamusYPosition() - pixels);
        const bool borrow = g.SamusYSubPosition() < sub; const uint16_t diff = uint16_t(g.SamusYSubPosition() - sub);
        ty = dec_target(ty, borrow, diff == 0); tx = g.SamusXPosition(); txs = g.SamusXSubPosition(); tys = g.SamusYSubPosition(); break; }
    default: {  // down
        ty = uint16_t(pixels + g.SamusYPosition());
        const uint32_t s = uint32_t(sub) + g.SamusYSubPosition();
        ty = inc_target(ty, s > 0xFFFF, uint16_t(s) == 0); tx = g.SamusXPosition(); txs = g.SamusXSubPosition(); tys = g.SamusYSubPosition(); break; }
    }
    g.SamusTargetXPosition() = tx; g.SamusTargetYPosition() = ty; g.SamusTargetXSubPosition() = txs; g.SamusTargetYSubPosition() = tys;
    g.SamusXRadiusMirror() = g.SamusXRadius(); g.SamusYRadiusMirror() = g.SamusYRadius();
    for (g.InteractiveEnemyIndicesIndex() = 0;; g.InteractiveEnemyIndicesIndex() += 2) {
        const uint16_t e = g.word_at(Game::InteractiveEnemyIndices_address + g.InteractiveEnemyIndicesIndex());
        if (e == 0xFFFF) return false;
        g.CollisionIndex() = e;
        if (!g.Enemy_freezeTimer(e) && !(g.Enemy_properties(e) & 0x8000)) continue;
        if (!within(abs16(uint16_t(g.Enemy_XPosition(e) - tx)), g.Enemy_XHitboxRadius(e), g.SamusXRadiusMirror())) continue;
        if (!within(abs16(uint16_t(g.Enemy_YPosition(e) - ty)), g.Enemy_YHitboxRadius(e), g.SamusYRadiusMirror())) continue;
        int16_t d;
        switch (direction) {
        case 0: d = int16_t(g.SamusXPosition() - g.SamusXRadius() - (g.Enemy_XPosition(e) + g.Enemy_XHitboxRadius(e))); break;
        case 1: d = int16_t(g.Enemy_XPosition(e) - g.Enemy_XHitboxRadius(e) - (g.SamusXPosition() + g.SamusXRadius())); break;
        case 2: d = int16_t(g.SamusYPosition() - g.SamusYRadius() - (g.Enemy_YPosition(e) + g.Enemy_YHitboxRadius(e))); break;
        default: d = int16_t(g.Enemy_YPosition(e) - g.Enemy_YHitboxRadius(e) - (g.SamusYPosition() + g.SamusYRadius())); break;
        }
        if (d < 0) continue;   // Samus is already overlapping this enemy
        if (d == 0) g.SamusYSubPosition() = 0;   // (sic) the original zeroes the Y sub-position even for horizontal hits
        pixels = d == 0 ? 0 : uint16_t(d); sub = 0; enemy_out = e;
        g.word_at(Game::EnemyIndexSamusCollidesLeft_address + direction * 2) = e;
        return true;
    }
}
}
