#include "thor/session.hpp"
#include "reference_index.hpp"
#include "thor/save_game.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace thor {
namespace { constexpr int32_t pixel = 65536; }
Session::Session(Rom rom) : rom_(std::move(rom)) { new_game(); }
void Session::place_station(const LoadStation& station) {
    auto context = progression_; context.entering_door = station.door;
    auto next = load_room(rom_, station.room_index, context);
    const auto generation = state_.generation + 1;
    room_ = std::move(next); room_index_ = station.room_index; progression_ = context;
    gameplay_.load(room_, rom_, progression_); enemies_.load(room_, rom_, progression_);
    if (!room_.enemy_quota && room_.header != 0x8F9804) gameplay_.release_grey(room_, rom_);
    transition_ = {}; animation_ = {}; elevator_ = {};
    state_ = {}; state_.generation = generation; state_.pose = 1;
    state_.x = station.x * pixel; state_.y = station.y * pixel;
    state_.camera_x = float(station.camera_x); state_.camera_y = float(station.camera_y);
    previous_ = state_; buttons_ = previous_buttons_ = 0; clock_.reset(); explored_.clear();
}
void Session::new_game() { progression_ = {}; place_station(load_station(rom_, 0, 0)); }
void Session::load_game(const Sram& sram, unsigned slot) {
    const auto saved = sram.summary(slot);
    if (!saved.valid) throw std::runtime_error("Save slot is empty or corrupt");
    const auto station = load_station(rom_, saved.area, saved.station);
    auto next = restore_progression(rom_, sram, slot);
    next.entering_door = station.door;
    // Decode before committing the imported save or replacing the running game.
    auto decoded = load_room(rom_, station.room_index, next);
    progression_ = next; place_station(station);
}
int Session::save_station() const {
    if (state_.dead || state_.transition) return -1;
    const int x = state_.x / pixel, y = state_.y / pixel;
    if (room_.header == 0x8F91F8 && std::abs(x - 1152) <= 32 && std::abs(y - 1088) <= 48) return 0;
    for (const auto& object : room_.objects)
        if (object.kind == (reference::PLMEntries_saveStation & 0xFFFF) &&
            std::abs(x - (object.x * 16 + 24)) <= 24 && std::abs(y - (object.y * 16 - 16)) <= 32)
            return object.argument & 7;
    return -1;
}
void Session::save_game(Sram& sram, unsigned slot) {
    const int station = save_station();
    if (station < 0) throw std::runtime_error("Stand at a save station or the ship to save");
    progression_.health = progression_.max_health; progression_.missiles = progression_.max_missiles;
    save_progression(rom_, sram, slot, progression_, unsigned(room_.area), unsigned(station));
}
void Session::select_room(size_t index) {
    auto context = progression_; context.entering_door = 0;
    auto next = load_room(rom_, index, context); // Complete decode before replacing current session.
    const auto generation = state_.generation + 1;
    room_ = std::move(next); room_index_ = index; progression_ = context;
    gameplay_.load(room_, rom_, progression_); enemies_.load(room_, rom_, progression_);
    if (!room_.enemy_quota && room_.header != 0x8F9804) gameplay_.release_grey(room_, rom_);
    transition_ = {}; animation_ = {}; elevator_ = {};
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
    gameplay_.load(room_, rom_, progression_); enemies_.load(room_, rom_, progression_);
    if (!room_.enemy_quota && room_.header != 0x8F9804) gameplay_.release_grey(room_, rom_);
    transition_ = {}; animation_ = {}; elevator_ = {};
    previous_ = state_ = arrived; buttons_ = previous_buttons_ = 0; clock_.reset();
    explored_.clear(); explored_.insert((y / 256) * (room_.width / 256) + x / 256);
}
bool Session::solid(int x, int y) const {
    const auto root = reaction_block(room_, x, y);
    for (const auto& item : gameplay_.pickups()) if (item.root == root && (item.revealed || item.taken)) return false;
    return room_solid_pixel(room_, rom_, x, y);
}
bool Session::collides(int32_t x, int32_t y) const {
    const int px = int(std::floor(double(x) / pixel)), py = int(std::floor(double(y) / pixel));
    const int radius = pose_radius(rom_, state_.pose);
    for (int cy = py - radius; cy < py + radius; ++cy)
        if (solid(px - 5, cy) || solid(px + 4, cy)) return true;
    for (int cx = px - 5; cx <= px + 4; ++cx)
        if (solid(cx, py - radius) || solid(cx, py + radius - 1)) return true;
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
        if (touch_door(x, y, vertical, direction)) { position += step; return; }
        if (collides(x, y)) {
            // $94 slope alignment: grounded horizontal travel follows the
            // sampled boundary instead of treating an incline as a wall.
            if (!vertical && state_.grounded) {
                for (int lift = 1; lift <= 3; ++lift) if (!collides(x, y - lift * pixel)) {
                    state_.y -= lift * pixel; position += step; remaining -= std::abs(step); goto moved;
                }
            }
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
        moved:;
    }
    if (position == start && vertical && delta > 0) state_.grounded = true;
}
void Session::pose(uint16_t next) {
    if (next > 252 || next == state_.pose) return;
    // $91:F88C: vanilla's default disables moonwalk, converting it to turn.
    if (movement_type(rom_, next) == 16) next = rom_.byte(reference::PoseDefinitions + next * 8) == 4 ? 0x25 : 0x26;
    const auto type = movement_type(rom_, next);
    if ((type == 4 || type == 8 || next == 0x37 || next == 0x38) && !(progression_.equipped_items & 4)) return;
    const auto old_radius = pose_radius(rom_, state_.pose), new_radius = pose_radius(rom_, next);
    const auto old_pose = state_.pose;
    const auto adjusted_y = state_.y + (state_.grounded ? int(old_radius) - int(new_radius) : 0) * pixel;
    state_.pose = next;
    if (new_radius > old_radius && collides(state_.x, adjusted_y)) { state_.pose = old_pose; return; }
    state_.y = adjusted_y; animation_.pose = next; animation_.frame = state_.frame = 0;
    const auto table = 0x910000u | rom_.word(reference::AnimationDelayTable + next * 2);
    const auto initial = rom_.byte(table); animation_.timer = initial < 0x80 ? std::max<unsigned>(initial, 1) : 1;
}
void Session::camera() {
    const int columns = room_.width / 256, rows = room_.height / 256;
    const int sx = std::clamp(state_.x / pixel / 256, 0, columns - 1), sy = std::clamp(state_.y / pixel / 256, 0, rows - 1);
    auto allowed = [&](int x, int y) { return room_.scrolls.at(size_t(y) * columns + x) != 0; };
    int left = sx, right = sx, top = sy, bottom = sy;
    while (left > 0 && allowed(left - 1, sy)) --left;
    while (right + 1 < columns && allowed(right + 1, sy)) ++right;
    while (top > 0 && allowed(sx, top - 1)) --top;
    while (bottom + 1 < rows && allowed(sx, bottom + 1)) ++bottom;
    state_.camera_x = std::clamp(float(state_.x / pixel - 128), float(left * 256), float(right * 256));
    const float bottom_limit = float(bottom * 256 + (room_.scrolls[size_t(bottom) * columns + sx] == 2 ? 32 : 0));
    state_.camera_y = std::clamp(float(state_.y / pixel - 112), float(top * 256), bottom_limit);
    explored_.insert(sy * columns + sx);
    // $80:map bit layout: two 32-column halves, MSB is the leftmost cell.
    const int mx = room_.map_x + sx, my = room_.map_y + sy;
    if (mx >= 0 && mx < 64 && my >= 0 && my < 32)
        progression_.map[size_t(room_.area) * 256 + (mx / 32) * 128 + my * 4 + (mx % 32) / 8] |= uint8_t(0x80 >> (mx % 8));
}
bool Session::touch_door(int32_t x, int32_t y, bool vertical, int direction) {
    const int px = x / pixel, py = y / pixel, radius = pose_radius(rom_, state_.pose);
    auto check = [&](int cx, int cy) {
        const auto index = reaction_block(room_, cx, cy);
        if (index >= room_.blocks.size() || (room_.blocks[index] >> 12) != 9) return false;
        const auto exit = room_.bts[index] & 0x7F;
        if (size_t(exit) >= room_.doors.size()) return false;
        const auto& door = room_.doors[exit];
        if (!door.destination || bool(door.direction & 2) != vertical ||
            int((door.direction & 1) ? -1 : 1) != direction) return false;
        begin_door(size_t(exit)); return true;
    };
    if (vertical) { for (int cx = px - 5; cx < px + 5; ++cx) if (check(cx, py + (direction > 0 ? radius - 1 : -radius))) return true; }
    else { for (int cy = py - radius; cy < py + radius; ++cy) if (check(px + (direction > 0 ? 4 : -5), cy)) return true; }
    return false;
}
void Session::begin_door(size_t index) {
    if (state_.transition) return;
    transition_ = {}; transition_.door = room_.doors.at(index); transition_.phase = 1;
    transition_.timer = (transition_.door.properties & 0x80) && !(transition_.door.direction & 1) ? 48 : 0;
    state_.transition = 1; gameplay_.reset();
}
void Session::door_tick() {
    auto& t = transition_; const auto direction = t.door.direction & 3;
    if (t.timer) { --t.timer; return; }
    if (t.phase == 1) {
        float& axis = (direction & 2) ? state_.camera_x : state_.camera_y;
        const float target = std::round(axis / 256) * 256;
        if (axis != target) { axis += std::clamp(target - axis, -1.f, 1.f); return; }
        t.source_x = state_.camera_x; t.source_y = state_.camera_y;
        auto context = progression_; context.entering_door = uint16_t(t.door.address);
        auto next = load_room(rom_, t.door.destination_index, context);
        const int cx = t.door.screen_x * 256, cy = t.door.screen_y * 256;
        state_.camera_x = float(cx + (direction == 0 ? -256 : direction == 1 ? 256 : 0));
        state_.camera_y = float(cy + (direction == 2 ? -224 : direction == 3 ? 255 : 0));
        state_.x = ((state_.x / pixel & 255) * pixel) + int32_t(state_.camera_x * pixel);
        state_.y = ((state_.y / pixel & 255) * pixel) + int32_t(state_.camera_y * pixel);
        room_ = std::move(next); room_index_ = t.door.destination_index; progression_ = context;
        ++state_.generation; state_.transition = t.phase = 2; explored_.clear(); gameplay_.load(room_, rom_, progression_); enemies_.load(room_, rom_, progression_);
    if (!room_.enemy_quota && room_.header != 0x8F9804) gameplay_.release_grey(room_, rom_);
        return;
    }
    if (t.phase == 2) {
        const auto raw = t.door.transition_speed;
        const int32_t speed = int32_t(raw & 0x8000 ? ((direction & 2) ? 0x180 : 0xC8) : raw) * 256;
        if (!(direction & 2)) {
            state_.x += (direction == 0 ? speed : -speed); state_.camera_x += direction == 0 ? 4 : -4;
        } else if (t.scroll) {
            state_.y += direction == 2 ? speed : -speed; state_.camera_y += direction == 2 ? 4 : -4;
        }
        if (++t.scroll >= ((direction & 2) ? 57u : 64u)) {
            state_.camera_x = float(t.door.screen_x * 256);
            state_.camera_y = float(t.door.screen_y * 256 + (direction == 3 ? 32 : 0));
            int x = state_.x / pixel;
            if (!(direction & 2)) x = direction == 0 ? x | 7 : x & ~7;
            if ((x & 0xF0) == 0x10) x = (x | 15) + 8;
            else if ((x & 0xF0) == 0xE0) x = (x & ~15) - 8;
            state_.x = x * pixel + (state_.x & 0xFFFF);
            int y = state_.y / pixel;
            if ((y & 0xF0) == 0x10) y = (y | 15) + 8;
            state_.y = y * pixel + (state_.y & 0xFFFF);
            gameplay_.close_entry(room_, rom_, t.door);
            state_.transition = t.phase = 3; t.timer = 12;
        }
        return;
    }
    if (t.phase == 3) {
        state_.transition = 0; state_.vx = state_.vy = 0;
        if (t.door.properties & 0x80) for (const auto& enemy : room_.enemies)
            if (enemy.kind == (reference::EnemyHeaders_Elevator & 0xFFFF)) {
                elevator_ = {true, true, enemy.parameter1 ? 1 : -1, int(enemy.x),
                    state_.y + 26 * pixel, int32_t(enemy.y) * pixel};
                state_.x = int32_t(enemy.x) * pixel; break;
            }
        t = {};
    }
}
bool Session::elevator_tick(uint16_t pressed) {
    if (!elevator_.active) for (const auto& enemy : room_.enemies) {
        if (enemy.kind != (reference::EnemyHeaders_Elevator & 0xFFFF)) continue;
        const uint16_t input = enemy.parameter1 ? Up : Down;
        if ((pressed & input) && std::abs(state_.x / pixel - enemy.x) < 16 &&
            std::abs(state_.y / pixel - (int(enemy.y) - 26)) < 16) {
            elevator_ = {true, false, enemy.parameter1 ? -1 : 1, int(enemy.x), int32_t(enemy.y) * pixel, 0};
            pose(0); state_.vx = state_.vy = 0; break;
        }
    }
    if (!elevator_.active) return false;
    elevator_.y += elevator_.direction * (pixel + pixel / 2);
    state_.x = elevator_.x * pixel; state_.y = (elevator_.y / pixel - 26) * pixel;
    state_.grounded = true;
    if (elevator_.arriving) {
        if ((elevator_.direction > 0 && elevator_.y >= elevator_.target) ||
            (elevator_.direction < 0 && elevator_.y <= elevator_.target)) {
            state_.y = elevator_.target - 26 * pixel; elevator_.active = false; pose(1);
        }
    } else if (!touch_door(state_.x, state_.y, true, elevator_.direction)) {
        // Elevator trigger screens can end at the boundary rather than expose
        // a cap; resolve only the original special link in the ride direction.
        if (state_.y / pixel < 0 || state_.y / pixel >= room_.height - 21)
            for (size_t i = 0; i < room_.doors.size(); ++i) {
                const auto& door = room_.doors[i];
                if ((door.properties & 0x80) && (door.direction & 2) &&
                    ((door.direction & 1) ? -1 : 1) == elevator_.direction) { begin_door(i); break; }
            }
    }
    return true;
}
void Session::step(uint16_t buttons) {
    if (state_.paused) { previous_ = state_; previous_buttons_ = buttons; return; }
    previous_ = state_; ++state_.tick;
    const uint16_t pressed = buttons & ~previous_buttons_;
    previous_buttons_ = buttons;
    if (state_.transition) {
        if (state_.transition == 3) gameplay_.tick(room_, rom_, state_.camera_x, state_.camera_y);
        door_tick(); return;
    }
    if (state_.dead) return;
    if (elevator_tick(pressed)) { camera(); return; }
    if (state_.invulnerable) --state_.invulnerable;
    if ((pressed & Select) && progression_.max_missiles) state_.missile_selected = !state_.missile_selected;
    gameplay_.tick(room_, rom_, state_.camera_x, state_.camera_y);
    const bool was_grounded = state_.grounded;
    const auto old_type = movement_type(rom_, state_.pose);
    // $91 controller normalization: jump=A, fire=X, run=B, aim L/R.
    auto normalized = [](uint16_t value) {
        return uint16_t((value & 0x3F40) | ((value & Jump) ? 0x80 : 0) |
            ((value & Run) ? 0x8000 : 0) | ((value & AimUp) ? 0x10 : 0) | ((value & AimDown) ? 0x20 : 0));
    };
    pose(input_pose(rom_, state_.pose, normalized(buttons), normalized(pressed), state_.vx));
    auto type = movement_type(rom_, state_.pose);
    const int direction = bool(buttons & Right) - bool(buttons & Left);
    const uint32_t table = reference::SamusXSpeedTable_Normal + type * 12;
    auto fixed = [&](uint32_t at) { return int32_t((uint32_t(rom_.word(at)) << 16) | rom_.word(at + 2)); };
    const int32_t acceleration = fixed(table), maximum = fixed(table + 4), deceleration = fixed(table + 8);
    if (direction && maximum) state_.vx = std::clamp(state_.vx + direction * acceleration, -maximum, maximum);
    else state_.vx = (state_.vx < 0 ? -1 : 1) * std::max(0, std::abs(state_.vx) - deceleration);
    const bool was_ball = old_type == 4 || old_type == 8;
    if ((pressed & Jump) && was_grounded && (!was_ball || (progression_.equipped_items & 2))) {
        // Speed and sub-speed constants are separate tables.
        state_.vy = -int32_t((uint32_t(rom_.word(reference::SamusPhysicsConstants_InitialYSpeeds_Jumping)) << 16) |
            rom_.word(reference::SamusPhysicsConstants_InitialYSubSpeeds_Jumping));
        const bool right = rom_.byte(reference::PoseDefinitions + state_.pose * 8) == 8;
        if (type == 4 || type == 8) pose(right ? 0x1D : 0x1E);
        else pose(direction ? (right ? 0x19 : 0x1A) : (right ? 0x13 : 0x14));
    }
    if ((buttons & Run) && direction && type == 1) {
        const auto acceleration = (uint32_t(rom_.word(reference::SamusPhysicsConstants_XAccelerations_DashHeld)) << 16) | rom_.word(reference::SamusPhysicsConstants_XSubAccelerations_DashHeld);
        const auto maximum = (uint32_t(rom_.word(reference::SamusPhysicsConstants_MaxXExtraRunSpeeds_NoSpeedBooster)) << 16) | rom_.word(reference::SamusPhysicsConstants_MaxXExtraRunSubSpeeds_NoSpeedBooster);
        state_.extra_run = std::min(state_.extra_run + int32_t(acceleration), int32_t(maximum));
    } else if (was_grounded && !(pressed & Jump)) state_.extra_run = 0;
    if (was_ball) for (const auto& bomb : gameplay_.beams())
        if (bomb.active && bomb.weapon == 2 && bomb.age == rom_.word(reference::BombTimerResetValue) &&
            std::abs(bomb.x - state_.x) < 16 * pixel && std::abs(bomb.y - state_.y) < 16 * pixel) {
            state_.vy = -int32_t((uint32_t(rom_.word(reference::SamusPhysicsConstants_InitialYSpeeds_BombJump)) << 16) | rom_.word(reference::SamusPhysicsConstants_InitialYSubSpeeds_BombJump));
            state_.grounded = false;
        }
    // $90 variable-height jump: releasing jump cancels upward speed.
    if (!(buttons & Jump) && state_.vy < 0 && old_type != 4 && old_type != 8) state_.vy = 0;
    const auto gravity = (uint32_t(rom_.word(reference::SamusPhysicsConstants_YAccelerationInAir)) << 16) |
                        rom_.word(reference::SamusPhysicsConstants_YSubAccelerationInAir);
    state_.vy = std::min(state_.vy + int32_t(gravity), 5 * pixel);
    if (state_.grounded) {
        gameplay_.contact_block(room_, rom_, state_.x / pixel - 5, state_.y / pixel + pose_radius(rom_, state_.pose));
        gameplay_.contact_block(room_, rom_, state_.x / pixel + 4, state_.y / pixel + pose_radius(rom_, state_.pose));
    }
    move_axis(state_.vx + (state_.vx < 0 ? -state_.extra_run : state_.extra_run), false);
    if (state_.transition) return;
    state_.grounded = false; move_axis(state_.vy, true);
    if (state_.transition) return;
    type = movement_type(rom_, state_.pose);
    const bool right = rom_.byte(reference::PoseDefinitions + state_.pose * 8) == 8;
    if (state_.grounded && !was_grounded) {
        if (type == 4 || type == 8) pose(right ? 0x1D : 0x1E);
        else if (type == 2 || type == 3 || type == 6) pose(direction ? (right ? 9 : 10) : (right ? 1 : 2));
    } else if (!state_.grounded && was_grounded && state_.vy >= 0 && type != 4 && type != 8)
        pose(right ? 0x29 : 0x2A);
    animate_samus(rom_, animation_, progression_.equipped_items, state_.grounded, bool(buttons & Jump));
    if (animation_.pose != state_.pose) pose(animation_.pose);
    state_.frame = animation_.frame;
    gameplay_.touch(room_, rom_, state_.x / pixel, state_.y / pixel, pose_radius(rom_, state_.pose));
    if (buttons & Shoot) {
        type = movement_type(rom_, state_.pose);
        const bool ball = type == 4 || type == 8;
        const unsigned weapon = ball ? 2 : state_.missile_selected ? 1 : 0;
        const auto aim = rom_.byte(reference::PoseDefinitions + state_.pose * 8 + 3);
        if ((ball && (progression_.equipped_items & 0x1000)) ||
            (!ball && aim < 10 && (weapon != 1 || progression_.missiles))) {
            const auto offset_y = rom_.byte(reference::PoseDefinitions_YOffset + state_.pose * 8);
            if (gameplay_.fire(rom_, state_.x, state_.y - offset_y * pixel, ball ? 2 : aim,
                bool(pressed & Shoot), type == 1, weapon) && weapon == 1 && !--progression_.missiles)
                state_.missile_selected = false; // $91 selection reverts to Power Beam when empty.
        }
    }
    const auto damage = enemies_.tick(room_, rom_, progression_, gameplay_, state_.x / pixel, state_.y / pixel,
        pose_radius(rom_, state_.pose), state_.camera_x, state_.camera_y, state_.tick);
    if (damage && !state_.invulnerable) {
        progression_.health = uint16_t(std::max(0, int(progression_.health) - damage));
        state_.invulnerable = 96; state_.vy = -3 * pixel; state_.dead = !progression_.health;
    }
    camera();
}
void Session::advance(uint64_t timestamp) { clock_.advance(timestamp, [&] { step(buttons_); }); }
RenderSnapshot Session::snapshot(bool interpolate) const {
    const float alpha = interpolate && previous_.generation == state_.generation && !state_.paused ? float(clock_.alpha()) : 1.f;
    auto blend = [&](double a, double b) { return float(a + (b - a) * alpha); };
    RenderSnapshot result{blend(previous_.x, state_.x) / pixel, blend(previous_.y, state_.y) / pixel,
            blend(previous_.camera_x, state_.camera_x), blend(previous_.camera_y, state_.camera_y),
            state_.pose, state_.frame, state_.generation, state_.tick, {}, {}, {}};
    for (const auto& beam : gameplay_.beams()) if (beam.active)
        result.beams.push_back({blend(beam.previous_x, beam.x) / pixel,
            blend(beam.previous_y, beam.y) / pixel, beam.direction, beam.weapon, uint16_t(beam.age)});
    result.samus_visible = !state_.dead && !(state_.invulnerable && state_.tick & 2);
    for (const auto& enemy : enemies_.list()) if (enemy.active && enemy.visible && !enemy.sleeping && enemy.map)
        result.enemies.push_back({blend(enemy.previous_x, enemy.x) / pixel, blend(enemy.previous_y, enemy.y) / pixel,
            enemy.map, enemy.kind, enemy.behavior, bool(enemy.flash && state_.tick & 2)});
    if (elevator_.active) for (auto& enemy : result.enemies) if (enemy.kind == (reference::EnemyHeaders_Elevator & 0xFFFF)) {
        enemy.x = result.x; enemy.y = result.y + 26;
    }
    for (const auto& shot : enemies_.shots()) result.enemy_shots.push_back({shot.x / float(pixel), shot.y / float(pixel), shot.map});
    if (state_.transition == 2) {
        result.transition_direction = uint8_t((transition_.door.direction & 3) + 1);
        result.transition_progress = float(transition_.scroll) / ((transition_.door.direction & 2) ? 57 : 64);
        result.source_x = transition_.source_x; result.source_y = transition_.source_y;
    }
    return result;
}
}
