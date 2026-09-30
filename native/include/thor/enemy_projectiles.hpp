#pragma once
// Bank $86 enemy projectile engine (core). See native/src/enemy/projectiles86.cpp.
#include "thor/game.hpp"

namespace thor::enemy {
void register_projectiles86(Game& g);
// $86:8027 / $86:8097. Return the projectile slot (byte offset) or -1 when none is free.
int SpawnEnemyProjectileY_ParameterA_XGraphics(Game& g, uint16_t projectile_id, uint16_t parameter, uint16_t enemy_x);
int SpawnEnemyProjectileY_ParameterA_RoomGraphics(Game& g, uint16_t projectile_id, uint16_t parameter);
void Enemy_Projectile_Handler(Game& g);   // $86:8104
void Clear_Enemy_Projectiles(Game& g);    // $86:8016
uint16_t Random_Drop_Routine(Game& g, uint16_t enemy_header);   // $86:F106 (1 small hp, 2 big hp, 3 PB, 4 missile, 5 super, 6 none)
void Respawn_Enemy(Game& g, uint16_t enemy_x);                  // $86:F264
}
