// Bank $A0 common enemy engine, part 2: main routine, collision handling, damage, death.
#include "thor/enemy_engine.hpp"
#include "reference_labels.hpp"

namespace thor::enemy {
namespace {
constexpr uint16_t kNone = 0xFFFF;
constexpr uint16_t kRespawn = uint16_t(labels::EnemyHeaders_Respawn & 0xFFFF);
constexpr uint16_t kNothingSpritemap = uint16_t(labels::Spritemap_Common_Nothing & 0xFFFF);
constexpr uint16_t kNothingExtended = uint16_t(labels::ExtendedSpritemap_Common_Nothing & 0xFFFF);
constexpr uint16_t kRtl = uint16_t(labels::RTL_A0804C & 0xFFFF), kRts = uint16_t(labels::RTS_A0804B & 0xFFFF);

uint8_t bank_of(Game& g, uint16_t x) { return uint8_t(g.Enemy_bank(x)); }
void run_ai(Game& g, uint16_t x, uint16_t pointer) {
    g.word_at(Game::EnemyAIPointer_address) = pointer;
    g.byte_at(Game::EnemyAIPointer_address + 2) = bank_of(g, x);
    g.call_function((uint32_t(bank_of(g, x)) << 16) | pointer, x);
}
uint16_t hdr_w(Game& g, uint16_t id, uint32_t field) { return g.rom_w(header_address(id) + field); }
bool ignorable_ai(uint16_t pointer) { return pointer == kRtl || pointer == kRts; }

// EnemyVulnerabilities table base (bank $B4) for an enemy ID.
uint32_t vulnerabilities(Game& g, uint16_t id) {
    uint16_t pointer = hdr_w(g, id, header::vulnerabilities);
    if (!pointer) pointer = uint16_t(labels::EnemyVulnerabilities_Default & 0xFFFF);
    return 0xB40000u | pointer;
}

// Projectile array accessors: `y` is the projectile byte offset (index * 2).
uint16_t proj_type(Game& g, uint16_t y) { return g.SamusProjectile_Types(y); }
uint32_t bomb_timer_address(uint16_t y) { return Game::SamusProjectile_BombTimers_address - 0xA + y; }
}

// ---- $A0:9A5A.. extended-spritemap collision, $A0:A07A.. normal collision -----------------------
namespace {
// Iterates the extended spritemap's hitbox entries, invoking `test(entryX, entryY, hitbox)` for every
// hitbox; `test` returns true to stop (a hit was handled). Mirrors the nested loops at $9A5A/$9B7F.
template<class F> bool for_each_hitbox(Game& g, uint16_t x, F test) {
    const uint8_t bank = bank_of(g, x);
    uint16_t map = g.Enemy_spritemap(x);
    if (map < 0x8000) return false;   // the original spins forever here
    uint16_t entries = g.rom_w(bank, map) & 0x00FF;
    uint16_t entry = uint16_t(map + 2);
    while (entries) {
        const uint16_t ex = uint16_t(g.Enemy_XPosition(x) + g.rom_w(bank, entry));
        const uint16_t ey = uint16_t(g.Enemy_YPosition(x) + g.rom_w(bank, uint16_t(entry + 2)));
        const uint16_t hitboxes_ptr = g.rom_w(bank, uint16_t(entry + 6));
        const uint16_t count = g.rom_w(bank, hitboxes_ptr);
        if (count) {
            uint16_t hitbox = uint16_t(hitboxes_ptr + 2);
            for (int16_t remaining = int16_t(count); remaining > 0; --remaining, hitbox = uint16_t(hitbox + 12))
                if (test(ex, ey, hitbox)) return true;
        }
        entry = uint16_t(entry + 8); --entries;
    }
    return false;
}
uint16_t hb(Game& g, uint16_t x, uint16_t hitbox, uint16_t field) { return g.rom_w(bank_of(g, x), uint16_t(hitbox + field)); }
}

// Enemy vs Samus, extended spritemap ($A0:9A5A)
static void Enemy_vs_Samus_Ext(Game& g) {
    const uint16_t x = g.EnemyIndex();
    if (!g.Enemy_spritemap(x)) return;
    const uint16_t touch = hdr_w(g, g.Enemy_ID(x), header::touch);
    if (ignorable_ai(touch)) return;
    if (g.ContactDamageIndex()) g.SamusInvincibilityTimer() = 0;
    else if (g.SamusInvincibilityTimer()) return;
    if (g.Enemy_spritemap(x) < 0x8000) return;
    const uint16_t right = uint16_t(g.SamusXPosition() + g.SamusXRadius()), left = uint16_t(g.SamusXPosition() - g.SamusXRadius());
    const uint16_t bottom = uint16_t(g.SamusYPosition() + g.SamusYRadius()), top = uint16_t(g.SamusYPosition() - g.SamusYRadius());
    for_each_hitbox(g, x, [&](uint16_t ex, uint16_t ey, uint16_t hitbox) {
        if (!n16(uint16_t(ex + hb(g, x, hitbox, 0) - right))) return false;
        if (n16(uint16_t(ex + hb(g, x, hitbox, 4) - left))) return false;
        if (!n16(uint16_t(ey + hb(g, x, hitbox, 2) - bottom))) return false;
        if (n16(uint16_t(ey + hb(g, x, hitbox, 6) - top))) return false;
        run_ai(g, x, hb(g, x, hitbox, 8));
        return true;
    });
}

// Enemy vs projectile / bomb, extended spritemap ($A0:9B7F, $A0:9D23)
static void Enemy_vs_Projectile_Ext(Game& g) {
    const uint16_t x = g.EnemyIndex();
    if (!g.SamusProjectile_ProjectileCounter()) return;
    if (!g.Enemy_spritemap(x) || g.Enemy_spritemap(x) == kNothingExtended) return;
    const uint16_t shot = hdr_w(g, g.Enemy_ID(x), header::shot);
    if (ignorable_ai(shot)) return;
    if ((g.Enemy_properties(x) & 0x0400) || g.Enemy_invincibilityTimer(x) || g.Enemy_ID(x) == kRespawn) return;
    for (g.CollisionIndex() = 0; g.CollisionIndex() < 5; ++g.CollisionIndex()) {
        const uint16_t y = uint16_t(g.CollisionIndex() * 2);
        if (!proj_type(g, y)) continue;
        const uint16_t kind = proj_type(g, y) & 0x0F00;
        if (kind == 0x0300 || kind == 0x0500 || kind >= 0x0700) continue;
        const int px = g.SamusProjectile_XPositions(y), py = g.SamusProjectile_YPositions(y);
        const uint16_t xr = g.SamusProjectile_XRadii(y), yr = g.SamusProjectile_YRadii(y);
        const bool hit = for_each_hitbox(g, x, [&](uint16_t ex, uint16_t ey, uint16_t hitbox) {
            if (n16(uint16_t(px + xr - uint16_t(ex + hb(g, x, hitbox, 0))))) return false;
            if (!n16(uint16_t(px - xr - uint16_t(ex + hb(g, x, hitbox, 4))))) return false;
            if (n16(uint16_t(py + yr - uint16_t(ey + hb(g, x, hitbox, 2))))) return false;
            if (!n16(uint16_t(py - yr - uint16_t(ey + hb(g, x, hitbox, 6))))) return false;
            if (kind == 0x0200) { g.EarthquakeTimer() = 0x1E; g.EarthquakeType() = 0x12; }
            if ((g.Enemy_properties(x) & 0x1000) || !(proj_type(g, y) & 0x0008))
                g.SamusProjectile_Directions(y) |= 0x0010;
            run_ai(g, x, hb(g, x, hitbox, 10));
            return true;
        });
        if (hit) return;
    }
}
static void Enemy_vs_Bomb_Ext(Game& g) {
    const uint16_t x = g.EnemyIndex();
    if (!g.Enemy_spritemap(x) || (g.Enemy_properties(x) & 0x0400) || g.Enemy_invincibilityTimer(x)) return;
    const uint16_t shot = hdr_w(g, g.Enemy_ID(x), header::shot);
    if (ignorable_ai(shot) || !g.SamusProjectile_BombCounter()) return;
    for (g.CollisionIndex() = 5; g.CollisionIndex() != 0x0A; ++g.CollisionIndex()) {
        const uint16_t y = uint16_t(g.CollisionIndex() * 2);
        if (!g.SamusProjectile_XPositions(y)) continue;
        if (!proj_type(g, y) || (proj_type(g, y) & 0x0F00) != 0x0500) continue;
        if (g.word_at(bomb_timer_address(y))) continue;
        const int px = g.SamusProjectile_XPositions(y), py = g.SamusProjectile_YPositions(y);
        const uint16_t xr = g.SamusProjectile_XRadii(y), yr = g.SamusProjectile_YRadii(y);
        const bool hit = for_each_hitbox(g, x, [&](uint16_t ex, uint16_t ey, uint16_t hitbox) {
            if (n16(uint16_t(px + xr - uint16_t(ex + hb(g, x, hitbox, 0))))) return false;
            if (!n16(uint16_t(px - xr - uint16_t(ex + hb(g, x, hitbox, 4))))) return false;
            if (n16(uint16_t(py + yr - uint16_t(ey + hb(g, x, hitbox, 2))))) return false;
            if (!n16(uint16_t(py - yr - uint16_t(ey + hb(g, x, hitbox, 6))))) return false;
            g.SamusProjectile_Directions(y) |= 0x0010;
            run_ai(g, x, hb(g, x, hitbox, 10));
            return true;
        });
        if (hit) return;
    }
}

// Enemy vs Samus, normal hitbox ($A0:A07A)
static void Enemy_vs_Samus(Game& g) {
    const uint16_t x = g.EnemyIndex();
    if (!g.Enemy_spritemap(x)) return;
    if (g.ContactDamageIndex()) g.SamusInvincibilityTimer() = 0;
    else if (g.SamusInvincibilityTimer()) {
        if (g.Enemy_ID(x) != kRespawn) return;
        const uint16_t marker = g.word_at(Game::EnemyTileData_address + x);
        if (!marker || marker == 8) return;
    }
    const uint16_t touch = hdr_w(g, g.Enemy_ID(x), header::touch);
    if (ignorable_ai(touch)) return;
    if (!within(abs16(uint16_t(g.SamusXPosition() - g.Enemy_XPosition(x))), g.SamusXRadius(), g.Enemy_XHitboxRadius(x))) return;
    if (!within(abs16(uint16_t(g.SamusYPosition() - g.Enemy_YPosition(x))), g.SamusYRadius(), g.Enemy_YHitboxRadius(x))) return;
    if (g.Enemy_ID(x) != kRespawn && g.Enemy_freezeTimer(x)) return;
    run_ai(g, x, touch);
}
// Enemy vs projectile, normal hitbox ($A0:A143)
static void Enemy_vs_Projectile(Game& g) {
    const uint16_t x = g.EnemyIndex();
    if (!g.SamusProjectile_ProjectileCounter()) return;
    if (!g.Enemy_spritemap(x) || g.Enemy_spritemap(x) == kNothingSpritemap) return;
    if ((g.Enemy_properties(x) & 0x0400) || g.Enemy_ID(x) == kRespawn || g.Enemy_invincibilityTimer(x)) return;
    for (g.CollisionIndex() = 0; g.CollisionIndex() != 5; ++g.CollisionIndex()) {
        const uint16_t y = uint16_t(g.CollisionIndex() * 2);
        if (!proj_type(g, y)) continue;
        const uint16_t kind = proj_type(g, y) & 0x0F00;
        if (kind == 0x0300 || kind == 0x0500 || kind >= 0x0700) continue;
        if (!within(abs16(uint16_t(g.SamusProjectile_XPositions(y) - g.Enemy_XPosition(x))), g.SamusProjectile_XRadii(y), g.Enemy_XHitboxRadius(x))) continue;
        if (!within(abs16(uint16_t(g.SamusProjectile_YPositions(y) - g.Enemy_YPosition(x))), g.SamusProjectile_YRadii(y), g.Enemy_YHitboxRadius(x))) continue;
        if (kind == 0x0200) { g.EarthquakeTimer() = 0x1E; g.EarthquakeType() = 0x12; }
        if ((g.Enemy_properties(x) & 0x1000) || !(proj_type(g, y) & 0x0008)) g.SamusProjectile_Directions(y) |= 0x0010;
        run_ai(g, x, hdr_w(g, g.Enemy_ID(x), header::shot));
        return;
    }
}
static void Enemy_vs_Bomb(Game& g) {
    const uint16_t x = g.EnemyIndex();
    if (!g.SamusProjectile_BombCounter() || !g.Enemy_spritemap(x) || g.Enemy_invincibilityTimer(x) || g.Enemy_ID(x) == kRespawn) return;
    for (g.CollisionIndex() = 5; g.CollisionIndex() != 0x0A; ++g.CollisionIndex()) {
        const uint16_t y = uint16_t(g.CollisionIndex() * 2);
        if (!proj_type(g, y)) continue;
        if (g.word_at(bomb_timer_address(y))) continue;
        if ((proj_type(g, y) & 0x0F00) != 0x0500 && !(proj_type(g, y) & 0x8000)) continue;
        if (!within(abs16(uint16_t(g.SamusProjectile_XPositions(y) - g.Enemy_XPosition(x))), g.SamusProjectile_XRadii(y), g.Enemy_XHitboxRadius(x))) continue;
        if (!within(abs16(uint16_t(g.SamusProjectile_YPositions(y) - g.Enemy_YPosition(x))), g.SamusProjectile_YRadii(y), g.Enemy_YHitboxRadius(x))) continue;
        if (g.word_at(bomb_timer_address(y))) continue;
        g.SamusProjectile_Directions(y) |= 0x0010;
        run_ai(g, x, hdr_w(g, g.Enemy_ID(x), header::shot));
        return;
    }
}

static void EnemyCollisionHandling(Game& g) {
    if (g.Enemy_properties2(g.EnemyIndex()) & 0x0004) {
        Enemy_vs_Projectile_Ext(g); Enemy_vs_Bomb_Ext(g); Enemy_vs_Samus_Ext(g);
    } else {
        Enemy_vs_Projectile(g); Enemy_vs_Bomb(g); Enemy_vs_Samus(g);
    }
}

// ---- $A0:8FD4 Main enemy routine --------------------------------------------------------------------
void Main_Enemy_Routine(Game& g) {
    g.enemies_drawn.clear();
    if (g.FirstFreeEnemyIndex()) {
        if (g.EnemyIndexToShake() != kNone) { g.Enemy_shakeTimer(g.EnemyIndexToShake()) = 0x40; g.EnemyIndexToShake() = kNone; }
        g.InteractiveEnemyIndicesIndex() = 0; g.ActiveEnemyIndicesIndex() = 0;
        for (unsigned guard = 0; guard < 64; ++guard) {
            const uint16_t x = g.word_at(Game::ActiveEnemyIndices_address + g.ActiveEnemyIndicesIndex());
            if (x == kNone) break;
            g.EnemyIndex() = x; g.EnemyDataPointer() = uint16_t(x + Game::Enemy_ID_address);
            g.byte_at(Game::EnemyAIPointer_address + 2) = bank_of(g, x);
            const bool frozen = g.TimeIsFrozenFlag() != 0;
            enum { ProcessAI, ProcessAIEnd, DrawEnd } next = ProcessAI;
            if (!(g.Enemy_properties(x) & 0x0400)) {
                if (g.Enemy_invincibilityTimer(x)) --g.Enemy_invincibilityTimer(x);
                else {
                    if (!frozen) {
                        EnemyCollisionHandling(g);
                        if (!g.Enemy_ID(x)) next = DrawEnd;
                    }
                    if (next != DrawEnd && (g.Enemy_properties2(x) & 0x0001)) next = ProcessAIEnd;
                }
            }
            if (next == ProcessAI) {
                g.DisableDrawingOfEnemies() = 0;
                bool run = true;
                if (frozen) {
                    const uint16_t ai = hdr_w(g, g.Enemy_ID(x), header::time_frozen_ai);
                    if (!ai) run = false; else run_ai(g, x, ai);
                } else {
                    unsigned index = 0; uint16_t bits = g.Enemy_AI(x);
                    if (bits) { do { ++index; } while (!(bits & 1) ? (bits >>= 1, true) : false); }
                    run_ai(g, x, hdr_w(g, g.Enemy_ID(x), header::main_ai + index * 2));
                }
                if (run && !frozen) {
                    ++g.Enemy_frameCounter(x);
                    if (g.Enemy_properties(x) & 0x2000) ProcessEnemyInstructions(g);
                }
                next = ProcessAIEnd;
            }
            if (next == ProcessAIEnd) {
                if (g.Enemy_properties2(x) & 0x0001) {
                    if (g.Enemy_flashTimer(x) == 1 || g.Enemy_freezeTimer(x) == 1) {
                        g.word_at(Game::EnemyTileData_address + 2 + x) = 0;
                        EnemyDeath(g, 0);
                    }
                }
                bool draw = (g.Enemy_properties2(x) & 0x0004) != 0;
                if (!draw) draw = CheckIfEnemyIsOnScreen(g, x);
                if (draw && !(g.Enemy_properties(x) & 0x0300) && !(g.DisableDrawingOfEnemies() & 1))
                    g.enemies_drawn.push_back(x);
            }
            if (g.Enemy_flashTimer(x) && !g.TimeIsFrozenFlag()) {
                --g.Enemy_flashTimer(x);
                if (int16_t(g.Enemy_flashTimer(x)) < 8) g.Enemy_AI(x) &= 0xFFFD;
            }
            g.ActiveEnemyIndicesIndex() += 2;
        }
    }
    ++g.NumberOfTimesMainEnemyRoutineExecuted();
    g.EnemyIndexSamusCollidesLeft() = kNone; g.EnemyIndexSamusCollidesRight() = kNone;
    g.EnemyIndexSamusCollidesUp() = kNone; g.EnemyIndexSamusCollidesDown() = kNone;
}

// ---- $A0:9785 Samus / projectile interaction (bombs and reflected projectiles) -----------------------
void Samus_Projectiles_Interaction_Handling(Game& g) {
    if (g.DisableSamusVsProjectileInteraction()) return;
    uint16_t limit = 5;
    if (g.SamusProjectile_BombCounter()) limit = 10;
    else if (!g.SamusProjectile_ProjectileCounter()) return;
    if (g.ProjectileInvincibilityTimer() || g.ContactDamageIndex()) return;
    for (g.CollisionIndex() = 0; g.CollisionIndex() != limit; ++g.CollisionIndex()) {
        const uint16_t y = uint16_t(g.CollisionIndex() * 2);
        if (!g.SamusProjectile_Damages(y)) continue;
        const uint16_t type = proj_type(g, y);
        if ((type & 0x8000) || (type & 0x0F00) >= 0x0700) continue;
        if (g.SamusProjectile_Directions(y) & 0x0010) continue;
        if (!within(abs16(uint16_t(g.SamusProjectile_XPositions(y) - g.SamusXPosition())), g.SamusProjectile_XRadii(y), g.SamusXRadius())) continue;
        if (!within(abs16(uint16_t(g.SamusProjectile_YPositions(y) - g.SamusYPosition())), g.SamusProjectile_YRadii(y), g.SamusYRadius())) continue;
        const uint16_t kind = type & 0xFF00;
        if (kind == 0x0300 || kind == 0x0500) {
            if (g.word_at(bomb_timer_address(y)) != 8) continue;
            g.BombJumpDirection() = g.SamusXPosition() == g.SamusProjectile_XPositions(y) ? 2
                : (int16_t(g.SamusXPosition() - g.SamusProjectile_XPositions(y)) < 0 ? 1 : 3);
            continue;
        }
        g.SamusProjectile_Directions(y) |= 0x0010;
        Deal_A_Damage_to_Samus(g, Suit_Damage_Division(g, g.SamusProjectile_Damages(y)));
        g.SamusInvincibilityTimer() = 0x60; g.SamusKnockbackTimer() = 5;
        g.KnockbackXDirection() = int16_t(g.SamusXPosition() - g.EnemyProjectile_XPositions(g.EnemyIndex())) < 0 ? 0 : 1;
        return;
    }
}

// ---- Enemy projectiles vs Samus / projectiles (projectile engine lives in bank $86) -------------------
namespace {
void HandleEnemyProjectileCollisionWithSamus(Game& g, uint16_t x) {
    g.SamusInvincibilityTimer() = 0x60; g.SamusKnockbackTimer() = 5;
    const uint16_t id = g.EnemyProjectile_ID(x);
    const uint16_t shot_list = g.rom_w(0x86, uint16_t((labels::EnemyProjectiles & 0xFFFF) + id + 0x0A));
    if (shot_list) { g.EnemyProjectile_InstListPointers(x) = shot_list; g.EnemyProjectile_InstructionTimers(x) = 1; }
    if (!(g.EnemyProjectile_Properties(x) & 0x4000)) g.EnemyProjectile_ID(x) = 0;
    Deal_A_Damage_to_Samus(g, Suit_Damage_Division(g, g.EnemyProjectile_Properties(x) & 0x0FFF));
    g.KnockbackXDirection() = int16_t(g.SamusXPosition() - g.EnemyProjectile_XPositions(x)) < 0 ? 0 : 1;
}
}
void EnemyProjectile_Samus_Collision_Handling(Game& g) {
    if (g.SamusInvincibilityTimer() || g.ContactDamageIndex()) return;
    for (int16_t index = 0x22; index >= 0; index -= 2) {
        g.CollisionIndex() = uint16_t(index);
        const uint16_t x = uint16_t(index);
        if (!g.EnemyProjectile_ID(x) || (g.EnemyProjectile_Properties(x) & 0x2000)) continue;
        const uint16_t xr = g.EnemyProjectile_Radii(x) & 0x00FF, yr = (g.EnemyProjectile_Radii(x) >> 8) & 0x00FF;
        if (!xr || !yr) continue;
        if (!within(abs16(uint16_t(g.SamusXPosition() - g.EnemyProjectile_XPositions(x))), g.SamusXRadius(), xr)) continue;
        if (!within(abs16(uint16_t(g.SamusYPosition() - g.EnemyProjectile_YPositions(x))), g.SamusYRadius(), yr)) continue;
        HandleEnemyProjectileCollisionWithSamus(g, x);
    }
}
void Projectile_vs_Projectile_Collision_Handling(Game& g) {
    if (!g.SamusProjectile_ProjectileCounter()) return;
    for (int16_t index = 0x22; index >= 0; index -= 2) {
        const uint16_t x = uint16_t(index);
        g.CollisionIndex() = x;
        if (!g.EnemyProjectile_ID(x) || !(g.EnemyProjectile_Properties(x) & 0x8000)) continue;
        for (uint16_t y = 0; y < 10; y = uint16_t(y + 2)) {
            if (g.word_at(Game::EnemyProjectileData_CollisionOptions_address + x) == 2) break;
            const uint16_t type = proj_type(g, y);
            if (!type) continue;
            const uint16_t kind = type & 0x0F00;
            if (kind == 0x0300 || kind == 0x0500 || kind >= 0x0700) continue;
            if ((g.SamusProjectile_XPositions(y) & 0xFFE0) != (g.EnemyProjectile_XPositions(x) & 0xFFE0)) continue;
            if ((g.SamusProjectile_YPositions(y) & 0xFFE0) != (g.EnemyProjectile_YPositions(x) & 0xFFE0)) continue;
            if (!(type & 0x0008)) g.SamusProjectile_Directions(y) |= 0x0010;
            if (g.word_at(Game::EnemyProjectileData_CollisionOptions_address + x) == 1) { g.queue_sound(0x3D); continue; }
            g.EnemyProjectile_CollidedProjectileType(x) = type;
            const uint16_t id = g.EnemyProjectile_ID(x);
            g.EnemyProjectile_InstListPointers(x) = g.rom_w(0x86, uint16_t((labels::EnemyProjectiles & 0xFFFF) + id + 0x0C));
            g.EnemyProjectile_InstructionTimers(x) = 1;
            g.EnemyProjectile_Properties(x) &= 0x0FFF;
        }
    }
}

// ---- $A0:A306 Power bomb interaction ---------------------------------------------------------------------
void Process_Enemy_PowerBomb_Interaction(Game& g) {
    const uint16_t radius_x = g.byte_at(Game::SamusProjectile_PowerBombExplosionRadius_address + 1);
    if (!radius_x) return;
    const uint16_t ry = uint16_t(((radius_x >> 1) + radius_x + (radius_x & 1)) >> 1);   // LSR; ADC $12; LSR
    for (int16_t x = 0x7C0; x >= 0; x = int16_t(x - 0x40)) {
        g.EnemyIndex() = uint16_t(x);
        if (g.Enemy_invincibilityTimer(uint16_t(x)) || !g.Enemy_ID(uint16_t(x)) || g.Enemy_ID(uint16_t(x)) == kRespawn) continue;
        const uint32_t table = vulnerabilities(g, g.Enemy_ID(uint16_t(x)));
        if (!(g.rom_b(table + vuln::power_bomb) & 0x7F)) continue;
        if (abs16(uint16_t(g.SamusProjectile_PowerBombExplosionXPosition() - g.Enemy_XPosition(uint16_t(x)))) >= radius_x) continue;
        if (abs16(uint16_t(g.SamusProjectile_PowerBombExplosionYPosition() - g.Enemy_YPosition(uint16_t(x)))) >= ry) continue;
        uint16_t reaction = hdr_w(g, g.Enemy_ID(uint16_t(x)), header::power_bomb_reaction);
        if (!reaction) reaction = uint16_t(labels::Common_NormalEnemyPowerBombAI & 0xFFFF);
        run_ai(g, uint16_t(x), reaction);
        g.Enemy_properties(uint16_t(x)) |= 0x0800;
    }
}

// ---- Damage AIs ($A0:A477..A8BC) ---------------------------------------------------------------------------
void NormalEnemyTouchAI_NoDeathCheck(Game& g) {
    const uint16_t x = g.EnemyIndex();
    const uint16_t id = g.Enemy_ID(x);
    if (!g.ContactDamageIndex()) {
        Deal_A_Damage_to_Samus(g, Suit_Damage_Division(g, hdr_w(g, id, header::damage)));
        g.SamusInvincibilityTimer() = 0x60; g.SamusKnockbackTimer() = 5;
        g.KnockbackXDirection() = int16_t(g.SamusXPosition() - g.Enemy_XPosition(x)) < 0 ? 0 : 1;
        return;
    }
    uint16_t offset = uint16_t(g.ContactDamageIndex() + 0x0F), damage;
    switch (g.ContactDamageIndex()) {
    case 1: damage = 0x01F4; break;   // speed boosting
    case 2: damage = 0x012C; break;   // shinesparking
    case 3: damage = 0x07D0; break;   // screw attack
    default:
        ++offset; damage = 0x00C8;    // pseudo screw attack
        if (g.ContactDamageIndex() == 4 && g.samus_command) g.samus_command(g, 4);
        break;
    }
    const uint16_t multiplier_byte = g.rom_b(vulnerabilities(g, id) + offset);
    g.Temp_ContactVulnerability() = multiplier_byte;
    const uint16_t multiplier = multiplier_byte & 0x7F;
    g.Temp_DamageMultiplier() = multiplier;
    if (!multiplier || multiplier == 0xFF) return;
    const uint16_t final_damage = uint16_t(Multiplication_32bit(uint16_t(damage >> 1), multiplier) >> 16);
    if (!final_damage) return;
    uint16_t hurt = g.rom_b(header_address(id) + header::hurt_ai_time);
    g.Enemy_flashTimer(x) = hurt ? hurt : 4;
    g.Enemy_AI(x) |= 0x0002; g.SamusInvincibilityTimer() = 0; g.SamusKnockbackTimer() = 0;
    const int16_t left = int16_t(g.Enemy_health(x) - final_damage);
    g.Enemy_health(x) = left < 0 ? 0 : uint16_t(left);
    g.queue_sound(0x0B);
}
void NormalEnemyTouchAI(Game& g) {
    NormalEnemyTouchAI_NoDeathCheck(g);
    const uint16_t x = g.EnemyIndex();
    if (g.Enemy_health(x)) return;
    g.word_at(Game::EnemyTileData_address + 2 + x) = 6;
    EnemyDeath(g, 1);
}
void NormalEnemyPowerBombAI_NoDeathCheck(Game& g) {
    const uint16_t x = g.EnemyIndex();
    const uint16_t id = g.Enemy_ID(x);
    const uint16_t byte = g.rom_b(vulnerabilities(g, id) + vuln::power_bomb);
    if (byte == 0xFF) return;
    const uint16_t multiplier = byte & 0x7F;
    g.Temp_DamageMultiplier() = multiplier;
    if (!multiplier) return;
    const uint16_t damage = uint16_t(Multiplication_32bit(0x00C8 >> 1, multiplier) >> 16);
    g.EnemySpritemapEntryXPositionDuringCollision() = damage;
    if (!damage) return;
    g.Enemy_invincibilityTimer(x) = 0x30;
    uint16_t hurt = g.rom_b(header_address(id) + header::hurt_ai_time);
    g.Enemy_flashTimer(x) = uint16_t((hurt ? hurt : 4) + 8);
    g.Enemy_AI(x) |= 0x0002;
    g.Enemy_health(x) = g.Enemy_health(x) > damage ? uint16_t(g.Enemy_health(x) - damage) : 0;
}
void NormalEnemyPowerBombAI(Game& g) {
    NormalEnemyPowerBombAI_NoDeathCheck(g);
    const uint16_t x = g.EnemyIndex();
    if (g.Enemy_health(x)) return;
    g.word_at(Game::EnemyTileData_address + 2 + x) = 3;
    EnemyDeath(g, 0);
}

void NormalEnemyShotAI_NoDeathCheck_NoEnemyShotGraphic(Game& g) {
    const uint16_t x = g.EnemyIndex();
    const uint16_t projectile = uint16_t(g.CollisionIndex() * 2);
    uint16_t damage = g.SamusProjectile_Damages(projectile);
    g.EnemySpritemapEntryXPositionDuringCollision() = damage;
    const uint16_t type = g.SamusProjectile_Types(projectile);
    const uint32_t table = vulnerabilities(g, g.Enemy_ID(x));
    auto no_damage = [&]() {
        g.SamusProjectile_Directions(projectile) |= 0x0010;
        g.queue_sound(0x3D);
    };
    auto freeze = [&]() {
        if (!g.Enemy_freezeTimer(x)) g.queue_sound(0x0A);
        g.Enemy_freezeTimer(x) = g.AreaIndex() == 2 ? 0x012C : 0x0190;
        g.Enemy_AI(x) |= 0x0004; g.Enemy_invincibilityTimer(x) = 0x0A;
    };
    uint16_t multiplier;
    if (!(type & 0x0F00)) {   // beam
        const uint16_t beam = g.rom_b(table + (type & 0x000F));
        g.Temp_BeamVulnerability() = beam;
        multiplier = beam & 0x7F;
        g.Temp_DamageMultiplier() = multiplier;
        if (beam == 0xFF) { freeze(); return; }
        if (type & 0x0010) {
            const uint16_t charged = g.rom_b(table + vuln::charged_beam);
            if (charged == 0xFF || !(charged & 0x0F)) { no_damage(); return; }
            multiplier = charged & 0x0F; g.Temp_DamageMultiplier() = multiplier;
        }
    } else {
        const uint16_t kind = type & 0x0F00;
        if (kind == 0x0100 || kind == 0x0200) multiplier = g.rom_b(table + 0x0C + ((kind >> 8) - 1)) & 0x7F;
        else if (kind == 0x0500) multiplier = g.rom_b(table + vuln::bomb) & 0x7F;
        else if (kind == 0x0300) multiplier = g.rom_b(table + vuln::power_bomb) & 0x7F;
        else { no_damage(); return; }
        g.Temp_DamageMultiplier() = multiplier;
    }
    damage = uint16_t(Multiplication_32bit(uint16_t(damage >> 1), multiplier) >> 16);
    if (!damage) { no_damage(); return; }
    g.EnemySpritemapEntryXPositionDuringCollision() = damage;
    uint16_t hurt = g.rom_b(header_address(g.Enemy_ID(x)) + header::hurt_ai_time);
    g.Enemy_flashTimer(x) = uint16_t((hurt ? hurt : 4) + 8);
    g.Enemy_AI(x) |= 0x0002;
    if (!g.Enemy_freezeTimer(x)) {
        if (hdr_w(g, g.Enemy_ID(x), header::cry)) g.queue_sound(0x0000 | hdr_w(g, g.Enemy_ID(x), header::cry));
        ++g.Temp_ShotAIHitFlag();
    }
    if (g.SamusProjectile_Types(projectile) & 0x0008) g.Enemy_invincibilityTimer(x) = 0x10;
    if (g.Enemy_health(x) > damage) { g.Enemy_health(x) = uint16_t(g.Enemy_health(x) - damage); return; }
    // Lethal hit: an ice-beam kill freezes instead of killing (unless the enemy resists freezing).
    if ((g.SamusProjectile_Types(projectile) & 0x0002) && (g.Temp_BeamVulnerability() & 0xF0) != 0x80 && !g.Enemy_freezeTimer(x)) {
        g.Enemy_freezeTimer(x) = g.AreaIndex() == 2 ? 0x012C : 0x0190;
        g.Enemy_AI(x) |= 0x0004; g.Enemy_invincibilityTimer(x) = 0x0A;
        g.queue_sound(0x0A);
        return;
    }
    g.Enemy_health(x) = 0;
}
void NormalEnemyShotAI_NoDeathCheck(Game& g) {
    g.Temp_ShotAIHitFlag() = 0;
    NormalEnemyShotAI_NoDeathCheck_NoEnemyShotGraphic(g);
}
void NormalEnemyShotAI(Game& g) {
    g.Temp_ShotAIHitFlag() = 0;
    NormalEnemyShotAI_NoDeathCheck_NoEnemyShotGraphic(g);
    const uint16_t x = g.EnemyIndex();
    if (g.Enemy_health(x)) return;
    const uint16_t projectile = uint16_t(g.CollisionIndex() * 2);
    const uint16_t weapon = (g.SamusProjectile_Types(projectile) >> 8) & 0x000F;
    g.word_at(Game::EnemyTileData_address + 2 + x) = weapon;
    uint16_t animation = hdr_w(g, g.Enemy_ID(x), header::death_animation);
    if (weapon == 2) { if (animation < 3) animation = 2; }
    EnemyDeath(g, animation);
}
void CreateADudShot(Game& g) {
    const uint16_t projectile = uint16_t(g.CollisionIndex() * 2);
    g.queue_sound(0x3D);
    g.SamusProjectile_Directions(projectile) |= 0x0010;
}

// ---- $A0:A3AF Enemy death ------------------------------------------------------------------------------------
void EnemyDeath(Game& g, uint16_t animation) {
    const uint16_t x = g.EnemyIndex();
    if (g.Enemy_AI(x) == 1) g.GrappleBeam_Function() = uint16_t(labels::GrappleBeamFunction_Dropped & 0xFFFF);
    if (animation >= 5) animation = 0;
    g.Temp_DeathExplosionType() = animation;
    if (g.spawn_enemy_projectile) g.spawn_enemy_projectile(g, uint16_t(labels::EnemyProjectile_EnemyDeathExplosion & 0xFFFF), animation);
    const bool respawns = (g.Enemy_properties(x) & 0x4000) != 0;
    for (uint16_t i = 0; i < 0x40; i = uint16_t(i + 2)) g.word_at(Game::Enemy_ID_address + x + i) = 0;
    if (respawns) { g.Enemy_ID(x) = kRespawn; g.Enemy_bank(x) = 0xA3; }
    ++g.NumberOfEnemiesKilled();
}
void RinkaDeath(Game& g, uint16_t animation) {
    if (animation >= 3) animation = 0;
    EnemyDeath(g, animation);
}
}
