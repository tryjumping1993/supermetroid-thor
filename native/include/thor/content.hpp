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
    uint16_t equipped_items = 0, health = 99, max_health = 99, missiles = 0;
    std::bitset<512> items, opened_doors, destroyed_chozo;
    std::array<uint8_t, 8 * 256> map{};
};
struct RoomObject { uint16_t kind = 0, argument = 0; uint8_t x = 0, y = 0; };
struct EnemySpawn { uint16_t kind = 0, x = 0, y = 0, instruction = 0, properties = 0, extra = 0, parameter1 = 0, parameter2 = 0; };
struct LoadStation { size_t room_index = 0; uint16_t door = 0; int camera_x = 0, camera_y = 0, x = 0, y = 0; };
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
    std::vector<RoomObject> objects;
    std::vector<EnemySpawn> enemies;
    std::vector<uint8_t> scrolls;
    uint8_t layer2_x = 0, layer2_y = 0, enemy_quota = 0;
    Image foreground;
    Image foreground_low, foreground_high, background;
    std::vector<uint8_t> tiletable, tiles;
    std::array<uint32_t, 256> colors{};
    uint32_t visual_revision = 0;
    uint16_t block(int x, int y) const;
};
// All decoding consumes user ROM bytes; no artwork is compiled into the APK.
uint32_t select_room_state(const Rom& rom, uint32_t header, const Progression& progression);
Room load_room(const Rom& rom, size_t index, const Progression& progression = {});
// Door lists are not terminated; counts come from the pinned source index.
std::vector<Door> load_doors(const Rom& rom, size_t index);
LoadStation load_station(const Rom& rom, unsigned area, unsigned station);
size_t room_index_from_header(uint32_t header);
Image draw_samus(const Rom& rom, int pose, int frame);
Image draw_power_beam(const Rom& rom, int direction);
void redraw_block(Room& room, size_t index);
// Resolve signed BTS extensions once for movement and projectile reactions.
// Returns blocks.size() for out-of-room/cyclic data. Zero extensions remain air.
size_t reaction_block(const Room& room, int x, int y);
std::array<uint32_t, 256> decode_palette(std::span<const uint8_t> bytes);
uint8_t tile_pixel(std::span<const uint8_t> tiles, int tile, int x, int y);
// Static block geometry only. PLM reactions, damage and directional Samus
// collision responses are separate gameplay systems still to be translated.
bool room_solid_pixel(const Room& room, const Rom& rom, int x, int y);
}
