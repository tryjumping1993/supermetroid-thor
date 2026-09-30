#pragma once
#include "thor/clock.hpp"
#include "thor/content.hpp"
#include "thor/gameplay.hpp"
#include "thor/enemies.hpp"
#include "thor/game.hpp"
#include "thor/samus.hpp"
#include "thor/sram.hpp"
#include <array>
#include <memory>
#include <set>

namespace thor {
enum Button : uint16_t { AimDown = 0x10, AimUp = 0x20, Shoot = 0x40, Right = 0x100, Left = 0x200, Down = 0x400, Up = 0x800, Start = 0x1000, Select = 0x2000, Run = 0x4000, Jump = 0x8000 };
struct State {
    uint64_t tick = 0;
    int32_t x = 0, y = 0, vx = 0, vy = 0, extra_run = 0; // 16.16 fixed-point
    float camera_x = 0, camera_y = 0;
    uint32_t generation = 0;
    uint16_t pose = 1, frame = 0;
    bool grounded = false, paused = false;
    bool missile_selected = false, dead = false;
    uint16_t invulnerable = 0;
    uint8_t transition = 0;
};
struct BeamSnapshot { float x = 0, y = 0; uint8_t direction = 2, weapon = 0; uint16_t age = 0; };
// `map` is the 24-bit spritemap address. Ported (WRAM-driven) enemies also carry their tile slot,
// palette bits and whether the spritemap is the extended format.
struct EnemySnapshot { float x, y; uint32_t map; uint16_t kind, behavior; bool flash;
    bool ported = false, extended = false; uint16_t gfx_offset = 0, palette = 0; bool frozen = false; };
struct EnemyShotSnapshot { float x, y; uint32_t map; };
struct RenderSnapshot {
    float x = 0, y = 0, camera_x = 0, camera_y = 0;
    uint16_t pose = 1, frame = 0;
    uint32_t generation = 0;
    uint64_t tick = 0;
    std::vector<BeamSnapshot> beams;
    std::vector<EnemySnapshot> enemies;
    std::vector<EnemyShotSnapshot> enemy_shots;
    bool samus_visible = true;
    uint8_t transition_direction = 0;
    float transition_progress = 0, source_x = 0, source_y = 0;
};
class Session {
public:
    explicit Session(Rom rom);
    Session(const Session& other);
    Session& operator=(const Session& other);
    Session(Session&& other) noexcept;
    Session& operator=(Session&& other) noexcept;
    void new_game();
    void load_game(const Sram& sram, unsigned slot);
    void save_game(Sram& sram, unsigned slot);
    int save_station() const;
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
    const RoomGameplay& gameplay() const { return gameplay_; }
    const std::set<int>& explored() const { return explored_; }
    const Enemies& enemies() const { return enemies_; }
    const Game& game() const { return game_; }
    const Progression& progression() const { return progression_; }
    double alpha() const { return clock_.alpha(); }
private:
    bool solid(int x, int y) const;
    bool collides(int32_t x, int32_t y) const;
    void move_axis(int32_t delta, bool vertical);
    void pose(uint16_t next);
    void camera();
    bool touch_door(int32_t x, int32_t y, bool vertical, int direction);
    void begin_door(size_t index);
    void door_tick();
    void place_station(const LoadStation& station);
    void load_actors();                 // gameplay objects, legacy enemies and the ported enemy engine
    void sync_game_in();                // Session state -> WRAM
    void sync_game_out();               // WRAM -> Session state
    void run_enemy_engine();
    struct Transition { Door door; unsigned phase = 0, timer = 0, scroll = 0; float source_x = 0, source_y = 0; } transition_;
    struct Elevator { bool active = false, arriving = false; int direction = 0, x = 0; int32_t y = 0, target = 0; } elevator_;
    bool elevator_tick(uint16_t pressed);
    SamusAnimation animation_;
    Rom rom_;
    Progression progression_;
    Room room_;
    RoomGameplay gameplay_;
    Enemies enemies_;
    mutable Game game_;   // accessors are non-const proxies; the snapshot only reads
    std::array<std::array<int32_t, 2>, 32> previous_enemy_;
    size_t room_index_ = 0;
    State previous_, state_;
    TickClock clock_;
    uint16_t buttons_ = 0, previous_buttons_ = 0;
    std::set<int> explored_;
};
}
