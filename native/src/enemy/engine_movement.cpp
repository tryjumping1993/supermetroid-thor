// Bank $A0 common enemy engine, part 3: geometry, trigonometry, block collision and movement.
#include "thor/enemy_engine.hpp"
#include "reference_labels.hpp"

namespace thor::enemy {
namespace {
uint16_t level_word(Game& g, uint32_t block_index) { return g.word_at(Game::LevelData_address + block_index * 2); }
uint8_t bts_at(Game& g, uint32_t block_index) { return g.byte_at(Game::BTS_address + block_index); }
bool neg(int v) { return (v & 0x8000) != 0; }
}

// ---- Arithmetic helpers ---------------------------------------------------------------------------
uint32_t Multiplication_32bit(uint16_t a, uint16_t b) { return uint32_t(a) * uint32_t(b); }
uint16_t NegateA(uint16_t v) { return abs16(v); }
uint16_t GenerateRandomNumber(Game& g) {
    // $80:8111. r' = r * 5 + $111, with the original's byte-wise carry quirks.
    const uint16_t seed = g.RandomNumberSeed();
    const uint16_t product_low = uint16_t((seed & 0xFF) * 5), product_high = uint16_t((seed >> 8) * 5);
    const unsigned sum = (product_high & 0xFF) + (product_low >> 8) + 1;
    const uint16_t combined = uint16_t((uint16_t(sum & 0xFF) << 8) | (product_low & 0xFF));
    const uint16_t result = uint16_t(combined + 0x11 + (sum > 0xFF ? 1 : 0));
    g.RandomNumberSeed() = result;
    return result;
}

// ---- $A0:C0B1 angle from -Y axis in 256ths of a circle -------------------------------------------
uint16_t CalculateAngleOfXYOffset(uint16_t dx, uint16_t dy) {
    unsigned octant = 0;
    if (n16(dx)) { octant = 4; dx = uint16_t(0 - dx); }
    if (n16(dy)) { octant += 2; dy = uint16_t(0 - dy); }
    const bool upper = dy >= dx;   // |y| >= |x|
    const unsigned numerator = (upper ? dx : dy) & 0xFF, denominator = (upper ? dy : dx) & 0xFF;
    const uint16_t quotient = denominator ? uint16_t((numerator << 8) / denominator) : 0xFFFF;
    const uint16_t q = uint16_t(quotient >> 3);
    if (upper) {
        switch (octant) {
        case 0: return uint16_t((0x80 - q) & 0xFF);   // bottom right, lower octant
        case 2: return uint16_t(q & 0xFF);            // top right, upper octant
        case 4: return uint16_t((0x80 + q) & 0xFF);   // bottom left, lower octant
        default: return uint16_t((0x100 - q) & 0xFF); // top left, upper octant
        }
    }
    switch (octant) {
    case 0: return uint16_t((0x40 + q) & 0xFF);       // bottom right, upper octant
    case 2: return uint16_t((0x40 - q) & 0xFF);       // top right, lower octant
    case 4: return uint16_t((0xC0 - q) & 0xFF);       // bottom left, upper octant
    default: return uint16_t((0xC0 + q) & 0xFF);      // top left, lower octant
    }
}
uint16_t CalculateAngleOfSamusFromEnemy(Game& g, uint16_t x) {
    return CalculateAngleOfXYOffset(uint16_t(g.SamusXPosition() - g.Enemy_XPosition(x)), uint16_t(g.SamusYPosition() - g.Enemy_YPosition(x)));
}
uint16_t CalculateAngleOfSamusFromEnemyProjectile(Game& g, uint16_t p) {
    return CalculateAngleOfXYOffset(uint16_t(g.SamusXPosition() - g.EnemyProjectile_XPositions(p)), uint16_t(g.SamusYPosition() - g.EnemyProjectile_YPositions(p)));
}
uint16_t CalculateAngleOfEnemyYFromEnemyX(Game& g, uint16_t target, uint16_t origin) {
    return CalculateAngleOfXYOffset(uint16_t(g.Enemy_XPosition(target) - g.Enemy_XPosition(origin)), uint16_t(g.Enemy_YPosition(target) - g.Enemy_YPosition(origin)));
}

// ---- $A0:B0B2.. 8-bit trigonometry ------------------------------------------------------------------
static Fixed16 sine_product(Game& g, uint16_t angle, uint8_t radius) {
    g.Temp_Angle() = angle; g.Temp_Radius() = radius;
    const uint8_t s = g.rom_b(labels::SineCosineTables_8bitSine + (angle & 0x7F));
    const uint16_t product = uint16_t(s * radius);
    uint16_t whole = product >> 8, fraction = uint16_t((product & 0xFF) << 8);
    if (angle & 0x80) { whole = uint16_t(0 - whole); fraction = uint16_t(0 - fraction); }   // no carry between words (original bug)
    g.Temp_SineProduct() = whole; g.Temp_SineProductFractionalPart() = fraction;
    return {whole, fraction};
}
Fixed16 EightBitSineMultiplication(Game& g, uint16_t angle, uint8_t radius) { return sine_product(g, angle & 0xFF, radius); }
Fixed16 EightBitCosineMultiplication(Game& g, uint16_t angle, uint8_t radius) { return sine_product(g, (angle + 0x40) & 0xFF, radius); }
Fixed16 EightBitNegativeSineMultiplication(Game& g, uint16_t angle, uint8_t radius) { return sine_product(g, (angle + 0x80) & 0xFF, radius); }

TrigResult Do_Some_Math_With_Sine_Cosine(Game& g, uint16_t angle, uint16_t magnitude) {
    auto product = [&](uint16_t a) {
        const uint16_t s = g.rom_w(labels::UnsignedSineTable + ((a & 0x7F) << 1));
        return Multiplication_32bit(s, magnitude);
    };
    const uint32_t sin_value = product(uint16_t(angle + 0x80)), cos_value = product(uint16_t(angle + 0x40));
    g.word_at(0x1A) = uint16_t(sin_value >> 16); g.word_at(0x1C) = uint16_t(sin_value);
    g.word_at(0x16) = uint16_t(cos_value >> 16); g.word_at(0x18) = uint16_t(cos_value);
    return {cos_value, sin_value};
}

void MoveEnemyAccordingToAngleAndXYSpeeds(Game& g, uint16_t x, uint16_t angle, uint16_t xspeed, uint16_t xsub, uint16_t yspeed, uint16_t ysub) {
    auto move = [&](Word position, Word sub, uint16_t speed, uint16_t subspeed, bool subtract) {
        const uint32_t p = (uint32_t(position.get()) << 16) | sub.get();
        const uint32_t d = (uint32_t(speed) << 16) | subspeed;
        const uint32_t r = subtract ? p - d : p + d;
        position.set(uint16_t(r >> 16)); sub.set(uint16_t(r));
    };
    move(g.Enemy_XPosition(x), g.Enemy_XSubPosition(x), xspeed, xsub, ((angle + 0x40) & 0x80) != 0);
    move(g.Enemy_YPosition(x), g.Enemy_YSubPosition(x), yspeed, ysub, ((angle + 0x80) & 0x80) != 0);
}

// ---- Position/geometry queries ----------------------------------------------------------------------------
bool CheckIfEnemyIsTouchingSamusFromBelow(Game& g, uint16_t x) {
    if (!within(abs16(uint16_t(g.SamusXPosition() - g.Enemy_XPosition(x))), g.SamusXRadius(), g.Enemy_XHitboxRadius(x))) return false;
    const uint16_t d = uint16_t(g.SamusYPosition() + 3 - g.Enemy_YPosition(x));
    if (!n16(d)) return false;
    const uint16_t a = uint16_t(0 - d);
    return a < g.SamusYRadius() || a - g.SamusYRadius() <= g.Enemy_YHitboxRadius(x);
}
bool CheckIfEnemyIsTouchingSamus(Game& g, uint16_t x) {
    const uint16_t adx = abs16(uint16_t(g.SamusXPosition() - g.Enemy_XPosition(x)));
    if (!(adx < g.SamusXRadius() || uint16_t(adx - g.SamusXRadius()) < g.Enemy_XHitboxRadius(x) || uint16_t(adx - g.SamusXRadius()) < 8)) return false;
    return within(abs16(uint16_t(g.SamusYPosition() - g.Enemy_YPosition(x))), g.SamusYRadius(), g.Enemy_YHitboxRadius(x));
}
bool CheckIfEnemyCenterIsOnScreen(Game& g, uint16_t x) {
    const int16_t ex = int16_t(g.Enemy_XPosition(x)), ey = int16_t(g.Enemy_YPosition(x));
    const int16_t lx = int16_t(g.Layer1XPosition()), ly = int16_t(g.Layer1YPosition());
    if (ex - lx < 0 || lx + 0x100 - ex < 0 || ey - ly < 0 || ly + 0x100 - ey < 0) return false;
    return true;
}
bool CheckIfEnemyCenterIsOverAPixelsOffScreen(Game& g, uint16_t x, uint16_t pixels) {
    const int16_t ex = int16_t(g.Enemy_XPosition(x)), ey = int16_t(g.Enemy_YPosition(x));
    const int16_t lx = int16_t(g.Layer1XPosition()), ly = int16_t(g.Layer1YPosition());
    return (ex + pixels - lx < 0) || (lx + 0x100 + pixels - ex < 0) || (ey + pixels - ly < 0) || (ly + 0x100 + pixels - ey < 0);
}
bool CheckIfEnemyIsOnScreen(Game& g, uint16_t x) {
    const int16_t ex = int16_t(g.Enemy_XPosition(x)), ey = int16_t(g.Enemy_YPosition(x));
    const int16_t lx = int16_t(g.Layer1XPosition()), ly = int16_t(g.Layer1YPosition());
    const int16_t xr = int16_t(g.Enemy_XHitboxRadius(x));
    return !(int16_t(ex + xr - lx) < 0) && !(int16_t(lx + 0x100 + xr - ex) < 0) &&
        !(int16_t(ey + 8 - ly) < 0) && !(int16_t(ly + 0xF8 - ey) < 0);
}
bool CheckIfEnemyIsHorizontallyOffScreen(Game& g, uint16_t x) {
    const int16_t ex = int16_t(g.Enemy_XPosition(x));
    if (ex < 0) return true;
    const int16_t xr = int16_t(g.Enemy_XHitboxRadius(x));
    const int16_t a = int16_t(ex + xr - int16_t(g.Layer1XPosition()));
    if (a < 0) return true;
    return !(int16_t(a - 0x100 - xr) < 0);
}
int16_t Get_SamusY_minus_EnemyY(Game& g, uint16_t x) { return int16_t(g.SamusYPosition() - g.Enemy_YPosition(x)); }
int16_t Get_SamusX_minus_EnemyX(Game& g, uint16_t x) { return int16_t(g.SamusXPosition() - g.Enemy_XPosition(x)); }
bool IsSamusWithingAPixelRowsOfEnemy(Game& g, uint16_t x, uint16_t pixels) {
    return n16(uint16_t(abs16(uint16_t(g.SamusYPosition() - g.Enemy_YPosition(x))) - pixels));
}
bool IsSamusWithinAPixelColumnsOfEnemy(Game& g, uint16_t x, uint16_t pixels) {
    return n16(uint16_t(abs16(uint16_t(g.SamusXPosition() - g.Enemy_XPosition(x))) - pixels));
}
bool CheckIfXDistanceBetweenEnemyAndSamusIsAtLeastA(Game& g, uint16_t x, uint16_t a) {
    return abs16(uint16_t(g.SamusXPosition() - g.Enemy_XPosition(x))) >= a;
}
uint16_t DetermineDirectionOfSamusFromEnemy(Game& g, uint16_t x) {
    if (IsSamusWithingAPixelRowsOfEnemy(g, x, 0x20)) return Get_SamusX_minus_EnemyX(g, x) >= 0 ? 2 : 7;
    if (IsSamusWithinAPixelColumnsOfEnemy(g, x, 0x20)) return Get_SamusY_minus_EnemyY(g, x) >= 0 ? 4 : 0;
    if (Get_SamusX_minus_EnemyX(g, x) >= 0) return Get_SamusY_minus_EnemyY(g, x) >= 0 ? 3 : 1;
    return Get_SamusY_minus_EnemyY(g, x) >= 0 ? 6 : 8;
}
bool CalculateDistanceAndAngleOfSamusFromEnemy(Game& g, uint16_t enemy_x, uint16_t enemy_y, uint16_t& distance) {
    const uint16_t dx = uint16_t(g.Temp_SamusXPosition() - enemy_x), dy = uint16_t(g.Temp_SamusYPosition() - enemy_y);
    (void)dx; (void)dy;
    const uint16_t sx = g.word_at(Game::Temp_SamusXPosition_address), sy = g.word_at(Game::Temp_SamusYPosition_address);
    const uint16_t xd = uint16_t(sx - enemy_x), yd = uint16_t(sy - enemy_y);
    const uint16_t ax = abs16(xd), ay = abs16(yd);
    if (int16_t(ax) >= 0xFF || int16_t(ay) >= 0xFF) return true;   // carry set: error
    const uint16_t reflected = CalculateAngleOfXYOffset(ax, ay);
    g.Temp_AngleFromEnemyToSamusReflectedDownRight() = reflected;
    Fixed16 a = EightBitNegativeSineMultiplication(g, reflected, uint8_t(ax));
    uint16_t xs = n16(a.whole) ? uint16_t(0 - a.whole) : a.whole;
    Fixed16 b = EightBitCosineMultiplication(g, reflected, uint8_t(ay));
    uint16_t ys = n16(b.whole) ? uint16_t(0 - b.whole) : b.whole;
    distance = uint16_t(xs + ys);
    g.Temp_AngleFromEnemyToSamus() = CalculateAngleOfXYOffset(xd, yd);
    return false;
}

// ---- Movement without collision ($A0:AF5A..AF90) ------------------------------------------------------------
namespace {
void add32(Word position, Word sub, uint16_t hi, uint16_t lo, bool subtract) {
    const uint32_t p = (uint32_t(position.get()) << 16) | sub.get(), d = (uint32_t(hi) << 16) | lo;
    const uint32_t r = subtract ? p - d : p + d;
    position.set(uint16_t(r >> 16)); sub.set(uint16_t(r));
}
}
void MoveEnemyX_minus_12_14(Game& g, uint16_t x, uint16_t lo, uint16_t hi) { add32(g.Enemy_XPosition(x), g.Enemy_XSubPosition(x), hi, lo, true); }
void MoveEnemyX_plus_12_14(Game& g, uint16_t x, uint16_t lo, uint16_t hi) { add32(g.Enemy_XPosition(x), g.Enemy_XSubPosition(x), hi, lo, false); }
void MoveEnemyY_minus_12_14(Game& g, uint16_t x, uint16_t lo, uint16_t hi) { add32(g.Enemy_YPosition(x), g.Enemy_YSubPosition(x), hi, lo, true); }
void MoveEnemyY_plus_12_14(Game& g, uint16_t x, uint16_t lo, uint16_t hi) { add32(g.Enemy_YPosition(x), g.Enemy_YSubPosition(x), hi, lo, false); }

}
