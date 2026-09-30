#pragma once
#include "thor/content.hpp"

namespace thor {
struct PowerBeam {
    bool active = false;
    int32_t x = 0, y = 0, previous_x = 0, previous_y = 0, vx = 0, vy = 0;
    uint8_t direction = 2, radius_x = 8, radius_y = 4;
    uint16_t damage = 20;
    uint8_t weapon = 0;
    unsigned age = 0;
};
struct Pickup { size_t root = 0; uint16_t argument = 0, tile = 0; unsigned item = 0, group = 0, frame = 0; bool revealed = true, taken = false; };
struct DoorLock { size_t root = 0; uint16_t argument = 0; unsigned type = 0, orientation = 0, hits = 0; bool released = false; };
struct BlueDoorAnimation {
    size_t block = 0;
    uint32_t list = 0;
    unsigned stage = 0, timer = 0;
    bool closing = false;
};
/** Native translations of basic beam motion and the four blue-door draw lists.
 * No PLM/65816 interpreter; other block scripts and weapons are pending. */
class RoomGameplay {
public:
    void reset();
    void load(Room& room, const Rom& rom, Progression& progression);
    void touch(Room& room, const Rom& rom, int x, int y, int radius);
    void release_grey(Room& room, const Rom& rom, bool boss = false);
    void close_entry(Room& room, const Rom& rom, const Door& entry);
    void draw_cap(Room& room, const Rom& rom, size_t root, unsigned orientation, bool open);
    bool fire(const Rom& rom, int32_t x, int32_t y, unsigned direction, bool new_press, bool running = false, unsigned weapon = 0);
    void tick(Room& room, const Rom& rom, float camera_x, float camera_y);
    const std::array<PowerBeam, 5>& beams() const { return beams_; }
    const std::vector<BlueDoorAnimation>& doors() const { return doors_; }
    bool statue_active() const { return statue_pc_ != 0; }
    void lock_boss(Room& room, const Rom& rom);
    void contact_block(Room& room, const Rom& rom, int x, int y);
    unsigned cooldown() const { return cooldown_; }
    const std::vector<Pickup>& pickups() const { return pickups_; }
    const std::vector<DoorLock>& locks() const { return locks_; }
    std::array<PowerBeam, 5>& beams() { return beams_; }
private:
    bool hit(Room& room, const Rom& rom, int x, int y, const PowerBeam& beam);
    void draw_door(Room& room, const Rom& rom, BlueDoorAnimation& door);
    void draw_list(Room& room, const Rom& rom, size_t root, uint32_t list);
    void item_frame(Room& room, const Rom& rom, Pickup& pickup);
    std::array<PowerBeam, 5> beams_{};
    std::vector<BlueDoorAnimation> doors_;
    unsigned cooldown_ = 0;
    std::vector<Pickup> pickups_;
    std::vector<DoorLock> locks_;
    struct BrokenBlock { size_t root; uint16_t block; unsigned timer, stage; uint32_t list; bool restore; };
    std::vector<BrokenBlock> broken_;
    uint32_t statue_pc_ = 0; size_t statue_root_ = 0; unsigned statue_timer_ = 0;
    Progression* progression_ = nullptr;
};
}
