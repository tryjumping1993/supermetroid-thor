#pragma once
#include "thor/clock.hpp"
#include "thor/content.hpp"
#include "thor/sram.hpp"
#include <memory>
#include <set>

namespace thor {
enum Button : uint16_t { Right = 0x100, Left = 0x200, Down = 0x400, Up = 0x800, Start = 0x1000, Select = 0x2000, Jump = 0x8000 };
struct State {
    uint64_t tick = 0;
    int32_t x = 0, y = 0, vx = 0, vy = 0; // 16.16 fixed-point
    float camera_x = 0, camera_y = 0;
    uint32_t generation = 0;
    uint16_t pose = 1, frame = 0;
    bool grounded = false, paused = false;
};
struct RenderSnapshot {
    float x = 0, y = 0, camera_x = 0, camera_y = 0;
    uint16_t pose = 1, frame = 0;
    uint32_t generation = 0;
    uint64_t tick = 0;
};
class Session {
public:
    explicit Session(Rom rom);
    void select_room(size_t index);
    // Explicit development traversal, pending original door/PLM scripts.
    // The expected source prevents a stale companion choice traversing a
    // different room's exit after another display changes the session.
    void traverse_door(size_t expected_room, size_t door_index);
    void advance(uint64_t timestamp);
    void step(uint16_t buttons);
    void buttons(uint16_t buttons) { buttons_ = buttons; }
    void suspend() { buttons_ = previous_buttons_ = 0; clock_.reset(); previous_ = state_; }
    void toggle_pause() { state_.paused = !state_.paused; suspend(); }
    RenderSnapshot snapshot(bool interpolate = true) const;
    const State& state() const { return state_; }
    const Room& room() const { return room_; }
    size_t room_index() const { return room_index_; }
    const Rom& rom() const { return rom_; }
    const std::set<int>& explored() const { return explored_; }
    const Progression& progression() const { return progression_; }
    double alpha() const { return clock_.alpha(); }
private:
    bool solid(int x, int y) const;
    bool collides(int32_t x, int32_t y) const;
    void move_axis(int32_t delta, bool vertical);
    Rom rom_;
    Progression progression_;
    Room room_;
    size_t room_index_ = 0;
    State previous_, state_;
    TickClock clock_;
    uint16_t buttons_ = 0, previous_buttons_ = 0;
    std::set<int> explored_;
};
}
