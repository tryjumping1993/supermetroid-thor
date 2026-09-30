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
size_t room_index_from_header(uint32_t header) {
    for (size_t i = 0; i < std::size(reference::rooms); ++i) if (reference::rooms[i].header == header) return i;
    throw std::runtime_error("Unknown room header");
}
LoadStation load_station(const Rom& rom, unsigned area, unsigned station) {
    if (area > 6 || station > 7) throw std::runtime_error("Unsupported load station");
    const auto at = (0x800000u | rom.word(reference::LoadStationListPointers + area * 2)) + station * 14;
    LoadStation result;
    result.room_index = room_index_from_header(0x8F0000u | rom.word(at));
    result.door = rom.word(at + 2);
    result.camera_x = rom.word(at + 6); result.camera_y = rom.word(at + 8);
    result.x = result.camera_x + 128 + int16_t(rom.word(at + 12));
    result.y = result.camera_y + rom.word(at + 10);
    return result;
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
    uint32_t object = 0x8F0000u | rom.word(room.state + 20);
    for (unsigned i = 0; i < 256 && rom.word(object); ++i, object += 6)
        room.objects.push_back({rom.word(object), rom.word(object + 4), rom.byte(object + 2), rom.byte(object + 3)});
    uint32_t enemy = 0xA10000u | rom.word(room.state + 8);
    for (unsigned i = 0; i < 128 && rom.word(enemy) != 0xFFFF; ++i, enemy += 16)
        room.enemies.push_back({rom.word(enemy), rom.word(enemy + 2), rom.word(enemy + 4), rom.word(enemy + 6),
            rom.word(enemy + 8), rom.word(enemy + 10), rom.word(enemy + 12), rom.word(enemy + 14)});
    room.enemy_quota = rom.byte(enemy + 2);
    room.layer2_x = rom.byte(room.state + 12); room.layer2_y = rom.byte(room.state + 13);
    const auto scroll = rom.word(room.state + 14);
    const size_t screens = size_t(room.width / 256) * (room.height / 256);
    room.scrolls.assign(screens, scroll < 0x8000 ? uint8_t(scroll + 1) : 0);
    if (scroll >= 0x8000) for (size_t i = 0; i < screens; ++i) room.scrolls[i] = rom.byte((0x8F0000u | scroll) + i);
    if (progression.entering_door) {
        const auto script = rom.word((0x830000u | progression.entering_door) + 10);
        for (const auto& action : reference::door_scrolls) if (action.script == script && action.screen < room.scrolls.size())
            room.scrolls[action.screen] = action.color;
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
    room.tiletable = std::move(tiletable); room.tiles = std::move(tiles); room.colors = colors;
    room.foreground = {room.width, room.height, std::vector<uint32_t>(size_t(room.width) * room.height)};
    room.foreground_low = room.foreground; room.foreground_high = room.foreground;
    for (size_t i = 0; i < room.blocks.size(); ++i) redraw_block(room, i);
    // $82:E5C7 library command data: unpack tilemaps and transfer into BG2.
    // Transfers from WRAM refer to the just-decoded buffer, not CPU execution.
    std::vector<uint8_t> background_data;
    auto library = rom.word(room.state + 22);
    if (library >= 0x8000) {
        uint32_t at = 0x8F0000u | library;
        for (unsigned i = 0; i < 64; ++i) {
            const auto command = rom.word(at); at += 2;
            if (!command) break;
            if (command == 4) { background_data = rom.unpack(rom.pointer(at)); at += 5; }
            else if (command == 2 || command == 8) {
                const auto source = rom.pointer(at);
                const auto bytes = rom.word(at + 5);
                if ((source >> 16) != 0x7E && (source >> 16) != 0x7F) {
                    const auto data = rom.slice(source, bytes); background_data.assign(data.begin(), data.end());
                }
                at += 7;
            } else if (command == 14) { at += 9; }
            else if (command == 6 || command == 10 || command == 12) { background_data.clear(); }
            else break; // Untranslated library features outside the connected slice.
        }
    }
    if (background_data.size() >= 2048) {
        room.background = {256, 256, std::vector<uint32_t>(65536)};
        for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x) {
            const auto tile = word(background_data, ((y / 8) * 32 + x / 8) * 2);
            const auto value = tile_pixel(room.tiles, tile & 0x3FF, (tile & 0x4000) ? 7 - x % 8 : x % 8,
                (tile & 0x8000) ? 7 - y % 8 : y % 8);
            room.background.pixels[y * 256 + x] = value ? room.colors[((tile >> 10) & 7) * 16 + value] : 0;
        }
    }
    if (room.background.pixels.empty() && level.size() >= 2 + bytes + count + visible_count * 2) {
        Room layer; layer.width = room.width; layer.height = room.height; layer.blocks.resize(visible_count);
        layer.tiles = room.tiles; layer.tiletable = room.tiletable; layer.colors = room.colors;
        layer.foreground = {room.width, room.height, std::vector<uint32_t>(size_t(room.width) * room.height)};
        for (size_t i = 0; i < visible_count; ++i) { layer.blocks[i] = word(level, 2 + bytes + count + i * 2); redraw_block(layer, i); }
        room.background = std::move(layer.foreground);
    }
    return room;
}

