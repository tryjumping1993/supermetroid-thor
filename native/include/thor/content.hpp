#pragma once
#include "thor/rom.hpp"
#include <array>
#include <bitset>
#include <string>
#include <vector>

namespace thor {
struct Image { int width = 0, height = 0; std::vector<uint32_t> pixels; };
struct Progression {
    std::bitset<256> events;
    std::array<uint8_t, 8> bosses{};
    uint16_t collected_items = 0, max_missiles = 0, max_power_bombs = 0;
    uint16_t entering_door = 0;
};
struct Door {
    uint32_t address = 0, destination = 0;
    size_t destination_index = 0;
    uint8_t properties = 0, direction = 0, cap_x = 0, cap_y = 0, screen_x = 0, screen_y = 0;
    uint16_t transition_speed = 0, custom_asm = 0;
};
struct Room {
    uint32_t header = 0, state = 0;
    std::string name;
    int area = 0, map_x = 0, map_y = 0, width = 0, height = 0;
    std::vector<uint16_t> blocks;
    std::vector<uint8_t> bts;
    std::vector<Door> doors;
    Image foreground;
    uint16_t block(int x, int y) const;
};
// All decoding consumes user ROM bytes; no artwork is compiled into the APK.
uint32_t select_room_state(const Rom& rom, uint32_t header, const Progression& progression);
Room load_room(const Rom& rom, size_t index, const Progression& progression = {});
// Door lists are not terminated; counts come from the pinned source index.
std::vector<Door> load_doors(const Rom& rom, size_t index);
Image draw_samus(const Rom& rom, int pose, int frame);
std::array<uint32_t, 256> decode_palette(std::span<const uint8_t> bytes);
uint8_t tile_pixel(std::span<const uint8_t> tiles, int tile, int x, int y);
// Static block geometry only. PLM reactions, damage and directional Samus
// collision responses are separate gameplay systems still to be translated.
bool room_solid_pixel(const Room& room, const Rom& rom, int x, int y);
}
