#include "thor/content.hpp"
#include "reference_index.hpp"
#include <algorithm>
#include <stdexcept>

namespace thor {
namespace {
uint16_t word(std::span<const uint8_t> b, size_t at) {
    if (at + 1 >= b.size()) throw std::runtime_error("Truncated decoded content");
    return b[at] | (b[at + 1] << 8);
}
uint32_t color(uint16_t snes) {
    auto expand = [](int v) { return (v << 3) | (v >> 2); };
    return 0xFF000000u | (expand(snes & 31)) | (expand((snes >> 5) & 31) << 8) | (expand((snes >> 10) & 31) << 16);
}
}
std::array<uint32_t, 256> decode_palette(std::span<const uint8_t> bytes) {
    std::array<uint32_t, 256> colors{};
    for (size_t i = 0; i < std::min(size_t(256), bytes.size() / 2); ++i) colors[i] = color(word(bytes, i * 2));
    return colors;
}
uint8_t tile_pixel(std::span<const uint8_t> tiles, int tile, int x, int y) {
    if (tile < 0 || x < 0 || x > 7 || y < 0 || y > 7) throw std::runtime_error("Invalid tile coordinate");
    const size_t at = size_t(tile) * 32 + size_t(y) * 2;
    if (at + 17 >= tiles.size()) throw std::runtime_error("Tile index out of bounds");
    const int bit = 7 - x;
    return ((tiles[at] >> bit) & 1) | (((tiles[at + 1] >> bit) & 1) << 1) |
           (((tiles[at + 16] >> bit) & 1) << 2) | (((tiles[at + 17] >> bit) & 1) << 3);
}
uint16_t Room::block(int x, int y) const {
    if (x < 0 || y < 0 || x >= width / 16 || y >= height / 16) return 0x8000;
    return blocks.at(size_t(y) * (width / 16) + x);
}
std::vector<Door> load_doors(const Rom& rom, size_t index) {
    if (index >= std::size(reference::rooms)) throw std::runtime_error("Invalid room index");
    const auto& ref = reference::rooms[index];
    const uint32_t list = 0x8F0000u | rom.word(ref.header + 9);
    std::vector<Door> doors;
    for (size_t i = 0; i < ref.door_count; ++i) {
        Door door;
        door.address = 0x830000u | rom.word(list + i * 2);
        const auto destination = rom.word(door.address);
        // Ceres elevator/escape entries can have a null destination. Preserve
        // them for inspection, but never resolve them as an ordinary room.
        door.destination = destination ? 0x8F0000u | destination : 0;
        door.destination_index = std::size(reference::rooms);
        for (size_t j = 0; j < std::size(reference::rooms); ++j)
            if (reference::rooms[j].header == door.destination) { door.destination_index = j; break; }
        if (door.destination && door.destination_index == std::size(reference::rooms))
            throw std::runtime_error("Unknown door destination");
        door.properties = rom.byte(door.address + 2); door.direction = rom.byte(door.address + 3);
        door.cap_x = rom.byte(door.address + 4); door.cap_y = rom.byte(door.address + 5);
        door.screen_x = rom.byte(door.address + 6); door.screen_y = rom.byte(door.address + 7);
        door.transition_speed = rom.word(door.address + 8); door.custom_asm = rom.word(door.address + 10);
        doors.push_back(door);
    }
    return doors;
}
Room load_room(const Rom& rom, size_t index, const Progression& progression) {
    if (index >= std::size(reference::rooms)) throw std::runtime_error("Invalid room index");
    const auto& ref = reference::rooms[index];
    Room room;
    room.header = ref.header; room.state = select_room_state(rom, ref.header, progression); room.name = ref.name;
    room.doors = load_doors(rom, index);
    room.area = rom.byte(ref.header + 1);
    room.map_x = rom.byte(ref.header + 2); room.map_y = rom.byte(ref.header + 3);
    room.width = rom.byte(ref.header + 4) * 256; room.height = rom.byte(ref.header + 5) * 256;
    const auto level = rom.unpack(rom.pointer(room.state));
    const size_t bytes = word(level, 0), count = bytes / 2;
    if (!room.width || !room.height ||
        bytes % 2 || 2 + bytes + count > level.size()) throw std::runtime_error("Invalid room geometry: " + room.name +
            " (" + std::to_string(room.width) + "x" + std::to_string(room.height) + ", level bytes=" + std::to_string(bytes) + ", decoded=" + std::to_string(level.size()) + ")");
    // Vanilla clears the whole destination to $8000 then copies the stored
    // length. Some rooms include unused extra geometry (e.g. Double Chamber).
    const size_t visible_count = size_t(room.width / 16) * (room.height / 16);
    room.blocks.assign(visible_count, 0x8000); room.bts.assign(visible_count, 0);
    for (size_t i = 0; i < std::min(count, visible_count); ++i) {
        room.blocks[i] = word(level, 2 + i * 2); room.bts[i] = level[2 + bytes + i];
    }
    const int tileset = rom.byte(room.state + 3);
    const auto table = 0x8F0000u | rom.word(reference::Tileset_Pointers + tileset * 2);
    std::vector<uint8_t> tiletable(8192), tiles(32768);
    auto install = [](auto& destination, const auto& source, size_t offset) {
        if (offset + source.size() > destination.size()) throw std::runtime_error("Tileset exceeds VRAM layout");
        std::copy(source.begin(), source.end(), destination.begin() + offset);
    };
    if (room.area != 6) {
        install(tiletable, rom.unpack(reference::CRE_TileTable_Compressed), 0);
        install(tiles, rom.unpack(reference::CRE_Tiles_Compressed), 0x5000);
    }
    install(tiletable, rom.unpack(rom.pointer(table)), room.area == 6 ? 0 : 0x800);
    install(tiles, rom.unpack(rom.pointer(table + 3)), 0);
    const auto colors = decode_palette(rom.unpack(rom.pointer(table + 6)));
    room.foreground = {room.width, room.height, std::vector<uint32_t>(size_t(room.width) * room.height)};
    for (int by = 0; by < room.height / 16; ++by) for (int bx = 0; bx < room.width / 16; ++bx) {
        const auto block = room.block(bx, by);
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
            const int px = (block & 0x400) ? 15 - x : x;
            const int py = (block & 0x800) ? 15 - y : y;
            const auto tile = word(tiletable, (block & 0x3FF) * 8 + (py / 8 * 2 + px / 8) * 2);
            const int tx = (tile & 0x4000) ? 7 - px % 8 : px % 8;
            const int ty = (tile & 0x8000) ? 7 - py % 8 : py % 8;
            const auto pixel = tile_pixel(tiles, tile & 0x3FF, tx, ty);
            room.foreground.pixels[size_t(by * 16 + y) * room.width + bx * 16 + x] = pixel ? colors[((tile >> 10) & 7) * 16 + pixel] : 0;
        }
    }
    return room;
}