void redraw_block(Room& room, size_t index) {
    if (index >= room.blocks.size()) throw std::runtime_error("Invalid block redraw");
    if (room.foreground.pixels.empty()) return;
    const int bx = int(index % (room.width / 16)), by = int(index / (room.width / 16));
    const auto block = room.blocks[index];
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        const int px = (block & 0x400) ? 15 - x : x, py = (block & 0x800) ? 15 - y : y;
        const auto tile = word(room.tiletable, (block & 0x3FF) * 8 + (py / 8 * 2 + px / 8) * 2);
        const int tx = (tile & 0x4000) ? 7 - px % 8 : px % 8, ty = (tile & 0x8000) ? 7 - py % 8 : py % 8;
        const auto pixel = tile_pixel(room.tiles, tile & 0x3FF, tx, ty);
        const auto at = size_t(by * 16 + y) * room.width + bx * 16 + x;
        const auto color = pixel ? room.colors[((tile >> 10) & 7) * 16 + pixel] : 0;
        room.foreground.pixels[at] = color;
        if (!room.foreground_low.pixels.empty()) {
            room.foreground_low.pixels[at] = (tile & 0x2000) ? 0 : color;
            room.foreground_high.pixels[at] = (tile & 0x2000) ? color : 0;
        }
    }
}

Image draw_power_beam(const Rom& rom, int direction) {
    if (direction < 0 || direction > 9) throw std::runtime_error("Invalid beam direction");
    const auto list = 0x930000u | rom.word(reference::ProjectileDataTable_Uncharged_Power + 2 + direction * 2);
    const auto map = 0x930000u | rom.word(list + 2);
    const auto tiles = rom.slice(reference::Tiles_PowerBeam, 512);
    const auto colors = decode_palette(rom.slice(reference::BeamPalettes_Power, 32));
    Image image{16, 16, std::vector<uint32_t>(256)};
    const auto count = rom.word(map);
    if (count > 8) throw std::runtime_error("Invalid beam spritemap");
    for (unsigned i = 0; i < count; ++i) {
        const auto entry = rom.slice(map + 2 + i * 5, 5);
        const unsigned xword = entry[0] | (entry[1] << 8), attributes = entry[3] | (entry[4] << 8);
        int ox = xword & 0x1FF; if (ox & 0x100) ox -= 512;
        const int oy = int(int8_t(entry[2])), dimension = (xword & 0x8000) ? 16 : 8;
        for (int y = 0; y < dimension; ++y) for (int x = 0; x < dimension; ++x) {
            const int sx = (attributes & 0x4000) ? dimension - 1 - x : x;
            const int sy = (attributes & 0x8000) ? dimension - 1 - y : y;
            const auto pixel = tile_pixel(tiles, int(attributes & 0x1FF) - 0x30 + sx / 8 + sy / 8 * 16, sx % 8, sy % 8);
            const int dx = ox + x + 8, dy = oy + y + 8;
            if (pixel && dx >= 0 && dx < 16 && dy >= 0 && dy < 16) image.pixels[dy * 16 + dx] = colors[pixel];
        }
    }
    return image;
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
