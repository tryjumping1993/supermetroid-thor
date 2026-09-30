#include "thor/gameplay.hpp"
#include "reference_index.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <set>

namespace thor {
namespace { constexpr int32_t pixel = 65536; }
void RoomGameplay::reset() { beams_ = {}; doors_.clear(); cooldown_ = 0; pickups_.clear(); locks_.clear(); progression_ = nullptr; broken_.clear(); statue_pc_ = 0; statue_timer_ = 0; }
void RoomGameplay::draw_list(Room& room, const Rom& rom, size_t root, uint32_t list) {
    int x = 0, y = 0;
    for (unsigned record = 0; record < 64; ++record) {
        const auto count = rom.word(list); list += 2; if (!count) { ++room.visual_revision; return; }
        if ((count & 0x7FFF) > 64) throw std::runtime_error("Invalid object draw list");
        for (unsigned i = 0; i < (count & 0x7FFF); ++i) {
            const int index = int(root) + (y + ((count & 0x8000) ? int(i) : 0)) * (room.width / 16) + x + ((count & 0x8000) ? 0 : int(i));
            if (index < 0 || size_t(index) >= room.blocks.size()) throw std::runtime_error("Object draw outside geometry");
            room.blocks[index] = rom.word(list); list += 2; redraw_block(room, size_t(index));
        }
        if (!rom.word(list)) { ++room.visual_revision; return; }
        x = int8_t(rom.byte(list)); y = int8_t(rom.byte(list + 1)); list += 2;
    }
    throw std::runtime_error("Unbounded object draw list");
}
void RoomGameplay::lock_boss(Room& room, const Rom& rom) {
    for (auto& lock : locks_) if (lock.type == 0 && (lock.argument >> 12) == 0 && !progression_->bosses[room.area]) {
        lock.released = false;
        draw_list(room, rom, lock.root, reference::DrawInst_GreyDoorFacingLeft_0 + lock.orientation * 48);
        room.bts[lock.root] = 0x44;
    }
}
void RoomGameplay::contact_block(Room& room, const Rom& rom, int x, int y) {
    const auto root = reaction_block(room, x, y);
    if (root >= room.blocks.size() || (room.blocks[root] >> 12) != 11 || room.bts[root] >= 8) return;
    const auto old = room.blocks[root]; room.blocks[root] = 0xFF; redraw_block(room, root); ++room.visual_revision;
    if (room.bts[root] < 4) {
        const auto list = reference::InstList_PLM_1x1RespawningCrumbleBlock + 3;
        broken_.push_back({root, old, rom.word(list), 0, list, true});
        draw_list(room, rom, root, 0x840000u | rom.word(list + 2));
    }
}
void RoomGameplay::item_frame(Room& room, const Rom& rom, Pickup& item) {
    uint16_t block = 0;
    if (item.taken) block = rom.word(reference::DrawInst_ItemChozoOrb + 2);
    else if (!item.revealed) block = rom.word(reference::DrawInst_ItemOrb_0 + 2);
    else block = uint16_t(0xB000 | (item.tile + item.frame % 2));
    room.blocks.at(item.root) = block; room.bts.at(item.root) = 0x45; redraw_block(room, item.root); ++room.visual_revision;
}
void RoomGameplay::load(Room& room, const Rom& rom, Progression& p) {
    reset(); progression_ = &p; unsigned graphics_slot = 0;
    for (const auto& object : room.objects) {
        const size_t root = size_t(object.y) * (room.width / 16) + object.x;
        if (root >= room.blocks.size()) throw std::runtime_error("Room object lies outside geometry");
        const auto kind = object.kind;
        if (kind == (reference::PLMEntries_BombTorizosCrumblingChozo & 0xFFFF)) {
            statue_root_ = root; statue_pc_ = reference::InstList_PLM_BombTorizosCrumblingChozo;
            draw_list(room, rom, root, reference::DrawInst_BombTorizosCrumblingChozo_0); continue;
        }
        unsigned type = 0; int offset = 0;
        if (kind == (reference::PLMEntries_rightwardsExtension & 0xFFFF)) { type = 5; offset = -1; }
        if (kind == (reference::PLMEntries_leftwardsExtension & 0xFFFF)) { type = 5; offset = 1; }
        if (kind == (reference::PLMEntries_downwardsExtension & 0xFFFF)) { type = 13; offset = -1; }
        if (kind == (reference::PLMEntries_upwardsExtension & 0xFFFF)) { type = 13; offset = 1; }
        if (type) { room.blocks[root] = uint16_t((room.blocks[root] & 0xFFF) | type << 12); room.bts[root] = uint8_t(offset); continue; }
        if (kind == (reference::PLMEntries_ScrollPLM & 0xFFFF)) { room.blocks[root] = (room.blocks[root] & 0xFFF) | 0x3000; room.bts[root] = 0x46; continue; }
        if ((kind >= 0xC842 && kind <= 0xC89C && (kind - 0xC842) % 6 == 0) || kind == 0xBAF4) {
            const unsigned group = kind == 0xBAF4 ? 0 : (kind - 0xC842) / 24;
            const unsigned orientation = kind == 0xBAF4 ? 1 : ((kind - 0xC842) / 6) % 4;
            DoorLock lock{root, object.argument, group, orientation, 0, p.opened_doors.test(object.argument & 0x1FF)};
            locks_.push_back(lock);
            draw_cap(room, rom, root, orientation, false);
            if (!lock.released && kind != 0xBAF4) {
                constexpr uint32_t closed[]{reference::DrawInst_GreyDoorFacingLeft_0, reference::DrawInst_YellowDoorFacingLeft_0,
                    reference::DrawInst_GreenDoorFacingLeft_0, reference::DrawInst_RedDoorFacingLeft_0};
                const auto at = closed[group] + orientation * 48;
                const size_t stride = orientation < 2 ? size_t(room.width / 16) : 1;
                for (size_t i = 0; i < 4; ++i) { room.blocks.at(root + i * stride) = rom.word(at + 2 + i * 2); redraw_block(room, root + i * stride); }
                room.bts[root] = 0x44;
            }
            continue;
        }
        if (kind < 0xEED7 || kind > 0xEFD3 || (kind - 0xEED7) % 4) continue;
        const unsigned item = ((kind - 0xEED7) / 4) % 21, group = ((kind - 0xEED7) / 4) / 21;
        if (item != 0 && item != 1 && item != 4 && item != 19) continue;
        Pickup pickup{root, object.argument, uint16_t(item == 0 ? 0x48 : 0x4C), item, group, 0,
            group == 0 || p.destroyed_chozo.test(object.argument & 0x1FF), p.items.test(object.argument & 0x1FF)};
        if (item == 4 || item == 19) {
            const auto tiles = rom.slice(item == 4 ? reference::ItemPLMGFX_Bombs : reference::ItemPLMGFX_MorphBall, 256);
            const size_t slot = graphics_slot++ % 4;
            std::copy(tiles.begin(), tiles.end(), room.tiles.begin() + 0x7C00 + slot * 256);
            for (unsigned i = 0; i < 8; ++i) {
                const unsigned tile = 0x3E0 + unsigned(slot) * 8 + i, at = 0x470 + unsigned(slot) * 16 + i * 2;
                room.tiletable[at] = uint8_t(tile); room.tiletable[at + 1] = uint8_t(tile >> 8);
            }
            pickup.tile = uint16_t(0x8E + slot * 2);
        }
        pickups_.push_back(pickup); item_frame(room, rom, pickups_.back());
    }
}
void RoomGameplay::touch(Room& room, const Rom& rom, int x, int y, int radius) {
    if (!progression_) return;
    for (auto& item : pickups_) {
        const int ix = int(item.root % (room.width / 16)) * 16 + 8, iy = int(item.root / (room.width / 16)) * 16 + 8;
        if (item.taken || !item.revealed || std::abs(x - ix) >= 13 || std::abs(y - iy) >= radius + 8) continue;
        item.taken = true; progression_->items.set(item.argument & 0x1FF);
        if (item.item == 0) { progression_->max_health += 100; progression_->health = progression_->max_health; }
        if (item.item == 1) { progression_->max_missiles += 5; progression_->missiles += 5; }
        if (item.item == 4 || item.item == 19) {
            const uint16_t equipment = item.item == 4 ? 0x1000 : 4;
            progression_->collected_items |= equipment; progression_->equipped_items |= equipment;
        }
        item_frame(room, rom, item);
    }
    for (const auto& object : room.objects) {
        if (object.kind != (reference::PLMEntries_ScrollPLM & 0xFFFF)) continue;
        const auto root = size_t(object.y) * (room.width / 16) + object.x;
        bool touches = false;
        for (int cy : {y - radius, y, y + radius - 1}) for (int cx : {x - 5, x + 4})
            if (reaction_block(room, cx, cy) == root) touches = true;
        if (touches) for (unsigned i = 0; i < 128; ++i) {
            const auto at = (0x8F0000u | object.argument) + i * 2;
            const auto screen = rom.byte(at); if (screen == 0x80) break;
            if (screen >= room.scrolls.size()) throw std::runtime_error("Scroll PLM references invalid screen");
            room.scrolls[screen] = rom.byte(at + 1);
        }
    }
}
void RoomGameplay::release_grey(Room& room, const Rom& rom, bool boss) {
    for (auto& lock : locks_) if (lock.type == 0 && !lock.released &&
        (boss || (lock.argument >> 12) == 9 || (lock.argument >> 12) == 0)) {
        lock.released = true; draw_cap(room, rom, lock.root, lock.orientation, false);
    }
}
void RoomGameplay::draw_cap(Room& room, const Rom& rom, size_t root, unsigned orientation, bool open) {
    constexpr uint32_t opened[]{reference::DrawInst_DoorFacingLeft_A677, reference::DrawInst_DoorFacingRight_A683,
        reference::DrawInst_DoorFacingUp_A68F, reference::DrawInst_DoorFacingDown_A69B};
    constexpr uint32_t closed[]{reference::DrawInst_DoorFacingLeft_A9B3, reference::DrawInst_DoorFacingRight_A9EF,
        reference::DrawInst_DoorFacingUp_AA2B, reference::DrawInst_DoorFacingDown_AA67};
    if (orientation > 3) throw std::runtime_error("Invalid cap orientation");
    const auto at = open ? opened[orientation] : closed[orientation];
    const size_t stride = (rom.word(at) & 0x8000) ? size_t(room.width / 16) : 1;
    for (size_t i = 0; i < 4; ++i) { room.blocks.at(root + stride * i) = rom.word(at + 2 + i * 2); redraw_block(room, root + stride * i); }
    room.bts.at(root) = uint8_t(0x40 + orientation); ++room.visual_revision;
}
void RoomGameplay::close_entry(Room& room, const Rom& rom, const Door& entry) {
    if (entry.properties & 0x80) return;
    const auto root = size_t(entry.cap_y) * (room.width / 16) + entry.cap_x;
    constexpr uint32_t closing[]{reference::InstList_PLM_BlueDoorFacingLeftClosed, reference::InstList_PLM_BlueDoorFacingRightClosed,
        reference::InstList_PLM_BlueDoorFacingUpClosed_42, reference::InstList_PLM_BlueDoorFacingUpClosed_43};
    const unsigned orientation = (entry.direction & 3) ^ 1;
    room.bts.at(root) = uint8_t(0x40 + orientation);
    doors_.push_back({root, closing[orientation], 0, 0, true}); draw_door(room, rom, doors_.back());
}
bool RoomGameplay::fire(const Rom& rom, int32_t x, int32_t y, unsigned direction, bool new_press, bool running, unsigned weapon) {
    if (direction > 9 || cooldown_) return false;
    auto slot = std::find_if(beams_.begin(), beams_.end(), [](const auto& beam) { return !beam.active; });
    if (slot == beams_.end()) return false;
    const auto data = weapon == 1 ? reference::ProjectileDataTable_NonBeam_Missile : reference::ProjectileDataTable_Uncharged_Power;
    const auto list = 0x930000u | rom.word(data + 2 + direction * 2);
    const int32_t speed = int32_t(rom.word((direction == 1 || direction == 3 || direction == 6 || direction == 8)
        ? reference::BeamSpeeds_Diagonal : reference::BeamSpeeds_Horizontal_Vertical)) * 256; // 8.8 -> 16.16
    constexpr int dx[]{0, 1, 1, 1, 0, 0, -1, -1, -1, 0};
    constexpr int dy[]{-1, -1, 0, 1, 1, 1, 1, 0, -1, -1};
    *slot = {};
    slot->active = true; slot->direction = uint8_t(direction);
    const auto origin_x = running ? reference::ProjectileOriginOffsetsByDirection_ProjX_Moonwalk_Running : reference::ProjectileOriginOffsetsByDirection_ProjectileX_Default;
    const auto origin_y = running ? reference::ProjectileOriginOffsetsByDirection_ProjY_Moonwalk_Running : reference::ProjectileOriginOffsetsByDirection_ProjectileY_Default;
    slot->x = x + int16_t(rom.word(origin_x + direction * 2)) * pixel;
    slot->y = y + int16_t(rom.word(origin_y + direction * 2)) * pixel;
    slot->previous_x = slot->x; slot->previous_y = slot->y;
    slot->vx = dx[direction] * speed; slot->vy = dy[direction] * speed;
    slot->radius_x = rom.byte(list + 4); slot->radius_y = rom.byte(list + 5);
    slot->damage = rom.word(data); slot->weapon = uint8_t(weapon);
    if (weapon == 1) slot->vx = slot->vy = 0;
    if (weapon == 2) { slot->x = slot->previous_x = x; slot->y = slot->previous_y = y; slot->vx = slot->vy = 0; slot->damage = 30; slot->radius_x = slot->radius_y = 0; }
    cooldown_ = rom.byte(new_press ? reference::ProjectileCooldowns_Uncharged : reference::BeamAutoFireCooldowns);
    if (weapon) cooldown_ = rom.byte(reference::ProjectileCooldowns_NonBeamProjectiles + (weapon == 1 ? 1 : 3));
    return true;
}
void RoomGameplay::draw_door(Room& room, const Rom& rom, BlueDoorAnimation& door) {
    // Four known native phases, not general execution of arbitrary PLM opcodes.
    constexpr unsigned closing_offsets[]{0, 4, 11, 15, 22};
    const auto entry = door.closing ? door.list + closing_offsets[door.stage] : door.list + 3 + door.stage * 4;
    door.timer = rom.word(entry);
    const uint32_t draw = 0x840000u | rom.word(entry + 2);
    const auto header = rom.word(draw);
    if ((header & 0x7FFF) != 4 || rom.word(draw + 10) != 0) throw std::runtime_error("Unexpected blue door draw layout");
    const size_t stride = (header & 0x8000) ? size_t(room.width / 16) : 1;
    const size_t x = door.block % (room.width / 16), y = door.block / (room.width / 16);
    if ((stride == 1 && x + 4 > size_t(room.width / 16)) ||
        (stride != 1 && y + 4 > size_t(room.height / 16))) throw std::runtime_error("Blue door draw outside room");
    for (size_t i = 0; i < 4; ++i) {
        const auto index = door.block + i * stride;
        room.blocks.at(index) = rom.word(draw + 2 + i * 2);
        redraw_block(room, index);
    }
    ++room.visual_revision;
}
bool RoomGameplay::hit(Room& room, const Rom& rom, int x, int y, const PowerBeam& beam) {
    const auto index = reaction_block(room, x, y);
    if (index == room.blocks.size()) return true;
    const auto type = room.blocks[index] >> 12;
    const auto bts = room.bts.at(index);
    if ((type == 12 && bts < 8) || (type == 15 && bts < 8 && beam.weapon == 2)) {
        const auto old = room.blocks[index];
        const auto list = (bts < 4 ? reference::InstList_PLM_1x1RespawningShotBlock : reference::InstList_PLM_1x1ShotBlock) + 3;
        broken_.push_back({index, old, rom.word(list), 0, list, bts < 4});
        draw_list(room, rom, index, 0x840000u | rom.word(list + 2));
        return true;
    }
    if (type == 12 && bts == 0x45) {
        for (auto& item : pickups_) if (item.root == index && !item.revealed) {
            item.revealed = true; if (progression_) progression_->destroyed_chozo.set(item.argument & 0x1FF);
            item_frame(room, rom, item); return true;
        }
        return false;
    }
    if (type == 12 && bts == 0x44) {
        for (auto& lock : locks_) if (lock.root == index && lock.type == 3 && beam.weapon == 1 && ++lock.hits >= 5) {
            if (progression_) progression_->opened_doors.set(lock.argument & 0x1FF);
            lock.released = true; draw_cap(room, rom, index, lock.orientation, false);
            constexpr uint32_t opening[]{reference::InstList_PLM_BlueDoorFacingLeftOpened_40,
                reference::InstList_PLM_BlueDoorFacingLeftOpened_41, reference::InstList_PLM_BlueDoorFacingUpOpened_42,
                reference::InstList_PLM_BlueDoorFacingUpOpened_43};
            doors_.push_back({index, opening[lock.orientation], 0, 0}); draw_door(room, rom, doors_.back());
            break;
        }
        return true;
    }
    if (type == 12 && bts >= 0x40 && bts <= 0x43) {
        constexpr uint32_t lists[]{reference::InstList_PLM_BlueDoorFacingLeftOpened_40,
            reference::InstList_PLM_BlueDoorFacingLeftOpened_41,
            reference::InstList_PLM_BlueDoorFacingUpOpened_42,
            reference::InstList_PLM_BlueDoorFacingUpOpened_43};
        for (auto& lock : locks_) if (lock.root == index && lock.released && progression_)
            progression_->opened_doors.set(lock.argument & 0x1FF);
        doors_.push_back({index, lists[bts - 0x40], 0, 0});
        draw_door(room, rom, doors_.back());
        return true;
    }
    // Door trigger blocks do not obstruct projectiles. Non-blue reactions are
    // still solid; Power Beam must not bypass coloured/grey caps or other locks.
    if (type == 9 || type == 5 || type == 13) return false;
    return room_solid_pixel(room, rom, x, y);
}
void RoomGameplay::tick(Room& room, const Rom& rom, float camera_x, float camera_y) {
    if (cooldown_) --cooldown_;
    for (auto& block : broken_) if (block.timer && --block.timer == 0) {
        ++block.stage;
        if (block.stage < (block.restore ? 7u : 4u)) {
            const auto at = block.list + block.stage * 4; block.timer = rom.word(at);
            draw_list(room, rom, block.root, 0x840000u | rom.word(at + 2));
        } else if (block.restore) {
            room.blocks[block.root] = block.block; redraw_block(room, block.root); ++room.visual_revision;
        }
    }
    std::erase_if(broken_, [](const auto& block) { return !block.timer; });
    if (statue_pc_ && (!statue_timer_ || --statue_timer_ == 0)) for (unsigned budget = 0; budget < 16; ++budget) {
        const auto value = rom.word(statue_pc_);
        if (value < 0x8000) { statue_timer_ = value; draw_list(room, rom, statue_root_, 0x840000u | rom.word(statue_pc_ + 2)); statue_pc_ += 4; break; }
        const auto command = 0x840000u | value;
        if (command == reference::Instruction_PLM_PreInstruction_inY) { statue_pc_ += 4; continue; }
        if (command == reference::Instruction_PLM_Sleep) {
            if (!(progression_->equipped_items & 0x1000)) break;
            lock_boss(room, rom); statue_pc_ += 2; continue;
        }
        if (command == reference::Instruction_PLM_TransferBytesToVRAM) { statue_pc_ += 9; continue; }
        if (command == reference::Instruction_PLM_SpawnBombTorizoStatueBreakingWithArgY) { statue_pc_ += 4; continue; }
        if (command == reference::Instruction_PLM_QueueSong1MusicTrack) { statue_pc_ += 2; continue; }
        if (command == reference::Instruction_PLM_Delete) { statue_pc_ = 0; break; }
        throw std::runtime_error("Unsupported Bomb Torizo statue callback");
    }
    for (auto& door : doors_) {
        if (door.timer && --door.timer == 0 && door.stage < (door.closing ? 4u : 3u)) { ++door.stage; draw_door(room, rom, door); }
    }
    std::erase_if(doors_, [](const auto& door) { return !door.timer && door.stage == (door.closing ? 4u : 3u); });
    for (auto& item : pickups_) if (!item.taken) {
        ++item.frame;
        if (item.frame % 4 == 0) { item.frame /= 4; item_frame(room, rom, item); item.frame *= 4; }
    }
    for (auto& beam : beams_) {
        if (!beam.active) continue;
        beam.previous_x = beam.x; beam.previous_y = beam.y;
        ++beam.age;
        if (beam.weapon == 2) {
            if (beam.age < rom.word(reference::BombTimerResetValue)) continue;
            if (beam.age > unsigned(rom.word(reference::BombTimerResetValue)) + 8) { beam.active = false; continue; }
            beam.radius_x = beam.radius_y = 16;
        }
        if (beam.weapon == 1) {
            if (beam.age == 1) {
                constexpr int dx[]{0, 1, 1, 1, 0, 0, -1, -1, -1, 0}, dy[]{-1,-1,0,1,1,1,1,0,-1,-1};
                beam.vx = dx[beam.direction] * pixel; beam.vy = dy[beam.direction] * pixel;
            } else {
                beam.vx += (int16_t(rom.word(reference::MissileAccelerations + beam.direction * 4)) +
                    int16_t(rom.word(reference::ProjectileAccelerations_X + beam.direction * 2))) * 256;
                beam.vy += (int16_t(rom.word(reference::MissileAccelerations + beam.direction * 4 + 2)) +
                    int16_t(rom.word(reference::ProjectileAccelerations_Y + beam.direction * 2))) * 256;
            }
        }
        // Check the spawn rectangle before movement as in initial beam collision,
        // then sweep in <=1-pixel steps to avoid skipping caps/extension blocks.
        const int steps = std::max(1, (std::max(std::abs(beam.vx), std::abs(beam.vy)) + pixel - 1) / pixel);
        const auto start_x = beam.x, start_y = beam.y;
        for (int step = 0; step <= steps && beam.active; ++step) {
            beam.x = start_x + int32_t(int64_t(beam.vx) * step / steps);
            beam.y = start_y + int32_t(int64_t(beam.vy) * step / steps);
            const int px = int(std::floor(double(beam.x) / pixel)), py = int(std::floor(double(beam.y) / pixel));
            bool absorbed = false;
            std::set<size_t> reacted;
            // React to every block in the collision rectangle before retiring
            // the shot. A solid neighbour must not hide an adjacent shot block.
            for (int cy = py - beam.radius_y; cy <= py + beam.radius_y; ++cy)
                for (int cx = px - beam.radius_x; cx <= px + beam.radius_x; ++cx) {
                    const auto root = reaction_block(room, cx, cy);
                    if (root < room.blocks.size() && (room.blocks[root] >> 12) == 12 && !reacted.insert(root).second) continue;
                    absorbed |= hit(room, rom, cx, cy, beam);
                }
            if (absorbed && beam.weapon != 2) beam.active = false;
        }
        // Original-width view contract; widescreen artwork never extends reach.
        if (beam.x / float(pixel) < camera_x - 32 || beam.x / float(pixel) > camera_x + 288 ||
            beam.y / float(pixel) < camera_y - 32 || beam.y / float(pixel) > camera_y + 256) beam.active = false;
    }
}
}
