// Bank $A0 boss death item drop routines ($A0:B8EE..BB6F) and Spawn_Enemy_Drops ($A0:920E).
#include "thor/enemy_engine.hpp"
#include "reference_labels.hpp"

namespace thor::enemy {
void Spawn_Enemy_Drops(Game& g, uint16_t enemy_header, uint16_t xpos, uint16_t ypos) {
    g.Temp_EnemyHeaderPointer() = enemy_header;
    g.word_at(0x12) = xpos; g.word_at(0x14) = ypos;
    if (g.spawn_enemy_projectile)
        g.spawn_enemy_projectile(g, uint16_t(labels::EnemyProjectile_EnemyDeathPickup & 0xFFFF), g.Temp_EnemyProjectileInitParam());
}

namespace {
void drops(Game& g, uint16_t count, uint32_t header) {
    g.NumberOfDrops() = count;
    do {
        const uint16_t r = GenerateRandomNumber(g);
        const uint16_t x = uint16_t((r & 0x1F) - 0x10 + g.word_at(Game::EnemyProjectileData_SpecialDeathItemDropXOriginPosition_address));
        const uint16_t y = uint16_t(((g.RandomNumberSeed() & 0x1F00) >> 8) - 0x10 + g.word_at(Game::EnemyProjectileData_SpecialDeathItemDropYOriginPosition_address));
        Spawn_Enemy_Drops(g, uint16_t(header & 0xFFFF), x, y);
    } while (--g.NumberOfDrops() != 0);
}
}
void MiniKraidDeathItemDropRoutine(Game& g) { drops(g, 4, labels::EnemyHeaders_MiniKraid); }
void MetalNinjaPirateDeathItemDropRoutine(Game& g) { drops(g, 5, labels::EnemyHeaders_PirateGoldNinja); }
void MetroidDeathItemDropRoutine(Game& g) { drops(g, 5, labels::EnemyHeaders_Metroid); }
void RidleyDeathItemDropRoutine(Game& g) { drops(g, 16, labels::EnemyHeaders_Ridley); }
void CrocomireDeathItemDropRoutine(Game& g) { drops(g, 16, labels::EnemyHeaders_Crocomire); }
void PhantoonDeathItemDropRoutine(Game& g) { drops(g, 16, labels::EnemyHeaders_PhantoonBody); }
void BotwoonDeathItemDropRoutine(Game& g) { drops(g, 16, labels::EnemyHeaders_Botwoon); }
void KraidDeathItemDropRoutine(Game& g) { drops(g, 16, labels::EnemyHeaders_Kraid); }
void BombTorizoDeathItemDropRoutine(Game& g) { drops(g, 16, labels::EnemyHeaders_BombTorizo); }
void GoldenTorizoDeathItemDropRoutine(Game& g) { drops(g, 16, labels::EnemyHeaders_BombTorizo); }
void SporeSpawnDeathItemDropRoutine(Game& g) { drops(g, 16, labels::EnemyHeaders_SporeSpawn); }
void DraygonDeathItemDropRoutine(Game& g) { drops(g, 16, labels::EnemyHeaders_DraygonBody); }
}
