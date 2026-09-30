#include "thor/game.hpp"
#include <cstdlib>
#include <cstring>

namespace thor {
// The enemy code banks ($A0-$B3) each carry identical copies of the common routines at
// $8000-$8186; they resolve to the single $A0 implementation.
uint32_t Game::canonical(uint32_t address) {
    const uint32_t bank = address >> 16, offset = address & 0xFFFF;
    if (bank >= 0xA0 && bank <= 0xB3 && offset >= 0x8000 && offset < 0x8187) return 0xA00000u | offset;
    return address;
}
void Game::note_missing(uint32_t address) {
    if (missing_.insert(address).second && std::getenv("THOR_LOG_MISSING"))
        std::fprintf(stderr, "unported routine $%06X\n", address);
}
bool Game::call_function(uint32_t address, uint16_t x) {
    const auto it = functions_.find(canonical(address));
    if (it == functions_.end()) { note_missing(address); return false; }
    it->second(*this, x); return true;
}
bool Game::call_instruction(uint32_t address, uint16_t x, uint16_t& y, bool& stop) {
    const auto it = instructions_.find(canonical(address));
    if (it == instructions_.end()) { note_missing(address); stop = false; return false; }
    const auto result = it->second(*this, x, y);
    y = result.y; stop = result.stop; return true;
}
void Game::sync_level_from_room(const Room& room) {
    const size_t count = std::min<size_t>(room.blocks.size(), 0x3200);
    RoomWidthBlocks() = uint16_t(room.width / 16); RoomHeightBlocks() = uint16_t(room.height / 16);
    RoomWidthScrolls() = uint16_t(room.width / 256);
    uint8_t* level = data() + 0x10002;
    std::memcpy(level, room.blocks.data(), count * 2);
    std::memcpy(data() + 0x16402, room.bts.data(), std::min<size_t>(room.bts.size(), 0x3200));
    word_at(0x10000) = uint16_t(count * 2);
}
bool Game::sync_level_to_room(Room& room) {
    const size_t count = std::min<size_t>(room.blocks.size(), 0x3200);
    bool changed = false;
    const uint8_t* level = data() + 0x10002;
    const uint8_t* bts = data() + 0x16402;
    for (size_t i = 0; i < count; ++i) {
        uint16_t block; std::memcpy(&block, level + i * 2, 2);
        if (block != room.blocks[i] || bts[i] != room.bts[i]) {
            room.blocks[i] = block; room.bts[i] = bts[i]; redraw_block(room, i); changed = true;
        }
    }
    if (changed) ++room.visual_revision;
    return changed;
}
}
