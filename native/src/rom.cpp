#include "thor/rom.hpp"
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace thor {
uint32_t crc32(std::span<const uint8_t> data) {
    uint32_t result = ~0u;
    for (auto b : data) {
        result ^= b;
        for (int bit = 0; bit < 8; ++bit)
            result = (result >> 1) ^ (0xEDB88320u & (0u - (result & 1u)));
    }
    return ~result;
}

// Native translation of $80:B119..B264. Inverted copies invert bytes, not bit
// order. Backreferences may overlap; copying a pre-sliced range is incorrect.
std::vector<uint8_t> decompress(std::span<const uint8_t> data, size_t limit) {
    size_t cursor = 0;
    auto get = [&]() { if (cursor >= data.size()) throw std::runtime_error("Truncated compressed data"); return data[cursor++]; };
    std::vector<uint8_t> out;
    while (true) {
        uint8_t code = get();
        if (code == 0xFF) return out;
        unsigned command = code >> 5;
        size_t length = (code & 31) + 1;
        if (command == 7) { command = (code >> 2) & 7; length = ((code & 3) << 8) + get() + 1; }
        if (length > limit - out.size()) throw std::runtime_error("Decompression exceeds destination");
        if (command == 0) {
            for (size_t i = 0; i < length; ++i) out.push_back(get());
        } else if (command == 1 || command == 3) {
            uint8_t value = get();
            for (size_t i = 0; i < length; ++i) out.push_back(uint8_t(value + (command == 3 ? i : 0)));
        } else if (command == 2) {
            const uint8_t a = get(), b = get();
            for (size_t i = 0; i < length; ++i) out.push_back((i & 1) ? b : a);
        } else {
            size_t from;
            if (command >= 6) {
                const unsigned distance = get();
                if (distance == 0 || distance > out.size()) throw std::runtime_error("Invalid relative backreference");
                from = out.size() - distance;
            } else { from = get(); from |= size_t(get()) << 8; }
            for (size_t i = 0; i < length; ++i) {
                if (from >= out.size()) throw std::runtime_error("Invalid absolute backreference");
                auto value = uint8_t(out[from++] ^ ((command & 1) ? 0xFF : 0));
                out.push_back(value);
            }
        }
    }
}

Rom::Rom(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {
    if (bytes_.size() == 0x300200) bytes_.erase(bytes_.begin(), bytes_.begin() + 512);
    if (bytes_.size() != 0x300000 || crc32(bytes_) != 0xD63ED5F8)
        throw std::runtime_error("Expected original NTSC Super Metroid ROM");
}
Rom Rom::from_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open ROM");
    return Rom(std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), {}));
}
size_t Rom::offset(uint32_t address) {
    if ((address & 0xFFFF) < 0x8000 || address > 0xFFFFFF)
        throw std::runtime_error("Invalid LoROM address");
    return ((address >> 16) & 0x7F) * 0x8000 + (address & 0x7FFF);
}
std::span<const uint8_t> Rom::slice(uint32_t address, size_t count) const {
    size_t start = offset(address);
    if (start > bytes_.size() || count > bytes_.size() - start) throw std::runtime_error("ROM read out of bounds");
    return {bytes_.data() + start, count};
}
uint8_t Rom::byte(uint32_t address) const { return slice(address, 1)[0]; }
uint16_t Rom::word(uint32_t address) const { auto s = slice(address, 2); return s[0] | (s[1] << 8); }
uint32_t Rom::pointer(uint32_t address) const { auto s = slice(address, 3); return s[0] | (s[1] << 8) | (s[2] << 16); }
std::vector<uint8_t> Rom::unpack(uint32_t address) const {
    const auto start = offset(address);
    if (start >= bytes_.size()) throw std::runtime_error("Compressed pointer out of bounds");
    return decompress(std::span<const uint8_t>(bytes_).subspan(start));
}
}
