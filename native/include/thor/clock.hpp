#pragma once
#include <algorithm>
#include <cstdint>

namespace thor {
// NTSC master-clock cycles/frame. Timestamp-based; never alternate two display
// frames per tick (120 Hz and NTSC frame cadence are not an exact 2:1 ratio).
class TickClock {
public:
    static constexpr uint64_t frequency = 21477272;
    static constexpr uint64_t cycles = 357366;
    static constexpr uint64_t quantum = cycles * 1000000000ULL;
    template<class F> void advance(uint64_t timestamp, F tick) {
        if (!started_) { started_ = true; last_ = timestamp; return; }
        if (timestamp < last_) { reset(); return; }
        const auto elapsed = std::min<uint64_t>(timestamp - last_, 250000000ULL);
        last_ = timestamp;
        accumulator_ += elapsed * frequency;
        while (accumulator_ >= quantum) { accumulator_ -= quantum; tick(); }
    }
    double alpha() const { return double(accumulator_) / double(quantum); }
    void reset() { started_ = false; last_ = accumulator_ = 0; }
private:
    uint64_t last_ = 0, accumulator_ = 0;
    bool started_ = false;
};
}
