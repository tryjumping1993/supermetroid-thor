#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace thor {
struct SaveSummary {
    bool valid = false;
    uint16_t health = 0, max_health = 0, missiles = 0, max_missiles = 0;
    uint16_t supers = 0, max_supers = 0, power_bombs = 0, max_power_bombs = 0;
    uint16_t equipped_items = 0, collected_items = 0, equipped_beams = 0, collected_beams = 0;
    uint16_t area = 0, station = 0, reserve = 0, max_reserve = 0;
};
class Sram {
public:
    static constexpr size_t size = 8192, slot_size = 0x65C;
    explicit Sram(std::span<const uint8_t> bytes);
    Sram() = default;
    bool valid(unsigned slot) const;
    std::span<const uint8_t> slot(unsigned slot) const;
    void write_slot(unsigned slot, std::span<const uint8_t> payload);
    SaveSummary summary(unsigned slot) const;
    const auto& bytes() const { return bytes_; }
private:
    static constexpr std::array<size_t, 3> offsets{0x10, 0x66C, 0xCC8};
    std::array<uint8_t, size> bytes_{};
};
}
