#include "thor/samus.hpp"
#include "reference_index.hpp"
#include <stdexcept>

namespace thor {
uint8_t pose_radius(const Rom& rom, uint16_t pose) { return rom.byte(reference::PoseDefinitions + pose * 8 + 6); }
uint8_t movement_type(const Rom& rom, uint16_t pose) { return rom.byte(reference::PoseDefinitions + pose * 8 + 1); }
uint16_t input_pose(const Rom& rom, uint16_t pose, uint16_t held, uint16_t pressed, int32_t vx) {
    const auto table = 0x910000u | rom.word(reference::TransitionTable + pose * 2);
    for (unsigned i = 0; i < 64; ++i) {
        const auto entry = table + i * 6;
        const auto required_press = rom.word(entry);
        if (required_press == 0xFFFF) break;
        const auto required_held = rom.word(entry + 2);
        if ((required_press & pressed) == required_press && (required_held & held) == required_held)
            return rom.word(entry + 4);
    }
    const auto type = movement_type(rom, pose);
    // $91:8304 retains moving/spinning poses during deceleration.
    if (vx && (type == 1 || type == 3 || type == 4 || type == 8 || type == 16)) return pose;
    const auto next = rom.byte(reference::PoseDefinitions + pose * 8 + 2);
    return next == 0xFF ? pose : next;
}
void animate_samus(const Rom& rom, SamusAnimation& a, uint16_t equipped, bool grounded, bool) {
    if (a.timer && --a.timer) return;
    ++a.frame;
    const auto table = 0x910000u | rom.word(reference::AnimationDelayTable + a.pose * 2);
    for (unsigned i = 0; i < 16; ++i) {
        const auto at = table + a.frame;
        const auto value = rom.byte(at);
        if (value < 0x80) { a.timer = value ? value : 1; return; }
        auto change = [&](uint16_t pose) { a.pose = pose; a.frame = 0; a.timer = 1; };
        switch (value & 15) {
        case 15: a.frame = 0; break;
        case 14: a.frame = uint16_t(a.frame - rom.byte(at + 1)); break;
        case 13: change(rom.byte(at + 1)); return;
        case 12: change(rom.byte(at + ((equipped & rom.word(at + 1)) ? 4 : 3))); return;
        case 10: change(rom.byte(at + (grounded ? 1 : 2))); return;
        case 9: change(rom.byte(at + ((equipped & rom.word(at + 1)) ? 5 : 3) + (grounded ? 0 : 1))); return;
        // $90:8370 tests a prospective jump pose, not the held jump button.
        // A jump transition has already replaced the current pose above;
        // turning animations must finish even while Jump remains held.
        case 8: change(rom.byte(at + 1)); return;
        default: a.frame = 0; a.timer = 1; return; // Later movement families use their own animation commands.
        }
    }
    throw std::runtime_error("Unbounded Samus animation command chain");
}
}
