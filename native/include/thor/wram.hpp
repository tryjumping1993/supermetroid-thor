#pragma once
// 128 KiB WRAM ($7E0000..$7FFFFF) mirror with byte/word/long proxies.
// Ported game code reads and writes this array with the same names and layout as
// the original so routines translate one-to-one (see docs/PORTING_GUIDE.md).
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace thor {
struct Byte {
    uint8_t* p;
    operator uint8_t() const { return *p; }
    Byte& operator=(uint8_t v) { *p = v; return *this; }
    Byte& operator=(const Byte& o) { *p = *o.p; return *this; }
    Byte& operator+=(int v) { *p = uint8_t(*p + v); return *this; }
    Byte& operator-=(int v) { *p = uint8_t(*p - v); return *this; }
    Byte& operator|=(int v) { *p = uint8_t(*p | v); return *this; }
    Byte& operator&=(int v) { *p = uint8_t(*p & v); return *this; }
    Byte& operator^=(int v) { *p = uint8_t(*p ^ v); return *this; }
    Byte& operator++() { ++*p; return *this; }
    Byte& operator--() { --*p; return *this; }
    uint8_t operator++(int) { return (*p)++; }
    uint8_t operator--(int) { return (*p)--; }
};
struct Word {
    uint8_t* p;
    uint16_t get() const { uint16_t v; std::memcpy(&v, p, 2); return v; }
    void set(uint16_t v) { std::memcpy(p, &v, 2); }
    operator uint16_t() const { return get(); }
    Word& operator=(uint16_t v) { set(v); return *this; }
    Word& operator=(const Word& o) { set(o.get()); return *this; }
    Word& operator+=(int v) { set(uint16_t(get() + v)); return *this; }
    Word& operator-=(int v) { set(uint16_t(get() - v)); return *this; }
    Word& operator|=(int v) { set(uint16_t(get() | v)); return *this; }
    Word& operator&=(int v) { set(uint16_t(get() & v)); return *this; }
    Word& operator^=(int v) { set(uint16_t(get() ^ v)); return *this; }
    Word& operator<<=(int v) { set(uint16_t(get() << v)); return *this; }
    Word& operator>>=(int v) { set(uint16_t(get() >> v)); return *this; }
    Word& operator++() { set(uint16_t(get() + 1)); return *this; }
    Word& operator--() { set(uint16_t(get() - 1)); return *this; }
    uint16_t operator++(int) { auto v = get(); set(uint16_t(v + 1)); return v; }
    uint16_t operator--(int) { auto v = get(); set(uint16_t(v - 1)); return v; }
    int16_t s() const { return int16_t(get()); }
};
struct Long {
    uint8_t* p;
    uint32_t get() const { return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16); }
    void set(uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); }
    operator uint32_t() const { return get(); }
    Long& operator=(uint32_t v) { set(v); return *this; }
};
class Wram {
public:
    static constexpr uint32_t size = 0x20000;
    uint8_t* bytes_at(uint32_t offset) { check(offset, 1); return ram_.data() + offset; }
    Byte byte_at(uint32_t offset) { return Byte{bytes_at(offset)}; }
    Word word_at(uint32_t offset) { check(offset, 2); return Word{ram_.data() + offset}; }
    Long long_at(uint32_t offset) { check(offset, 3); return Long{ram_.data() + offset}; }
    uint8_t* data() { return ram_.data(); }
    void clear() { ram_.fill(0); }
private:
    static void check(uint32_t offset, uint32_t width) {
        if (offset + width > size) throw std::out_of_range("WRAM access outside $7E0000..$7FFFFF");
    }
    std::array<uint8_t, size> ram_{};
};
}
