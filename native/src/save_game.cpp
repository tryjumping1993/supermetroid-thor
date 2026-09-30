#include "thor/save_game.hpp"
#include "reference_index.hpp"
#include <algorithm>
#include <stdexcept>

namespace thor {
namespace {
uint16_t word(std::span<const uint8_t> bytes, size_t at) { return bytes[at] | bytes[at + 1] << 8; }
void put(std::span<uint8_t> bytes, size_t at, uint16_t value) { bytes[at] = uint8_t(value); bytes[at + 1] = uint8_t(value >> 8); }
template<size_t N> void read_bits(std::bitset<N>& bits, std::span<const uint8_t> bytes, size_t at, size_t count = N / 8) {
    for (size_t i = 0; i < count * 8; ++i) bits.set(i, (bytes[at + i / 8] >> (i % 8)) & 1);
}
template<size_t N> void write_bits(const std::bitset<N>& bits, std::span<uint8_t> bytes, size_t at, size_t count = N / 8) {
    for (size_t i = 0; i < count; ++i) {
        uint8_t value = 0; for (size_t b = 0; b < 8; ++b) if (bits.test(i * 8 + b)) value |= uint8_t(1 << b);
        bytes[at + i] = value;
    }
}
template<class Action> void map_bytes(const Rom& rom, Action action) {
    // Native $81:82E4/834B map packing. The cartridge stores only selected cells.
    for (unsigned area = 0; area < 6; ++area) {
        const unsigned count = rom.byte(reference::SRAMMapData_size + area);
        const unsigned offset = rom.word(reference::SRAMMapData_offset + area * 2);
        const auto list = 0x810000u | rom.word(reference::MapRoomPointers + area * 2);
        for (unsigned i = 0; i < count; ++i) action(area * 256 + rom.byte(list + i), 0x15C + offset + i);
    }
}
}
Progression restore_progression(const Rom& rom, const Sram& sram, unsigned slot) {
    if (!sram.valid(slot)) throw std::runtime_error("Save slot is empty or corrupt");
    const auto bytes = sram.slot(slot);
    Progression p;
    p.equipped_items = word(bytes, 0); p.collected_items = word(bytes, 2);
    p.health = word(bytes, 0x20); p.max_health = word(bytes, 0x22);
    p.missiles = word(bytes, 0x24); p.max_missiles = word(bytes, 0x26); p.max_power_bombs = word(bytes, 0x2E);
    read_bits(p.events, bytes, 0x60, 3);
    std::copy_n(bytes.begin() + 0x68, p.bosses.size(), p.bosses.begin());
    read_bits(p.destroyed_chozo, bytes, 0x70); read_bits(p.items, bytes, 0xB0); read_bits(p.opened_doors, bytes, 0xF0);
    map_bytes(rom, [&](auto map, auto saved) { p.map[map] = bytes[saved]; });
    return p;
}
void save_progression(const Rom& rom, Sram& sram, unsigned slot, const Progression& p, unsigned area, unsigned station) {
    if (slot > 2 || area > 6 || station > 7) throw std::runtime_error("Invalid save station/slot");
    // Preserve unowned fields when continuing an imported vanilla save.
    std::vector<uint8_t> bytes(Sram::slot_size);
    if (sram.valid(slot)) { const auto original = sram.slot(slot); std::copy(original.begin(), original.end(), bytes.begin()); }
    put(bytes, 0, p.equipped_items); put(bytes, 2, p.collected_items);
    put(bytes, 0x20, p.health); put(bytes, 0x22, p.max_health); put(bytes, 0x24, p.missiles); put(bytes, 0x26, p.max_missiles);
    put(bytes, 0x2E, p.max_power_bombs);
    write_bits(p.events, bytes, 0x60, 3); std::copy(p.bosses.begin(), p.bosses.end(), bytes.begin() + 0x68);
    write_bits(p.destroyed_chozo, bytes, 0x70); write_bits(p.items, bytes, 0xB0); write_bits(p.opened_doors, bytes, 0xF0);
    bytes[0x138 + area * 2 + station / 8] |= uint8_t(1 << (station % 8));
    put(bytes, 0x154, 0); put(bytes, 0x156, uint16_t(station)); put(bytes, 0x158, uint16_t(area));
    map_bytes(rom, [&](auto map, auto saved) { bytes[saved] = p.map[map]; });
    sram.write_slot(slot, bytes);
}
}
