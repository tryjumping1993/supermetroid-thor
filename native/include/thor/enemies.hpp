#pragma once
#include "thor/gameplay.hpp"

namespace thor {
struct Enemy {
    uint16_t kind = 0, health = 0, damage = 0, radius_x = 0, radius_y = 0;
    uint32_t bank = 0, pc = 0, map = 0, function = 0, movement = 0, link = 0, gut_link = 0;
    int32_t x = 0, y = 0, previous_x = 0, previous_y = 0, vx = 0, vy = 256;
    int home_x = 0, home_y = 0, range = 0;
    unsigned timer = 1, loop = 0, flash = 0, turn = 0, steps = 0, wake = 0;
    uint16_t graphics = 0, behavior = 0;
    bool active = true, visible = true, locked = false, boss = false, sleeping = false, grounded = false, decoration = false;
};
struct EnemyShot { int32_t x = 0, y = 0, vx = 0, vy = 0; unsigned life = 0, type = 0; uint32_t map = 0; uint16_t damage = 0; };
class Enemies {
public:
    void load(const Room& room, const Rom& rom, const Progression& progression);
    // Returns contact damage. Native callbacks consume original animation data;
    // this does not execute 65816 instructions or emulate the game CPU.
    uint16_t tick(Room& room, const Rom& rom, Progression& progression, RoomGameplay& gameplay,
        int samus_x, int samus_y, int samus_radius, float camera_x, float camera_y, uint64_t tick);
    const std::vector<Enemy>& list() const { return enemies_; }
    const std::vector<EnemyShot>& shots() const { return shots_; }
    unsigned killed() const { return killed_; }
private:
    void animate(Enemy& enemy, const Room& room, const Rom& rom, Progression& progression,
        int samus_x, int samus_y, uint64_t tick);
    bool move(Enemy& enemy, const Room& room, const Rom& rom, int32_t distance, bool vertical);
    void attack(Enemy& enemy, const Rom& rom, unsigned type, int parameter = 0);
    std::vector<Enemy> enemies_;
    std::vector<EnemyShot> shots_;
    unsigned killed_ = 0;
};
Image draw_enemy(const Rom& rom, uint16_t kind, uint32_t map, uint16_t behavior = 0);
Image draw_enemy_shot(const Rom& rom, uint32_t map);
}
