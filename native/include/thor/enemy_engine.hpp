#pragma once
// Bank $A0 common enemy engine, ported routine by routine (see docs/PORTING_GUIDE.md).
// Names match the disassembly labels. `x` is always the enemy byte offset (index * 0x40).
#include "thor/game.hpp"

namespace thor::enemy {
// ---- Signed/unsigned helpers that mirror 65816 flag behavior -------------------------------
inline bool n16(uint32_t v) { return v & 0x8000; }
inline uint16_t abs16(uint16_t v) { return (v & 0x8000) ? uint16_t(0 - v) : v; }
// |d| - r1 borrows or is below r2: the common "X/Y overlap" test (SEC SBC / BCC / CMP / BCS).
inline bool within(uint16_t abs_delta, uint16_t r1, uint16_t r2) {
    return abs_delta < r1 || uint16_t(abs_delta - r1) < r2;
}

// ---- Enemy header ($A0 EnemyHeaders, 0x40 bytes) --------------------------------------------
namespace header {
inline constexpr uint32_t tile_data_size = 0, palette = 2, health = 4, damage = 6, width = 8, height = 0xA,
    bank = 0xC, hurt_ai_time = 0xD, cry = 0xE, boss_id = 0x10, init_ai = 0x12, parts = 0x14, main_ai = 0x18,
    grapple_ai = 0x1A, hurt_ai = 0x1C, frozen_ai = 0x1E, time_frozen_ai = 0x20, death_animation = 0x22,
    power_bomb_reaction = 0x28, variant_index = 0x2A, touch = 0x30, shot = 0x32, spritemap = 0x34,
    tile_data = 0x36, layer = 0x39, drops = 0x3A, vulnerabilities = 0x3C, name = 0x3E;
}
inline uint32_t header_address(uint16_t id) { return 0xA00000u | id; }

// ---- Vulnerability byte offsets ($B4 EnemyVulnerabilities) ---------------------------------
namespace vuln {
inline constexpr uint32_t power = 0, plasma_ice_wave_base = 3 /* +index per beam combo */, missile = 0xC,
    super_missile = 0xD, bomb = 0xE, power_bomb = 0xF, speed_booster = 0x10, shinespark = 0x11,
    screw_attack = 0x12, charged_beam = 0x13, pseudo_screw = 0x14;
}

// ---- Engine entry points (ROM label in the comment) -----------------------------------------
void register_engine(Game& g);                              // installs the common AI/instruction routines
void Load_Enemies(Game& g);                                 // $A0:8A1E
void ClearEnemyData_ProcessEnemySet(Game& g);               // $A0:8A6D
void Initialise_Enemies(Game& g);                           // $A0:8A9E
void Determine_Which_Enemies_to_Process(Game& g);           // $A0:8EB6
void Main_Enemy_Routine(Game& g);                           // $A0:8FD4
void Samus_Projectiles_Interaction_Handling(Game& g);       // $A0:9785
void EnemyProjectile_Samus_Collision_Handling(Game& g);     // $A0:9894
void Projectile_vs_Projectile_Collision_Handling(Game& g);  // $A0:996C
void Process_Enemy_PowerBomb_Interaction(Game& g);          // $A0:A306
void DecrementSamusHurtTimers_ClearActiveEnemyIndicesLists(Game& g);  // $A0:9169
void Handle_Room_Shaking(Game& g);                          // $A0:8687
void ProcessEnemyInstructions(Game& g);                     // $A0:C26A

// Spawning ($A0:9275/$92DB): `population` is the 24-bit address of a 16-byte population entry
// (the original reads it through the caller's data bank). Returns false when no slot is free;
// on success `new_index` receives the (first) enemy byte offset.
bool SpawnEnemy(Game& g, uint32_t population, uint16_t* new_index = nullptr);
void SpawnEnemy_AlwaysSucceed(Game& g, uint32_t population, uint16_t new_index, uint16_t parts);
void DeleteEnemyAndAnyConnectedEnemies(Game& g);            // $A0:922B
void EnemyDeath(Game& g, uint16_t animation);               // $A0:A3AF (uses EnemyIndex())
void RinkaDeath(Game& g, uint16_t animation);               // $A0:A410

// Damage
uint16_t Suit_Damage_Division(Game& g, uint16_t damage);    // $A0:A45E
void Deal_A_Damage_to_Samus(Game& g, uint16_t damage);      // $91:DF51
void Restore_A_Energy_ToSamus(Game& g, uint16_t amount);    // $91:DF12
void Restore_A_Missiles_ToSamus(Game& g, uint16_t amount);  // $91:DF80
void NormalEnemyTouchAI(Game& g);                           // $A0:A477
void NormalEnemyTouchAI_NoDeathCheck(Game& g);              // $A0:A4A1
void NormalEnemyPowerBombAI(Game& g);                       // $A0:A597
void NormalEnemyPowerBombAI_NoDeathCheck(Game& g);          // $A0:A5C1
void NormalEnemyShotAI(Game& g);                            // $A0:A63D
void NormalEnemyShotAI_NoDeathCheck(Game& g);               // $A0:A6B4
void NormalEnemyShotAI_NoDeathCheck_NoEnemyShotGraphic(Game& g);  // $A0:A6DE
void NormalEnemyFrozenAI(Game& g);                          // $A0:957E (A result in g.A)
void CreateADudShot(Game& g);                               // $A0:A8BC

// Samus/enemy geometry helpers ($A0:A8F0..AF0B)
// Distance is in {pixels, sub-pixels}; on a hit both are replaced by the distance to the collision.
bool Samus_vs_SolidEnemy_CollisionDetection(Game& g, uint16_t& pixels, uint16_t& sub, uint16_t& enemy);
bool CheckIfEnemyIsTouchingSamusFromBelow(Game& g, uint16_t x);
bool CheckIfEnemyIsTouchingSamus(Game& g, uint16_t x);
bool CheckIfEnemyCenterIsOnScreen(Game& g, uint16_t x);     // true = ON screen (original A = 0)
bool CheckIfEnemyCenterIsOverAPixelsOffScreen(Game& g, uint16_t x, uint16_t pixels);  // true = over (A=1)
bool CheckIfEnemyIsOnScreen(Game& g, uint16_t x);           // true = on screen
bool CheckIfEnemyIsHorizontallyOffScreen(Game& g, uint16_t x);  // true = off screen
uint16_t DetermineDirectionOfSamusFromEnemy(Game& g, uint16_t x);
int16_t Get_SamusY_minus_EnemyY(Game& g, uint16_t x);
int16_t Get_SamusX_minus_EnemyX(Game& g, uint16_t x);
bool IsSamusWithingAPixelRowsOfEnemy(Game& g, uint16_t x, uint16_t pixels);
bool IsSamusWithinAPixelColumnsOfEnemy(Game& g, uint16_t x, uint16_t pixels);
bool CheckIfXDistanceBetweenEnemyAndSamusIsAtLeastA(Game& g, uint16_t x, uint16_t a);

// Movement ($14.$12 distance parameters are passed explicitly: lo = sub-pixel word, hi = pixel word)
void MoveEnemyX_minus_12_14(Game& g, uint16_t x, uint16_t lo, uint16_t hi);
void MoveEnemyX_plus_12_14(Game& g, uint16_t x, uint16_t lo, uint16_t hi);
void MoveEnemyY_minus_12_14(Game& g, uint16_t x, uint16_t lo, uint16_t hi);
void MoveEnemyY_plus_12_14(Game& g, uint16_t x, uint16_t lo, uint16_t hi);
bool MoveEnemyRightBy_14_12_TreatSlopesAsWalls(Game& g, uint16_t x, uint16_t lo, uint16_t hi);  // true = collision
bool MoveEnemyRightBy_14_12_ProcessSlopes(Game& g, uint16_t x, uint16_t lo, uint16_t hi);
bool MoveEnemyRightBy_14_12_IgnoreSlopes(Game& g, uint16_t x, uint16_t lo, uint16_t hi);
bool MoveEnemyDownBy_14_12(Game& g, uint16_t x, uint16_t lo, uint16_t hi);
bool AlignEnemyYPositionWithNonSquareSlope(Game& g, uint16_t x);   // true = adjusted
// Read-only probes: true = collision; `adjusted_hi` is $14 after the call (reduced distance)
bool CheckForHorizontalSolidBlockCollision(Game& g, uint16_t x, uint16_t lo, uint16_t& hi);
bool CheckForVerticalSolidBlockCollision(Game& g, uint16_t x, uint16_t lo, uint16_t& hi);
bool CheckForVerticalSolidBlockCollision_SkreeMetaree(Game& g, uint16_t x, bool down, uint16_t lo, uint16_t& hi);  // $A0:BF8A
uint16_t CalculateAngleOfXYOffset(uint16_t dx, uint16_t dy);       // $A0:C0B1 (angle from -Y axis, 0..FF)
uint16_t CalculateAngleOfSamusFromEnemy(Game& g, uint16_t x);
uint16_t CalculateAngleOfSamusFromEnemyProjectile(Game& g, uint16_t projectile_x);
uint16_t CalculateAngleOfEnemyYFromEnemyX(Game& g, uint16_t target, uint16_t origin);
bool CalculateDistanceAndAngleOfSamusFromEnemy(Game& g, uint16_t enemy_x, uint16_t enemy_y, uint16_t& distance);
// 8-bit trig products ($A0:B0B2..B0DA): result is {integer part, fractional part} of sin/cos * radius
struct Fixed16 { uint16_t whole, fraction; };
Fixed16 EightBitSineMultiplication(Game& g, uint16_t angle, uint8_t radius);
Fixed16 EightBitCosineMultiplication(Game& g, uint16_t angle, uint8_t radius);
Fixed16 EightBitNegativeSineMultiplication(Game& g, uint16_t angle, uint8_t radius);
struct TrigResult { uint32_t cos_product, sin_product; };          // ($16.$18, $1A.$1C)
TrigResult Do_Some_Math_With_Sine_Cosine(Game& g, uint16_t angle, uint16_t magnitude);  // $A0:B643
void MoveEnemyAccordingToAngleAndXYSpeeds(Game& g, uint16_t x, uint16_t angle, uint16_t xspeed, uint16_t xsub, uint16_t yspeed, uint16_t ysub);
uint32_t Multiplication_32bit(uint16_t a, uint16_t b);             // $A0:B6FF
uint16_t GenerateRandomNumber(Game& g);                            // $80:8111
uint16_t NegateA(uint16_t v);                                      // |A| ($A0:B067)

// Boss/enemy death item drop routines ($A0:B8EE..BB6F)
void Spawn_Enemy_Drops(Game& g, uint16_t enemy_header, uint16_t xpos, uint16_t ypos);  // $A0:920E
void MiniKraidDeathItemDropRoutine(Game& g); void MetalNinjaPirateDeathItemDropRoutine(Game& g);
void MetroidDeathItemDropRoutine(Game& g); void RidleyDeathItemDropRoutine(Game& g);
void CrocomireDeathItemDropRoutine(Game& g); void PhantoonDeathItemDropRoutine(Game& g);
void BotwoonDeathItemDropRoutine(Game& g); void KraidDeathItemDropRoutine(Game& g);
void BombTorizoDeathItemDropRoutine(Game& g); void GoldenTorizoDeathItemDropRoutine(Game& g);
void SporeSpawnDeathItemDropRoutine(Game& g); void DraygonDeathItemDropRoutine(Game& g);
}
