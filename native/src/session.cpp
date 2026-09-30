#include "thor/session.hpp"
#include "reference_index.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace thor {
namespace { constexpr int32_t pixel = 65536; }
Session::Session(Rom rom) : rom_(std::move(rom)) { select_room(0); }
void Session::select_room(size_t index) {
    auto context = progression_; context.entering_door = 0;
    auto next = load_room(rom_, index, context); // Complete decode before replacing current session.
    const auto generation = state_.generation + 1;
    room_ = std::move(next); room_index_ = index; progression_ = context;
    state_ = {}; state_.generation = generation; state_.pose = 1;
    state_.x = (room_.width / 2) * pixel;
    state_.y = 48 * pixel;
    // Development slice spawn: first clear center-column floor, not a translated
    // load-station sequence. Scripted landing/ship and door spawns are pending.
    auto floor_at = [&](int x, int from, int to) {
        for (int y = from; y <= to; ++y) {
            if (!collides(x * pixel, y * pixel) && collides(x * pixel, (y + 1) * pixel)) {
                state_.x = x * pixel; state_.y = y * pixel; return true;
            }
        }
        return false;
    };
    const int center = room_.width / 2;
    bool placed = floor_at(center, room_.height / 2, room_.height - 16) ||
                  floor_at(center, 16, room_.height / 2 - 1);
    for (int distance = 16; !placed && distance < room_.width; distance += 16) {
        if (center - distance >= 5) placed = floor_at(center - distance, 16, room_.height - 16);
        if (!placed && center + distance <= room_.width - 5) placed = floor_at(center + distance, 16, room_.height - 16);
    }
    state_.camera_x = std::clamp(float(state_.x / pixel - 128), 0.f, float(std::max(0, room_.width - 256)));
    state_.camera_y = std::clamp(float(state_.y / pixel - 112), 0.f, float(std::max(0, room_.height - 224)));
    previous_ = state_; previous_buttons_ = buttons_ = 0; explored_.clear(); clock_.reset();
}
void Session::traverse_door(size_t expected_room, size_t door_index) {
    if (expected_room != room_index_) throw std::runtime_error("Room changed; choose an exit again");
    if (door_index >= room_.doors.size()) throw std::runtime_error("Invalid door index");
    const Door door = room_.doors[door_index];
    if (!door.destination || (door.properties & 0x80) || door.direction > 7)
        throw std::runtime_error("Special/elevator transitions are not implemented");
    auto context = progression_; context.entering_door = uint16_t(door.address);
    auto next = load_room(rom_, door.destination_index, context);
    // Development placement: near the destination cap, inside its screen.
    // Original $82 scrolling, subpixel speed, nudges and door ASM are pending.
    int x = door.cap_x * 16 + 8, y = door.cap_y * 16 + 32;
    switch (door.direction & 3) {
    case 0: x += 32; break; // moving right into destination
    case 1: x -= 32; break;
    case 2: x += 24; y += 16; break;
    case 3: x += 24; y -= 48; break;
    }
    const int min_x = door.screen_x * 256 + 5, max_x = std::min(next.width - 5, door.screen_x * 256 + 251);
    const int min_y = door.screen_y * 256 + 16, max_y = std::min(next.height - 16, door.screen_y * 256 + 240);
    if (min_x > max_x || min_y > max_y) throw std::runtime_error("Door screen lies outside destination room");
    x = std::clamp(x, min_x, max_x); y = std::clamp(y, min_y, max_y);
    auto clear = [&](int px, int py) {
        for (int cy = py - 16; cy <= py + 15; ++cy)
            for (int cx = px - 5; cx <= px + 4; ++cx)
                if (room_solid_pixel(next, rom_, cx, cy)) return false;
        return true;
    };
    bool placed = clear(x, y);
    // Keep blocked caps solid. Find nearby space instead of changing original
    // room geometry or silently spawning in a different screen.
    for (int radius = 1; !placed && radius <= 96; ++radius) {
        for (int dx = -radius; !placed && dx <= radius; ++dx) {
            const int dy = radius - std::abs(dx);
            for (int sign : {1, -1}) {
                const int px = x + dx, py = y + dy * sign;
                if (px >= min_x && px <= max_x && py >= min_y && py <= max_y && clear(px, py)) {
                    x = px; y = py; placed = true; break;
                }
            }
        }
    }
    if (!placed) throw std::runtime_error("No safe standing spawn near destination door");
    State arrived{};
    arrived.tick = state_.tick; arrived.generation = state_.generation + 1; arrived.paused = state_.paused;
    arrived.x = x * pixel; arrived.y = y * pixel;
    arrived.pose = (door.direction & 3) == 1 ? 2 : 1;
    arrived.camera_x = std::clamp(float(door.screen_x * 256), 0.f, float(std::max(0, next.width - 256)));
    arrived.camera_y = std::clamp(float(door.screen_y * 256), 0.f, float(std::max(0, next.height - 224)));
    // Commit only after destination decoding and spawn validation succeed.
    room_ = std::move(next); room_index_ = door.destination_index; progression_ = context;
    previous_ = state_ = arrived; buttons_ = previous_buttons_ = 0; clock_.reset();
    explored_.clear(); explored_.insert((y / 256) * (room_.width / 256) + x / 256);
}
bool Session::solid(int x, int y) const {
    return room_solid_pixel(room_, rom_, x, y);
}
bool Session::collides(int32_t x, int32_t y) const {
    const int px = int(std::floor(double(x) / pixel)), py = int(std::floor(double(y) / pixel));
    for (int cy = py - 16; cy <= py + 15; ++cy)
        if (solid(px - 5, cy) || solid(px + 4, cy)) return true;
    for (int cx = px - 5; cx <= px + 4; ++cx)
        if (solid(cx, py - 16) || solid(cx, py + 15)) return true;
    return false;
}
void Session::move_axis(int32_t delta, bool vertical) {
    int32_t& position = vertical ? state_.y : state_.x;
    const int32_t start = position;
    const int32_t direction = delta < 0 ? -1 : 1;
    int32_t remaining = std::abs(delta);
    while (remaining) {
        const int32_t step = std::min(remaining, pixel) * direction;
        const auto x = vertical ? state_.x : position + step;
        const auto y = vertical ? position + step : state_.y;
        if (collides(x, y)) {
            // Preserve subpixels up to the nearest non-intersecting point.
            int32_t low = 0, high = std::abs(step);
            while (high - low > 1) {
                int32_t mid = (low + high) / 2;
                if (collides(vertical ? state_.x : position + mid * direction,
                             vertical ? position + mid * direction : state_.y)) high = mid;
                else low = mid;
            }
            position += low * direction;
            if (vertical) { state_.vy = 0; if (delta > 0) state_.grounded = true; }
            else state_.vx = 0;
            break;
        }
        position += step; remaining -= std::abs(step);
    }
    if (position == start && vertical && delta > 0) state_.grounded = true;
}
void Session::step(uint16_t buttons) {
    if (state_.paused) { previous_ = state_; previous_buttons_ = buttons; return; }
    previous_ = state_;
    ++state_.tick;
    int direction = bool(buttons & Right) - bool(buttons & Left);
    const uint32_t table = reference::SamusXSpeedTable_Normal + (state_.grounded ? 1 : 2) * 12;
    auto fixed = [&](uint32_t at) { return int32_t((uint32_t(rom_.word(at)) << 16) | rom_.word(at + 2)); };
    const int32_t acceleration = fixed(table), maximum = fixed(table + 4), deceleration = fixed(table + 8);
    if (direction) {
        state_.vx += direction * acceleration;
        state_.vx = std::clamp(state_.vx, -maximum, maximum);
    } else {
        state_.vx = (state_.vx < 0 ? -1 : 1) * std::max(0, std::abs(state_.vx) - deceleration);
    }
    if ((buttons & Jump) && !(previous_buttons_ & Jump) && state_.grounded) {
        state_.vy = -int32_t((uint32_t(rom_.word(reference::SamusPhysicsConstants_InitialYSpeeds_Jumping)) << 16) |
                              rom_.word(reference::SamusPhysicsConstants_InitialYSubSpeeds_Jumping));
    }
    const auto gravity = (uint32_t(rom_.word(reference::SamusPhysicsConstants_YAccelerationInAir)) << 16) |
                        rom_.word(reference::SamusPhysicsConstants_YSubAccelerationInAir);
    state_.vy = std::min(state_.vy + int32_t(gravity), 5 * pixel);
    state_.grounded = false;
    move_axis(state_.vx, false); move_axis(state_.vy, true);
    if (direction) state_.pose = direction > 0 ? 9 : 10;
    else state_.pose = (state_.pose % 2) ? 1 : 2;
    // Standing/running artwork only; original pose state machine still pending.
    state_.frame = direction && state_.grounded ? uint16_t((state_.tick / 6) % 8) : 0;
    state_.camera_x = std::clamp(float(state_.x) / pixel - 128.f, 0.f, float(std::max(0, room_.width - 256)));
    state_.camera_y = std::clamp(float(state_.y) / pixel - 112.f, 0.f, float(std::max(0, room_.height - 224)));
    const int mx = std::clamp(int(state_.x / pixel / 256), 0, room_.width / 256 - 1);
    const int my = std::clamp(int(state_.y / pixel / 256), 0, room_.height / 256 - 1);
    explored_.insert(my * (room_.width / 256) + mx);
    previous_buttons_ = buttons;
}
void Session::advance(uint64_t timestamp) { clock_.advance(timestamp, [&] { step(buttons_); }); }
RenderSnapshot Session::snapshot(bool interpolate) const {
    const float alpha = interpolate && previous_.generation == state_.generation && !state_.paused ? float(clock_.alpha()) : 1.f;
    auto blend = [&](double a, double b) { return float(a + (b - a) * alpha); };
    return {blend(previous_.x, state_.x) / pixel, blend(previous_.y, state_.y) / pixel,
            blend(previous_.camera_x, state_.camera_x), blend(previous_.camera_y, state_.camera_y),
            state_.pose, state_.frame, state_.generation, state_.tick};
}
}
