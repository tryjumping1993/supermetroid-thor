#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <string>

namespace thor {
uint32_t crc32(std::span<const uint8_t> data);
std::vector<uint8_t> decompress(std::span<const uint8_t> data, size_t limit = 65536);
class Rom {
public:
    explicit Rom(std::vector<uint8_t> bytes);
    static Rom from_file(const std::string& path);
    static size_t offset(uint32_t address);
    uint8_t byte(uint32_t address) const;
    uint16_t word(uint32_t address) const;
    uint32_t pointer(uint32_t address) const;
    std::span<const uint8_t> slice(uint32_t address, size_t count) const;
    std::vector<uint8_t> unpack(uint32_t address) const;
private:
    std::vector<uint8_t> bytes_;
};
}
