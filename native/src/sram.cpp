#include "thor/sram.hpp"
#include <algorithm>
#include <stdexcept>

namespace thor {
namespace {
uint16_t word(std::span<const uint8_t> bytes, size_t at) { return bytes[at] | (bytes[at + 1] << 8); }
void put(std::span<uint8_t> bytes, size_t at, uint16_t value) { bytes[at] = value & 255; bytes[at + 1] = value >> 8; }
uint16_t checksum(std::span<const uint8_t> bytes) {
    uint16_t sum = 0;
    for (size_t i = 0; i < bytes.size(); i += 2) sum = uint16_t(sum + word(bytes, i));
    return sum;
}
}
Sram::Sram(std::span<const uint8_t> bytes) {
    if (bytes.size() != size) throw std::runtime_error("Expected 8192-byte SNES SRAM, not emulator state");
    std::copy(bytes.begin(), bytes.end(), bytes_.begin());
}
std::span<const uint8_t> Sram::slot(unsigned slot) const {
    if (slot >= offsets.size()) throw std::runtime_error("Invalid save slot");
    return std::span<const uint8_t>(bytes_).subspan(offsets[slot], slot_size);
}
bool Sram::valid(unsigned index) const {
    const auto sum = checksum(slot(index));
    // $81:80C5 accepts either checksum copy, preserving vanilla recovery behavior.
    for (size_t base : {size_t(0), size_t(0x1FF0)})
        if (word(bytes_, base + index * 2) == sum &&
            word(bytes_, base + 8 + index * 2) == uint16_t(sum ^ 0xFFFF)) return true;
    return false;
}
void Sram::write_slot(unsigned index, std::span<const uint8_t> payload) {
    if (index >= offsets.size() || payload.size() != slot_size) throw std::runtime_error("Invalid save payload");
    std::copy(payload.begin(), payload.end(), bytes_.begin() + offsets[index]);
    const auto sum = checksum(payload);
    for (size_t base : {size_t(0), size_t(0x1FF0)}) {
        put(bytes_, base + index * 2, sum);
        put(bytes_, base + 8 + index * 2, sum ^ 0xFFFF);
    }
}
SaveSummary Sram::summary(unsigned index) const {
    SaveSummary s;
    s.valid = valid(index); if (!s.valid) return s;
    const auto p = slot(index);
    s.equipped_items = word(p, 0); s.collected_items = word(p, 2);
    s.equipped_beams = word(p, 4); s.collected_beams = word(p, 6);
    s.health = word(p, 0x20); s.max_health = word(p, 0x22);
    s.missiles = word(p, 0x24); s.max_missiles = word(p, 0x26);
    s.supers = word(p, 0x28); s.max_supers = word(p, 0x2A);
    s.power_bombs = word(p, 0x2C); s.max_power_bombs = word(p, 0x2E);
    s.max_reserve = word(p, 0x32); s.reserve = word(p, 0x34);
    s.station = word(p, 0x156); s.area = word(p, 0x158);
    return s;
}
}
