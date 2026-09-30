// Bank $86 enemy projectile engine core: spawn, handler, common instructions, enemy death
// explosions, item pickups and the random drop routine. Species projectiles register elsewhere.
#include "thor/enemy_projectiles.hpp"
#include "thor/enemy_engine.hpp"
#include "reference_labels.hpp"

namespace thor::enemy {
namespace {
constexpr uint16_t kNone = 0xFFFF;
uint16_t rom86(Game& g, uint16_t address) { return g.rom_w(0x86, address); }
uint16_t lo(uint32_t label) { return uint16_t(label & 0xFFFF); }

uint16_t EnemyHeaderPointer(Game& g, uint16_t slot) { return g.word_at(Game::EnemyProjectileData_EnemyHeaderPointer_address + slot); }
}

// ---- $86:8027 / $86:8097 spawn ------------------------------------------------------------------
static int spawn(Game& g, uint16_t projectile_id, uint16_t parameter, uint16_t graphics_indices) {
    g.EnemyProjectile_InitParam0() = parameter;
    for (int16_t y = 0x22; y >= 0; y -= 2) {
        const uint16_t slot = uint16_t(y);
        if (g.EnemyProjectile_ID(slot)) continue;
        g.EnemyProjectile_GraphicsIndices(slot) = graphics_indices;
        g.EnemyProjectile_ID(slot) = projectile_id;
        g.EnemyProjectile_PreInstructions(slot) = rom86(g, uint16_t(projectile_id + 2));
        g.EnemyProjectile_InstListPointers(slot) = rom86(g, uint16_t(projectile_id + 4));
        g.EnemyProjectile_Radii(slot) = rom86(g, uint16_t(projectile_id + 6));
        g.EnemyProjectile_Properties(slot) = rom86(g, uint16_t(projectile_id + 8));
        g.EnemyProjectile_InstructionTimers(slot) = 1;
        g.EnemyProjectile_SpritemapPointers(slot) = uint16_t(labels::EnemyProjSpritemaps_Blank_Default & 0xFFFF);
        g.EnemyProjectile_Var0(slot) = 0; g.EnemyProjectile_Var1(slot) = 0; g.EnemyProjectile_Timers(slot) = 0;
        g.EnemyProjectile_XSubPositions(slot) = 0; g.EnemyProjectile_YSubPositions(slot) = 0;
        g.EnemyProjectile_CollidedProjectileType(slot) = 0;
        g.call_function(0x860000u | rom86(g, projectile_id), slot);   // initialisation AI (Y = slot)
        return slot;
    }
    return -1;
}
int SpawnEnemyProjectileY_ParameterA_XGraphics(Game& g, uint16_t projectile_id, uint16_t parameter, uint16_t enemy_x) {
    return spawn(g, projectile_id, parameter, uint16_t(g.Enemy_palette(enemy_x) | g.Enemy_GFXOffset(enemy_x)));
}
int SpawnEnemyProjectileY_ParameterA_RoomGraphics(Game& g, uint16_t projectile_id, uint16_t parameter) {
    return spawn(g, projectile_id, parameter, 0);
}

// ---- $86:8104 handler -------------------------------------------------------------------------------
static void Process_Enemy_Projectile(Game& g, uint16_t x) {
    g.call_function(0x860000u | g.EnemyProjectile_PreInstructions(x), x);
    if (--g.EnemyProjectile_InstructionTimers(x) != 0) return;
    uint16_t y = g.EnemyProjectile_InstListPointers(x);
    for (unsigned guard = 0; guard < 1024; ++guard) {
        const uint16_t value = rom86(g, y);
        if (value < 0x8000) {
            g.EnemyProjectile_InstructionTimers(x) = value;
            g.EnemyProjectile_SpritemapPointers(x) = rom86(g, uint16_t(y + 2));
            g.EnemyProjectile_InstListPointers(x) = uint16_t(y + 4);
            return;
        }
        y += 2;
        bool stop = false;
        if (!g.call_instruction(0x860000u | value, x, y, stop)) {   // unported command: park on it
            g.EnemyProjectile_InstListPointers(x) = uint16_t(y - 2); g.EnemyProjectile_InstructionTimers(x) = 1; return;
        }
        if (stop) return;
    }
}
void Enemy_Projectile_Handler(Game& g) {
    if (!(g.EnemyProjectile_Enable() & 0x8000)) return;
    for (int16_t x = 0x22; x >= 0; x -= 2) {
        g.EnemyProjectile_Index() = uint16_t(x);
        if (g.EnemyProjectile_ID(uint16_t(x))) Process_Enemy_Projectile(g, uint16_t(x));
    }
}
void Clear_Enemy_Projectiles(Game& g) {
    for (int16_t x = 0x22; x >= 0; x -= 2) g.EnemyProjectile_ID(uint16_t(x)) = 0;
}

// ---- Pickups ($86:EEAF..F264) ----------------------------------------------------------------------------
uint16_t Random_Drop_Routine(Game& g, uint16_t header) {
    const uint16_t drops = g.rom_w(header_address(header) + header::drops);
    static constexpr uint8_t kDropType[6] = {1, 2, 4, 6, 5, 3};
    if (!drops) return 6;
    const uint32_t chance = 0xB40000u | drops;
    uint16_t target;
    do target = GenerateRandomNumber(g) & 0xFF; while (!target);
    uint8_t minor_pool = 0, major_remaining = 0xFF, pooled_flags = 0;
    const uint16_t energy = uint16_t(g.Energy() + g.ReserveEnergy());
    if (energy < 0x1E) g.CriticalEnergyFlag() = 1;
    else if (energy >= 0x32) g.CriticalEnergyFlag() = 0;
    auto chance_at = [&](unsigned i) { return g.rom_b(chance + i); };
    if (g.CriticalEnergyFlag()) {
        minor_pool = uint8_t(chance_at(0) + chance_at(1)); pooled_flags = 3;
    } else {
        minor_pool = chance_at(3); pooled_flags = 8;
        if (g.Energy() != g.MaxEnergy() || g.ReserveEnergy() != g.MaxReserveEnergy()) {
            minor_pool = uint8_t(minor_pool + chance_at(0) + chance_at(1)); pooled_flags |= 3;
        }
        if (g.Missiles() != g.MaxMissiles()) { minor_pool = uint8_t(minor_pool + chance_at(2)); pooled_flags |= 4; }
        if (g.SuperMissiles() != g.MaxSuperMissiles()) { major_remaining = uint8_t(major_remaining - chance_at(4)); pooled_flags |= 0x10; }
        if (g.PowerBombs() != g.MaxPowerBombs()) { major_remaining = uint8_t(major_remaining - chance_at(5)); pooled_flags |= 0x20; }
    }
    uint16_t accumulator = 0; unsigned y = 0;
    if (minor_pool) {
        for (; y < 4; ++y) {
            const bool present = pooled_flags & 1; pooled_flags >>= 1;
            if (!present) continue;
            const uint16_t product = uint16_t(major_remaining * chance_at(y));
            const uint16_t q = uint16_t(product / minor_pool);
            if (uint16_t(accumulator + q) >= target) return kDropType[y];
            accumulator = uint16_t(accumulator + q);
        }
    } else { pooled_flags >>= 4; y = 4; }
    for (; y < 6; ++y) {
        const bool present = pooled_flags & 1; pooled_flags >>= 1;
        if (!present) continue;
        if (uint16_t(chance_at(y) + accumulator) >= target) return kDropType[y];
        accumulator = uint16_t(accumulator + chance_at(y));
    }
    return kDropType[3];
}
void Respawn_Enemy(Game& g, uint16_t x) {
    const uint16_t population = uint16_t(g.EnemyPopulationPointer() + (x >> 2));
    const uint32_t p = 0xA10000u | population;
    g.EnemyIndex() = x;
    g.Enemy_ID(x) = g.rom_w(p); g.Enemy_XPosition(x) = g.rom_w(p + 2); g.Enemy_YPosition(x) = g.rom_w(p + 4);
    g.Enemy_instList(x) = g.rom_w(p + 6); g.Enemy_properties(x) = g.rom_w(p + 8); g.Enemy_properties2(x) = g.rom_w(p + 10);
    g.Enemy_init0(x) = g.rom_w(p + 12); g.Enemy_init1(x) = g.rom_w(p + 14);
    g.Enemy_palette(x) = g.EnemySpawnData_paletteIndex(x); g.Enemy_GFXOffset(x) = g.EnemySpawnData_VRAMTilesIndex(x);
    g.Enemy_freezeTimer(x) = 0; g.Enemy_flashTimer(x) = 0; g.Enemy_invincibilityTimer(x) = 0;
    g.Enemy_loopCounter(x) = 0; g.Enemy_frameCounter(x) = 0;
    g.Enemy_var0(x) = 0; g.Enemy_var1(x) = 0; g.Enemy_var2(x) = 0; g.Enemy_var3(x) = 0; g.Enemy_var4(x) = 0; g.Enemy_var5(x) = 0;
    g.Enemy_instTimer(x) = 1;
    const uint32_t header_at = header_address(g.Enemy_ID(x));
    g.Enemy_XHitboxRadius(x) = g.rom_w(header_at + header::width); g.Enemy_YHitboxRadius(x) = g.rom_w(header_at + header::height);
    g.Enemy_health(x) = g.rom_w(header_at + header::health); g.Enemy_layer(x) = g.rom_b(header_at + header::layer);
    g.Enemy_bank(x) = g.rom_w(header_at + header::bank);
    g.call_function((uint32_t(uint8_t(g.Enemy_bank(x))) << 16) | g.rom_w(header_at + header::init_ai), x);
}

namespace {
InstructionResult Inst_Delete(Game& g, uint16_t x, uint16_t y) { g.EnemyProjectile_ID(x) = 0; return {y, true}; }
InstructionResult Inst_Sleep(Game& g, uint16_t x, uint16_t y) { g.EnemyProjectile_InstListPointers(x) = uint16_t(y - 2); return {y, true}; }
InstructionResult Inst_PreInstructionInY(Game& g, uint16_t x, uint16_t y) { g.EnemyProjectile_PreInstructions(x) = rom86(g, y); return {uint16_t(y + 2)}; }
InstructionResult Inst_ClearPreInstruction(Game& g, uint16_t x, uint16_t y) { g.EnemyProjectile_PreInstructions(x) = uint16_t(0x8170); return {y}; }
InstructionResult Inst_GotoY(Game& g, uint16_t, uint16_t y) { return {rom86(g, y)}; }
InstructionResult Inst_GotoY_Y(Game& g, uint16_t, uint16_t y) { return {uint16_t(y + int8_t(g.rom_b(0x860000u | y)))}; }
InstructionResult Inst_DecrementTimer_GotoYIfNonZero(Game& g, uint16_t x, uint16_t y) {
    if (--g.EnemyProjectile_Timers(x) != 0) return {rom86(g, y)};
    return {uint16_t(y + 2)};
}
InstructionResult Inst_TimerInY(Game& g, uint16_t x, uint16_t y) { g.EnemyProjectile_Timers(x) = rom86(g, y); return {uint16_t(y + 2)}; }
InstructionResult Inst_Nop(Game&, uint16_t, uint16_t y) { return {y}; }
InstructionResult Inst_PropertiesOr(Game& g, uint16_t x, uint16_t y) { g.EnemyProjectile_Properties(x) |= rom86(g, y); return {uint16_t(y + 2)}; }
InstructionResult Inst_PropertiesAnd(Game& g, uint16_t x, uint16_t y) { g.EnemyProjectile_Properties(x) &= rom86(g, y); return {uint16_t(y + 2)}; }
InstructionResult Inst_DisableCollisionWithSamusProjectiles(Game& g, uint16_t x, uint16_t y) { g.EnemyProjectile_Properties(x) &= 0x7FFF; return {y}; }
InstructionResult Inst_DisableCollisionWithSamus(Game& g, uint16_t x, uint16_t y) { g.EnemyProjectile_Properties(x) |= 0x2000; return {y}; }

InstructionResult Inst_SpawnSpriteObject(Game& g, uint16_t, uint16_t y) {   // graphics-only effect: consume the RNG like the original
    GenerateRandomNumber(g); return {uint16_t(y + 2)};
}
InstructionResult Inst_QueueEnemyKilledSound(Game& g, uint16_t, uint16_t y) { g.queue_sound(0x09); return {y}; }
InstructionResult Inst_QueueSmallExplosionSound(Game& g, uint16_t, uint16_t y) { g.queue_sound(0x24); return {y}; }
InstructionResult Inst_QueueContactKilledSound(Game& g, uint16_t, uint16_t y) { g.queue_sound(0x0B); return {y}; }

void set_nothing(Game& g, uint16_t x) {
    g.EnemyProjectile_InstructionTimers(x) = 1; g.EnemyProjectile_Properties(x) = 0x3000;
    g.EnemyProjectile_PreInstructions(x) = uint16_t(labels::RTS_86EFDF & 0xFFFF);
    g.EnemyProjectile_InstListPointers(x) = uint16_t(labels::InstList_EnemyProjectile_Pickup_HandleRespawningEnemy & 0xFFFF);
}
InstructionResult Inst_BecomePickup(Game& g, uint16_t x, uint16_t) {
    const uint16_t drop = Random_Drop_Routine(g, EnemyHeaderPointer(g, x));
    // (original bug) the BEQ tests the index register restored by the routine: slot 0 never drops.
    if (x == 0 || drop >= 6) { set_nothing(g, x); return {g.EnemyProjectile_InstListPointers(x)}; }
    static const uint32_t kLists[6] = {0, labels::InstList_EnemyProjectile_Pickup_SmallEnergy, labels::InstList_EnemyProjectile_Pickup_BigEnergy,
        labels::InstList_EnemyProjectile_Pickup_PowerBombs, labels::InstList_EnemyProjectile_Pickup_Missiles, labels::InstList_EnemyProjectile_Pickup_SuperMissiles};
    g.EnemyProjectile_Var0(x) = uint16_t(drop * 2);
    g.EnemyProjectile_InstListPointers(x) = uint16_t(kLists[drop] & 0xFFFF);
    g.EnemyProjectile_InstructionTimers(x) = 1; g.EnemyProjectile_Var1(x) = 0x0190;
    g.EnemyProjectile_PreInstructions(x) = uint16_t(labels::PreInstruction_EnemyProjectile_Pickup & 0xFFFF);
    g.EnemyProjectile_Properties(x) &= 0xBFFF;
    return {g.EnemyProjectile_InstListPointers(x)};
}
InstructionResult Inst_HandleRespawningEnemy(Game& g, uint16_t x, uint16_t y) {
    const uint16_t killed = g.word_at(Game::EnemyProjectileData_KilledEnemyIndex_address + x);
    if (killed != kNone && (killed & 0x8000)) Respawn_Enemy(g, uint16_t(killed & 0x7FFF));
    return {y};
}

void InitAI_DeathExplosion(Game& g, uint16_t slot) {
    const uint16_t e = g.EnemyIndex();
    g.EnemyProjectile_XPositions(slot) = g.Enemy_XPosition(e); g.EnemyProjectile_YPositions(slot) = g.Enemy_YPosition(e);
    g.word_at(Game::EnemyProjectileData_KilledEnemyIndex_address + slot) = (g.Enemy_properties(e) & 0x4000) ? uint16_t(e | 0x8000) : e;
    g.word_at(Game::EnemyProjectileData_EnemyHeaderPointer_address + slot) = g.Enemy_ID(e);
    g.EnemyProjectile_GraphicsIndices(slot) = 0;
    static const uint32_t kLists[5] = {labels::InstList_EnemyProjectile_EnemyDeathExplosion_SmallExplosion,
        labels::InstList_EnemyProj_EnemyDeathExplosion_KilledBySamusContact, labels::InstList_EnemyProjectile_EnemyDeathExplosion_NormalExplosion,
        labels::InstList_EnemyProj_EnemyDeathExplosion_MiniKraidExplosion_0, labels::InstList_EnemyProjectile_EnemyDeathExplosion_BigExplosion_0};
    const uint16_t kind = g.EnemyProjectile_InitParam0();
    g.EnemyProjectile_InstListPointers(slot) = uint16_t(kLists[kind < 5 ? kind : 0] & 0xFFFF);
    g.EnemyProjectile_InstructionTimers(slot) = 1;
}
void InitAI_Pickup(Game& g, uint16_t slot) {
    g.EnemyProjectile_XPositions(slot) = g.word_at(0x12); g.EnemyProjectile_YPositions(slot) = g.word_at(0x14);
    g.EnemyProjectile_GraphicsIndices(slot) = 0;
    g.word_at(Game::EnemyProjectileData_EnemyHeaderPointer_address + slot) = g.Temp_EnemyHeaderPointer();
    const uint16_t drop = Random_Drop_Routine(g, g.Temp_EnemyHeaderPointer());
    if (drop == 0 || drop >= 6) {
        g.EnemyProjectile_InstListPointers(slot) = uint16_t(labels::InstList_EnemyProjectile_Pickup_HandleRespawningEnemy & 0xFFFF);
        g.EnemyProjectile_InstructionTimers(slot) = 1; g.EnemyProjectile_Properties(slot) = 0x3000;
        g.EnemyProjectile_PreInstructions(slot) = uint16_t(labels::RTS_86EFDF & 0xFFFF); return;
    }
    static const uint32_t kLists[6] = {0, labels::InstList_EnemyProjectile_Pickup_SmallEnergy, labels::InstList_EnemyProjectile_Pickup_BigEnergy,
        labels::InstList_EnemyProjectile_Pickup_PowerBombs, labels::InstList_EnemyProjectile_Pickup_Missiles, labels::InstList_EnemyProjectile_Pickup_SuperMissiles};
    g.EnemyProjectile_Var0(slot) = uint16_t(drop * 2);
    g.EnemyProjectile_InstListPointers(slot) = uint16_t(kLists[drop] & 0xFFFF);
    g.EnemyProjectile_InstructionTimers(slot) = 1; g.EnemyProjectile_Var1(slot) = 0x0190;
    g.word_at(Game::EnemyProjectileData_KilledEnemyIndex_address + slot) = kNone;
}
void apply_pickup(Game& g, uint16_t kind) {
    switch (kind) {
    case 2: Restore_A_Energy_ToSamus(g, 5); g.queue_sound(1); break;      // small health
    case 4: Restore_A_Energy_ToSamus(g, 20); g.queue_sound(2); break;     // big health
    case 6: g.PowerBombs() = uint16_t(g.PowerBombs() + 1); if (int16_t(g.PowerBombs() - g.MaxPowerBombs()) > 0) g.PowerBombs() = g.MaxPowerBombs(); g.queue_sound(5); break;
    case 8: Restore_A_Missiles_ToSamus(g, 2); g.queue_sound(3); break;
    case 10: g.SuperMissiles() = uint16_t(g.SuperMissiles() + 1); if (int16_t(g.SuperMissiles() - g.MaxSuperMissiles()) > 0) g.SuperMissiles() = g.MaxSuperMissiles(); g.queue_sound(4); break;
    default: break;
    }
}
void PreInstruction_Pickup(Game& g, uint16_t x) {
    if (--g.EnemyProjectile_Var1(x) == 0) { set_nothing(g, x); return; }
    // Grapple pickup ($90 command $0D) is not ported yet; only direct contact collects.
    const uint16_t xr = g.EnemyProjectile_Radii(x) & 0xFF, yr = (g.EnemyProjectile_Radii(x) >> 8) & 0xFF;
    if (!within(abs16(uint16_t(g.SamusXPosition() - g.EnemyProjectile_XPositions(x))), g.SamusXRadius(), xr)) return;
    if (!within(abs16(uint16_t(g.SamusYPosition() - g.EnemyProjectile_YPositions(x))), g.SamusYRadius(), yr)) return;
    apply_pickup(g, g.EnemyProjectile_Var0(x));
    set_nothing(g, x);
}
}

void register_projectiles86(Game& g) {
    using namespace labels;
    auto ins = [&](uint32_t address, InstructionResult (*f)(Game&, uint16_t, uint16_t)) { g.add_instruction(address, f); };
    ins(labels::Instruction_EnemyProjectile_Delete, Inst_Delete);
    ins(labels::Instruction_EnemyProjectile_Sleep, Inst_Sleep);
    ins(labels::Instruction_EnemyProjectile_PreInstructionInY, Inst_PreInstructionInY);
    ins(labels::Instruction_EnemyProjectile_ClearPreInstruction, Inst_ClearPreInstruction);
    ins(labels::Instruction_EnemyProjectile_GotoY, Inst_GotoY);
    ins(labels::Instruction_EnemyProjectile_GotoY_Y, Inst_GotoY_Y);
    ins(labels::Instruction_EnemyProjectile_DecrementTimer_GotoYIfNonZero, Inst_DecrementTimer_GotoYIfNonZero);
    ins(labels::Instruction_EnemyProjectile_TimerInY, Inst_TimerInY);
    ins(labels::RTS_8681DE, Inst_Nop);
    ins(labels::Instruction_EnemyProjectile_Properties_OrY, Inst_PropertiesOr);
    ins(labels::Instruction_EnemyProjectile_Properties_AndY, Inst_PropertiesAnd);
    ins(labels::Instruction_EnemyProjectile_DisableCollisionWIthSamusProj, Inst_DisableCollisionWithSamusProjectiles);
    ins(labels::Instruction_EnemyProjectile_DisableCollisionWithSamus, Inst_DisableCollisionWithSamus);
    ins(labels::Instruction_EnemyProj_EnemyDeathExpl_SpawnSpriteObjectInY_10, Inst_SpawnSpriteObject);
    ins(labels::Instruction_EnemyProj_EnemyDeathExpl_SpawnSpriteObjectInY_20, Inst_SpawnSpriteObject);
    ins(labels::Instruction_EnemyProj_EnemyDeathExpl_QueueEnemyKilledSoundFX, Inst_QueueEnemyKilledSound);
    ins(labels::Instruction_EnemyProj_EDeathExplo_QueueSmallExplosionSoundFX, Inst_QueueSmallExplosionSound);
    ins(labels::Instruction_EnemyProj_EDeathExplo_QueueContactKilledSoundFX, Inst_QueueContactKilledSound);
    ins(labels::Instruction_EnemyProjectile_EnemyDeathExplosion_BecomePickup, Inst_BecomePickup);
    ins(labels::Instruction_EnemyProjectile_Pickup_HandleRespawningEnemy, Inst_HandleRespawningEnemy);
    g.add_function(labels::InitAI_EnemyProjectile_EnemyDeathExplosion, InitAI_DeathExplosion);
    g.add_function(labels::InitAI_EnemyProjectile_Pickup, InitAI_Pickup);
    g.add_function(labels::PreInstruction_EnemyProjectile_Pickup, PreInstruction_Pickup);
    g.add_function(labels::RTS_86EFDF, [](Game&, uint16_t) {});
    g.add_function(0x868170, [](Game&, uint16_t) {});
    g.spawn_enemy_projectile = [](Game& game, uint16_t id, uint16_t parameter) {
        SpawnEnemyProjectileY_ParameterA_XGraphics(game, id, parameter, game.EnemyIndex());
    };
}
}
