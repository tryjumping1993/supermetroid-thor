#pragma once
// Enemy species ports (banks $A2-$B3). One register_* function per bank file.
#include "thor/enemy_engine.hpp"
#include "thor/enemy_projectiles.hpp"
#include "reference_labels.hpp"

namespace thor::species {
using enemy::abs16;
using enemy::n16;
inline uint16_t L(uint32_t label) { return uint16_t(label & 0xFFFF); }   // 16-bit address within its bank

// Spawn an enemy projectile the way `JSL SpawnEnemyProjectileY_ParameterA_XGraphics` does and return
// the 65816 accumulator afterwards: the slot on success, the graphics word on failure (carry set).
inline uint16_t spawn_projectile(Game& g, uint32_t projectile_label, uint16_t a, uint16_t x) {
    const int slot = enemy::SpawnEnemyProjectileY_ParameterA_XGraphics(g, L(projectile_label), a, x);
    return slot >= 0 ? uint16_t(slot) : uint16_t(g.Enemy_palette(x) | g.Enemy_GFXOffset(x));
}

void register_a3(Game& g);
void register_species(Game& g);
}
