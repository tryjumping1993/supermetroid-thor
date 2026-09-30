// Bank $A3 enemy species (Crateria/Brinstar-era set). Routine addresses are in the comments.
#include "thor/species.hpp"

namespace thor::species {
namespace {
using enemy::header_address;
namespace lb = labels;

// ---- Skree ($A3:C6A4..C841) ---------------------------------------------------------------------
InstructionResult Instruction_Skree_SetAttackReadyFlag(Game& g, uint16_t x, uint16_t y) { g.Skree_attackReadyFlag(x) = 1; return {y}; }
void SetSkreeInstListPointer(Game& g, uint16_t x) {   // $C7D5
    if (g.Skree_newInstListIndex(x) == g.Skree_instListIndex(x)) return;
    g.Skree_instListIndex(x) = g.Skree_newInstListIndex(x);
    g.Enemy_instList(x) = g.rom_w(0xA3, uint16_t(L(lb::InstListPointers_Skree) + g.Skree_newInstListIndex(x) * 2));
    g.Enemy_instTimer(x) = 1; g.Enemy_loopCounter(x) = 0;
}
void InitAI_Skree(Game& g, uint16_t) {
    const uint16_t x = g.EnemyIndex();
    g.Skree_newInstListIndex(x) = 0; g.Skree_instListIndex(x) = 0; g.Skree_attackReadyFlag(x) = 0;
    g.Enemy_instList(x) = L(lb::InstList_Skree_Idling);
    g.Skree_function(x) = L(lb::Function_Skree_Idling);
}
void MainAI_Skree(Game& g, uint16_t) {
    const uint16_t x = g.EnemyIndex();
    g.call_function(0xA30000u | g.Skree_function(x), x);
}
void Function_Skree_Idling(Game& g, uint16_t x) {
    if (abs16(uint16_t(g.Enemy_XPosition(x) - g.SamusXPosition())) >= 0x30) return;
    ++g.Skree_newInstListIndex(x);
    SetSkreeInstListPointer(g, x);
    g.Skree_function(x) = L(lb::Function_Skree_PrepareToLaunchAttack);
}
void Function_Skree_PrepareToLaunchAttack(Game& g, uint16_t x) {
    if (!g.Skree_attackReadyFlag(x)) return;
    g.Skree_attackReadyFlag(x) = 0;
    ++g.Skree_newInstListIndex(x);
    SetSkreeInstListPointer(g, x);
    g.Skree_function(x) = L(lb::Function_Skree_LaunchedAttack);
    g.queue_sound(0x5B);
}
void Function_Skree_LaunchedAttack(Game& g, uint16_t x) {
    g.Skree_burrowTimer(x) = 0x15;
    g.Enemy_properties(x) |= 0x0003;
    uint16_t distance = 6;   // $14.$12 = 6.0
    if (enemy::CheckForVerticalSolidBlockCollision_SkreeMetaree(g, x, true, 0, distance)) {
        g.Enemy_instTimer(x) = 1; g.Enemy_loopCounter(x) = 0;
        g.Skree_function(x) = L(lb::Function_Skree_Burrowing);
        g.queue_sound(0x5C);
        return;
    }
    g.Enemy_YPosition(x) = uint16_t(g.Enemy_YPosition(x) + 6);
    const int16_t step = int16_t(g.Enemy_XPosition(x) - g.SamusXPosition()) < 0 ? 1 : -1;
    g.Enemy_XPosition(x) = uint16_t(g.Enemy_XPosition(x) + step);
}
void SpawnSkreeParticles(Game& g, uint16_t x, uint16_t a) {
    a = spawn_projectile(g, lb::EnemyProjectile_SkreeParticles_DownRight, a, x);
    a = spawn_projectile(g, lb::EnemyProjectile_SkreeParticles_UpRight, a, x);
    a = spawn_projectile(g, lb::EnemyProjectile_SkreeParticles_DownLeft, a, x);
    spawn_projectile(g, lb::EnemyProjectile_SkreeParticles_UpLeft, a, x);
}
void Function_Skree_Burrowing(Game& g, uint16_t x) {
    if (--g.Skree_burrowTimer(x) == 0) {
        g.EnemySpawnData_VRAMTilesIndex(x) = uint16_t(g.Enemy_palette(x) | g.Enemy_GFXOffset(x));
        g.Enemy_palette(x) = 0x0A00; g.Enemy_GFXOffset(x) = 0;
        g.Enemy_properties(x) |= 0x0200;
        return;
    }
    if (g.Skree_burrowTimer(x) == 8) SpawnSkreeParticles(g, x, g.Skree_burrowTimer(x));
    ++g.Enemy_YPosition(x);
}
void EnemyShot_Skree(Game& g, uint16_t) {
    uint16_t x = g.EnemyIndex();
    enemy::NormalEnemyShotAI_NoDeathCheck_NoEnemyShotGraphic(g);
    if (g.Enemy_health(x)) return;
    SpawnSkreeParticles(g, x, g.Skree_burrowTimer(x));
    const bool super_missile = (g.SamusProjectile_Types(uint16_t(g.CollisionIndex() * 2)) & 0x0F00) == 0x0200;
    enemy::EnemyDeath(g, super_missile ? 2 : 0);
}
}

void register_a3(Game& g) {
    g.add_instruction(lb::Instruction_Skree_SetAttackReadyFlag, Instruction_Skree_SetAttackReadyFlag);
    g.add_function(lb::InitAI_Skree, InitAI_Skree);
    g.add_function(lb::MainAI_Skree, MainAI_Skree);
    g.add_function(lb::Function_Skree_Idling, Function_Skree_Idling);
    g.add_function(lb::Function_Skree_PrepareToLaunchAttack, Function_Skree_PrepareToLaunchAttack);
    g.add_function(lb::Function_Skree_LaunchedAttack, Function_Skree_LaunchedAttack);
    g.add_function(lb::Function_Skree_Burrowing, Function_Skree_Burrowing);
    g.add_function(lb::EnemyShot_Skree, EnemyShot_Skree);
}
void register_species(Game& g) { register_a3(g); }
}
