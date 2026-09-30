#pragma once
// Game: the WRAM mirror plus ROM access and the registry of natively ported routines.
// Ported code is written as members/free functions taking `Game&` and uses the
// generated disassembly names (see docs/PORTING_GUIDE.md).
#include "thor/content.hpp"
#include "thor/rom.hpp"
#include "thor/wram.hpp"
#include "ram_map.hpp"
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace thor {
class Game;
// Enemy / PLM / projectile "function pointers" in the original are 16-bit addresses inside a
// bank. They are registered here by full 24-bit label address (e.g. labels::MainAI_Zoomer).
// X is the 65816 X register: the byte offset of the current enemy (index * 0x40), as in $0E54.
using EnemyFunction = void (*)(Game& g, uint16_t x);
// Enemy instruction-list commands ($A0:C26A). Y points at the command's argument words (just
// after the command word). Return the new Y. Set `stop` for sleep/wait/delete style commands
// that end processing for this frame (the original pops the return address).
struct InstructionResult { uint16_t y; bool stop = false; };
using EnemyInstruction = InstructionResult (*)(Game& g, uint16_t x, uint16_t y);

class Game : public WramNames {
public:
    explicit Game(const Rom& rom) : rom_(rom) {}
    const Rom& rom() const { return rom_; }
    // ROM reads by 24-bit address (LoROM). Bank-relative helper for pointers in a bank.
    uint8_t rom_b(uint32_t address) const { return rom_.byte(address); }
    uint16_t rom_w(uint32_t address) const { return rom_.word(address); }
    uint32_t rom_l(uint32_t address) const { return rom_.pointer(address); }
    uint16_t rom_w(uint8_t bank, uint16_t address) const { return rom_.word((uint32_t(bank) << 16) | address); }

    // Registries. Missing entries are reported once and treated as no-ops so an unported
    // routine degrades gracefully instead of crashing the game.
    static uint32_t canonical(uint32_t address);
    void add_function(uint32_t address, EnemyFunction fn) { functions_[canonical(address)] = fn; }
    void add_instruction(uint32_t address, EnemyInstruction fn) { instructions_[canonical(address)] = fn; }
    bool call_function(uint32_t address, uint16_t x);
    bool call_instruction(uint32_t address, uint16_t x, uint16_t& y, bool& stop);
    const std::unordered_set<uint32_t>& missing() const { return missing_; }

    // Level data mirror ($7F0002 blocks, $7F6402 BTS). `sync_level_from_room` publishes a room
    // to WRAM; `sync_level_to_room` copies WRAM edits (from ported PLM/enemy code) back into the
    // room's blocks, redrawing changed blocks. Both are cheap enough to call every tick.
    void sync_level_from_room(const Room& room);
    bool sync_level_to_room(Room& room);

    // 65816 register stand-ins for routines that take/return A (e.g. Instruction_..._WithA).
    uint16_t A = 0;
    // Enemies whose graphics were queued this frame (the original's drawing queues).
    std::vector<uint16_t> enemies_drawn;

    // Audio hooks (no-ops until the SPC/S-DSP backend lands). IDs are the original queue values.
    void queue_sound(uint16_t id) { sound_queue_.push_back(id); if (sound_queue_.size() > 256) sound_queue_.erase(sound_queue_.begin()); }
    void queue_music(uint16_t id) { music_queue_.push_back(id); if (music_queue_.size() > 64) music_queue_.erase(music_queue_.begin()); }
    std::vector<uint16_t>& sound_queue() { return sound_queue_; }
    std::vector<uint16_t>& music_queue() { return music_queue_; }

    // Cross-area hooks so independently ported areas need not link against each other.
    // The PLM engine ($84) installs spawn_plm; block/projectile code calls it.
    std::function<bool(Game&, uint16_t plm_id, uint16_t block_x, uint16_t block_y)> spawn_plm;

    // Hooks for services implemented by other ported areas (Samus commands, the $86 projectile engine).
    std::function<void(Game&, uint16_t command)> samus_command;                       // $90:... Run_Samus_Command
    std::function<void(Game&, uint16_t projectile_id, uint16_t parameter)> spawn_enemy_projectile;  // $86:8027

    // Global registration entry points; each species/PLM file adds its routines here.
    static void register_all(Game& game);
private:
    void note_missing(uint32_t address);
    const Rom& rom_;
    std::unordered_map<uint32_t, EnemyFunction> functions_;
    std::unordered_map<uint32_t, EnemyInstruction> instructions_;
    std::unordered_set<uint32_t> missing_;
    std::vector<uint16_t> sound_queue_, music_queue_;
};
}