// $92:8000 tile definitions + $92:9263/$945D spritemap selection. DMA entries
// are {24-bit source, 16-bit row0 size, 16-bit row1 size}; 16x16 OBJ tiles use
// tile+16 for their second row. Canvas origin matches Samus' center.
Image draw_samus(const Rom& rom, int pose, int frame) {
    if (pose < 0 || pose > 252 || frame < 0 || frame > 63) throw std::runtime_error("Invalid Samus pose/frame");
    Image image{64, 64, std::vector<uint32_t>(4096)};
    std::vector<uint8_t> vram(16384);
    const uint32_t animation = 0x920000u | rom.word(reference::SamusTilesAnimation_AnimationDefinitionPointers + pose * 2);
    const auto def = rom.slice(animation + frame * 4, 4);
    for (int half = 0; half < 2; ++half) {
        if (def[half * 2] == 0xFF) continue;
        const auto pointers = half ? reference::SamusBottomHalfTilesAnimation_TilesDefinitionPointers : reference::SamusTopHalfTilesAnimation_TilesDefinitionPointers;
        const uint32_t dma = (0x920000u | rom.word(pointers + def[half * 2] * 2)) + def[half * 2 + 1] * 7;
        const uint32_t source = rom.pointer(dma);
        const unsigned first = rom.word(dma + 3), second = rom.word(dma + 5);
        const size_t dest = half ? 0x100 : 0;
        if (first > 0x200 || second > 0x200) throw std::runtime_error("Invalid Samus DMA row");
        auto a = rom.slice(source, first);
        auto b = rom.slice(source + first, second);
        std::copy(a.begin(), a.end(), vram.begin() + dest);
        std::copy(b.begin(), b.end(), vram.begin() + dest + 0x200);
    }
    const auto palette = decode_palette(rom.slice(reference::SamusPalettes_PowerSuit, 32));
    for (int half = 0; half < 2; ++half) {
        const auto indices = half ? reference::SamusSpritemapTableIndices_BottomHalf : reference::SamusSpritemapTableIndices_TopHalf;
        const unsigned index = rom.word(indices + pose * 2) + frame;
        const uint32_t map = 0x920000u | rom.word(reference::SamusSpritemapTable + index * 2);
        if ((map & 0xFFFF) == 0) continue;
        const unsigned entries = rom.word(map);
        if (entries > 64) throw std::runtime_error("Invalid Samus spritemap");
        for (unsigned entry = 0; entry < entries; ++entry) {
            const auto s = rom.slice(map + 2 + entry * 5, 5);
            const int xword = s[0] | (s[1] << 8);
            int ox = xword & 0x1FF; if (ox & 0x100) ox -= 512;
            const int oy = int(int8_t(s[2])), attributes = s[3] | (s[4] << 8);
            const int dimension = (xword & 0x8000) ? 16 : 8;
            for (int y = 0; y < dimension; ++y) for (int x = 0; x < dimension; ++x) {
                const int sx = (attributes & 0x4000) ? dimension - 1 - x : x;
                const int sy = (attributes & 0x8000) ? dimension - 1 - y : y;
                const auto pixel = tile_pixel(vram, (attributes & 0x1FF) + sx / 8 + sy / 8 * 16, sx % 8, sy % 8);
                const int dx = ox + x + 32, dy = oy + y + 32;
                if (pixel && dx >= 0 && dx < 64 && dy >= 0 && dy < 64) image.pixels[dy * 64 + dx] = palette[pixel];
            }
        }
    }
    return image;
}
}
