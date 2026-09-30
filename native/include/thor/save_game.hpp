#pragma once
#include "thor/content.hpp"
#include "thor/sram.hpp"

namespace thor {
Progression restore_progression(const Rom& rom, const Sram& sram, unsigned slot);
void save_progression(const Rom& rom, Sram& sram, unsigned slot, const Progression& progression, unsigned area, unsigned station);
}
