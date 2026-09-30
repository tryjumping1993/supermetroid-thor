#include "thor/enemies.hpp"
#include "reference_index.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace thor {
namespace {
constexpr int32_t pixel = 65536;
bool facing_right(const Enemy& e) { return e.graphics & 0x8000; }
void script(Enemy& e, uint32_t pc) { e.pc = pc; e.timer = 1; e.sleeping = false; }
bool front(const Enemy& e, int x) { return facing_right(e) ? x >= e.x / pixel : x < e.x / pixel; }
}
void Enemies::load(const Room& room, const Rom& rom, const Progression& p) {
    enemies_.clear(); shots_.clear(); killed_ = 0;
    for (const auto& spawn : room.enemies) {
        const bool boss = spawn.kind == (reference::EnemyHeaders_BombTorizo & 0xFFFF);
        const bool walking = spawn.kind == (reference::EnemyHeaders_PirateGreyWalking & 0xFFFF);
        const bool wall = spawn.kind == (reference::EnemyHeaders_PirateGreyWall & 0xFFFF);
        const bool elevator = spawn.kind == (reference::EnemyHeaders_Elevator & 0xFFFF);
        const bool ship = spawn.kind == (reference::EnemyHeaders_ShipTop & 0xFFFF) || spawn.kind == (reference::EnemyHeaders_ShipBottomEntrance & 0xFFFF);
        if ((!boss && !walking && !wall && !elevator && !ship) || (boss && (p.bosses[room.area] & 4))) continue;
        Enemy e; e.kind = spawn.kind; e.boss = boss;
        const auto header = 0xA00000u | spawn.kind;
        e.bank = uint32_t(rom.byte(header + 12)) << 16;
        e.health = rom.word(header + 4); e.damage = rom.word(header + 6);
        e.radius_x = rom.word(header + 8); e.radius_y = rom.word(header + 10);
        e.x = spawn.x * pixel; e.y = spawn.y * pixel;
        e.home_x = spawn.x; e.home_y = spawn.y; e.range = spawn.parameter2;
        e.graphics = (spawn.parameter1 & 1) ? 0x8000 : 0;
        if (ship || elevator) {
            e.decoration = true; e.damage = 0;
            e.pc = elevator ? reference::InstList_Elevator : spawn.kind == (reference::EnemyHeaders_ShipTop & 0xFFFF) ? reference::InstList_ShipTop : spawn.parameter2 ? reference::InstList_ShipBottom : reference::InstList_ShipEntrancePad_Closed;
            e.map = e.bank | rom.word(e.pc + 2); enemies_.push_back(e); continue;
        }
        if (boss) {
            e.x = rom.word(reference::TorizoInitial_XPosition) * pixel;
            e.y = rom.word(reference::TorizoInitial_YPosition) * pixel;
            e.radius_x = rom.word(reference::TorizoInitial_XRadius); e.radius_y = rom.word(reference::TorizoInitial_YRadius);
            e.pc = reference::InstList_Torizo_BombTorizo_Initial_0;
        } else if (walking) e.pc = facing_right(e) ? reference::InstList_PirateWalking_WalkingRight_0 : reference::InstList_PirateWalking_WalkingLeft_0;
        else e.pc = facing_right(e) ? reference::InstList_PirateWall_MovingDownRightWall_0 : reference::InstList_PirateWall_MovingUpLeftWall_0;
        e.previous_x = e.x; e.previous_y = e.y; enemies_.push_back(e);
    }
}
bool Enemies::move(Enemy& e, const Room& room, const Rom& rom, int32_t distance, bool vertical) {
    auto& position = vertical ? e.y : e.x;
    const int direction = distance < 0 ? -1 : 1;
    int32_t remaining = std::abs(distance);
    while (remaining) {
        const auto step = std::min(remaining, pixel) * direction;
        const int x = (vertical ? e.x : e.x + step) / pixel, y = (vertical ? e.y + step : e.y) / pixel;
        bool solid = false;
        if (vertical) {
            for (int xx = x - e.radius_x; xx < x + e.radius_x; ++xx)
                solid |= room_solid_pixel(room, rom, xx, y + (direction > 0 ? e.radius_y - 1 : -e.radius_y));
        } else for (int yy = y - e.radius_y; yy < y + e.radius_y; ++yy)
            solid |= room_solid_pixel(room, rom, x + (direction > 0 ? e.radius_x - 1 : -e.radius_x), yy);
        if (solid) return true;
        position += step; remaining -= std::abs(step);
    }
    return false;
}
void Enemies::attack(Enemy& e, const Rom& rom, unsigned type, int parameter) {
    if (shots_.size() >= 32) return;
    EnemyShot shot; shot.type = type; shot.life = 180; shot.damage = 10;
    const int direction = facing_right(e) ? 1 : -1;
    shot.x = e.x + direction * 32 * pixel; shot.y = e.y;
    uint32_t list;
    if (type == 0) { // $86:9BA2 space-pirate laser.
        shot.x = e.x + direction * 16 * pixel; shot.y += parameter * pixel;
        shot.vx = direction * 4 * pixel;
        list = direction > 0 ? reference::InstList_EnemyProjectile_Pirate_MotherBrain_Laser_Right_0 : reference::InstList_EnemyProjectile_Pirate_MotherBrain_Laser_Left_0;
    } else if (type == 1) { // $86:AE15 sonic boom, acceleration $10 / tick.
        shot.y += (parameter ? 20 : -12) * pixel; shot.vx = direction * 0x270 * 256;
        list = direction > 0 ? reference::InstList_EnemyProjectile_TorizoSonicBoom_FiredRight : reference::InstList_EnemyProjectile_TorizoSonicBoom_FiredLeft;
    } else if (type == 2) {
        shot.vx = direction * (pixel + (parameter + 1) * pixel / 2); shot.vy = -(parameter + 2) * pixel;
        list = direction > 0 ? reference::InstList_EnemyProjectile_TorizoChozoOrbs_Right : reference::InstList_EnemyProjectile_TorizoChozoOrbs_Left;
    } else {
        const auto offset = unsigned(parameter) % 20;
        const auto dx = int16_t(rom.word(reference::InitAI_EnemyProjectile_BombTorizoExplosiveSwipe_Xpositions + offset));
        shot.x = e.x + (direction > 0 ? -dx : dx) * pixel;
        shot.y = e.y + int16_t(rom.word(reference::InitAI_EnemyProjectile_BombTorizoExplosiveSwipe_Yposition + offset)) * pixel;
        shot.life = 16; list = reference::InstList_EnemyProjectile_BombTorizoExplosionSwipe;
    }
    shot.map = 0x8D0000u | rom.word(list + 2); shots_.push_back(shot);
}
void Enemies::animate(Enemy& e, const Room& room, const Rom& rom, Progression& p, int sx, int sy, uint64_t tick) {
    if (e.sleeping || (e.timer && --e.timer)) return;
    for (unsigned budget = 0; budget < 48; ++budget) {
        const auto value = rom.word(e.pc); e.pc += 2;
        if (value < 0x8000) { e.timer = value ? value : 1; e.map = e.bank | rom.word(e.pc); e.pc += 2; return; }
        const auto op = e.bank | value;
        auto argument = [&] { const auto v = rom.word(e.pc); e.pc += 2; return v; };
        if (value == (reference::Instruction_CommonAA_GotoY & 0xFFFF)) { e.pc = e.bank | argument(); continue; }
        if (value == (reference::Instruction_CommonAA_TimerInY & 0xFFFF)) { e.loop = argument(); continue; }
        if (value == (reference::Instruction_CommonAA_DecrementTimer_GotoYIfNonZero_duplicate & 0xFFFF)) {
            const auto target = argument(); if (e.loop && --e.loop) e.pc = e.bank | target; continue;
        }
        if (value == (reference::Instruction_CommonAA_WaitYFrames & 0xFFFF)) { e.timer = argument(); return; }
        if (value == (reference::Instruction_CommonAA_DeleteEnemy & 0xFFFF)) { e.active = false; return; }
        if (value == (reference::Instruction_CommonAA_Sleep & 0xFFFF)) { e.sleeping = true; return; }
        if (value == (reference::Instruction_CommonAA_TransferYBytesInYToVRAM & 0xFFFF)) { e.pc += 7; continue; }
        if (value == (reference::Instruction_CommonAA_Enemy0FB2_InY & 0xFFFF)) { e.movement = e.bank | argument(); continue; }
        if (op == reference::Instruction_Torizo_FunctionInY || op == reference::Instruction_PirateWalking_FunctionInY ||
            op == reference::Instruction_PirateWall_FunctionInY) { e.function = e.bank | argument(); continue; }
        if (op == reference::Instruction_Torizo_LinkInstructionInY) { e.link = e.bank | argument(); continue; }
        if (op == reference::Instruction_Torizo_Return) { e.pc = e.link; continue; }
        if (op == reference::Instruction_Torizo_GotoGutExplosionLinkInstruction) { e.pc = e.gut_link; continue; }
        if (op == reference::Instruction_Torizo_GotoY_IfFaceBlownUp_ElseGotoY2_IfGolden) {
            const auto target = argument(); argument(); if (e.behavior & 0x4000) e.pc = e.bank | target; continue;
        }
        if (op == reference::Instruction_Torizo_SetAnimationLock) { e.locked = true; continue; }
        if (op == reference::Instruction_Torizo_ClearAnimationLock) { e.locked = false; continue; }
        if (op == reference::Instruction_Torizo_SetSteppedLeftWithLeftFootState || op == reference::Instruction_Torizo_SetSteppedLeftWithRightFootState ||
            op == reference::Instruction_Torizo_SetSteppedRightWithLeftFootState || op == reference::Instruction_Torizo_SetSteppedRightWithRightFootState) {
            const bool right = op == reference::Instruction_Torizo_SetSteppedRightWithLeftFootState || op == reference::Instruction_Torizo_SetSteppedRightWithRightFootState;
            const bool foot = op == reference::Instruction_Torizo_SetSteppedLeftWithRightFootState || op == reference::Instruction_Torizo_SetSteppedRightWithLeftFootState;
            e.graphics = (e.graphics & 0x1FFF) | (right ? 0x8000 : 0) | (foot ? 0x2000 : 0); ++e.steps; continue;
        }
        if (op == reference::Instruction_Torizo_StandingUpMovement_IndexInY) {
            const auto index = argument(); e.x += int16_t(rom.word(reference::Instruction_Torizo_StandingUpMovement_IndexInY_XVelocities + index)) * pixel;
            e.y += int16_t(rom.word(reference::Instruction_Torizo_StandingUpMovement_IndexInY_YVelocities + (index & 15))) * pixel; continue;
        }
        if (op == reference::Instruction_Torizo_BombTorizoWalkingMovement_Normal_IndexInY || op == reference::Instruction_Torizo_BTWalkingMovement_Faceless_IndexInY) {
            const auto index = argument();
            const auto table = op == reference::Instruction_Torizo_BombTorizoWalkingMovement_Normal_IndexInY ? reference::Instruction_Torizo_BombTorizoWalkingMovement_Normal_IndexInY_velocities : reference::Instruction_Torizo_BTWalkingMovement_Faceless_IndexInY_velocities;
            e.vx = int16_t(rom.word(table + index));
            if (move(e, room, rom, e.vx * pixel, false)) {
                e.turn = 0; e.pc = facing_right(e) ? reference::InstList_Torizo_FacingLeft_TurningLeft : reference::InstList_Torizo_FacingRight_TurningRight;
            } else if (!front(e, sx) && !e.turn) e.turn = 72;
            continue;
        }
        if (op == reference::Instruction_Torizo_CallYIfSamusIsLessThan38PixelsInFront) {
            const auto target = argument(); if (front(e, sx) && std::abs(sx - e.x / pixel) < 56) { e.link = e.pc; e.pc = e.bank | target; } continue;
        }
        if (op == reference::Instruction_Torizo_GotoYAndJumpBackwardsIfLessThan20Pixels) {
            const auto target = argument(); if (front(e, sx) && std::abs(sx - e.x / pixel) < 32) {
                e.vx = facing_right(e) ? -0x300 : 0x300; e.vy = -0x480; e.pc = e.bank | target;
            } continue;
        }
        if (op == reference::Instruction_Torizo_GotoY_IfRising) { const auto target = argument(); if (e.vy < 0) e.pc = e.bank | target; continue; }
        if (op == reference::Instruction_Torizo_CallY_OrY2_ForBombTorizoAttack) {
            const auto orb = argument(), sonic = argument(); e.link = e.pc;
            e.pc = e.bank | ((p.missiles < 5 || (((sx >> 1) + tick) & 8)) ? orb : sonic); continue;
        }
        if (op == reference::Instruction_Torizo_SpawnBombTorizosChozoOrbs) { for (int i = 0; i < 3; ++i) attack(e, rom, 2, i); continue; }
        if (op == reference::Instruction_Torizo_SpawnBombTorizoSonicBoomWithParameterY) { attack(e, rom, 1, argument()); continue; }
        if (op == reference::Instruction_Torizo_SpawnBombTorizoExplosiveSwipeWithParamY) { attack(e, rom, 3, argument()); continue; }
        if (op == reference::Instruction_Torizo_SetAsVisible) { e.visible = true; continue; }
        if (op == reference::Instruction_Torizo_SetAsInvisible) { e.visible = false; continue; }
        if (op == reference::Instruction_Torizo_MarkBTGutBlownUp_Spawn6BTDroolProjectiles) { e.behavior |= 0x8000; continue; }
        if (op == reference::Instruction_Torizo_MarkBombTorizoFaceBlownUp) { e.behavior |= 0x4000; continue; }
        if (op == reference::Instruction_Torizo_Spawn5LowHealthExplosion_SleepFor28Frames) { argument(); e.timer = 28; return; }
        if (op == reference::Instruction_Torizo_SetBossBit_QueueElevatorMusic_SpawnDrops) { p.bosses[room.area] |= 4; continue; }
        if (op == reference::Instruction_Torizo_SpawnTorizoDeathExplosion_SleepFor1IFrame) { e.timer = 1; return; }
        if (op == reference::Instruction_Torizo_SetTorizoTurningAroundFlag) { e.graphics |= 0x4000; continue; }
        if (op == reference::Instruction_Torizo_PlayShotTorizoSFX || op == reference::Instruction_Torizo_PlayTorizoFootstepsSFX ||
            op == reference::Instruction_Torizo_SpawnLowHealthInitialDroolIfHealthIsLow || op == reference::Instruction_Torizo_SpawnTorizoLandingDustClouds ||
            op == reference::Instruction_Torizo_SetupPaletteTransitionToBlack || op == reference::Instruction_Torizo_SetupPaletteTransitionToNormalTorizo ||
            op == reference::Instruction_Torizo_AdvanceGradualColorChange || op == reference::Instruction_Torizo_StartFightMusic_BombTorizoBellyPaletteFX || op == reference::RTL_AAC2C8) continue;
        if (op == reference::Instruction_PirateWalking_FireLaserLeftWithYOffsetInY || op == reference::Instruction_PirateWalking_FireLaserRightWithYOffsetInY) {
            e.graphics = op == reference::Instruction_PirateWalking_FireLaserRightWithYOffsetInY ? 0x8000 : 0; attack(e, rom, 0, int16_t(argument())); continue;
        }
        if (op == reference::Instruction_PirateWalking_ChooseAMovement) {
            e.pc = std::abs(sy - e.y / pixel) < 16 ? (sx < e.x / pixel ? reference::InstList_PirateWalking_FireLasersLeft : reference::InstList_PirateWalking_FireLasersRight) :
                (facing_right(e) ? reference::InstList_PirateWalking_WalkingRight_0 : reference::InstList_PirateWalking_WalkingLeft_0); continue;
        }
        if (op == reference::Inst_PirateWall_MoveYPixelsDown_ChangeDirOnCollision_Left || op == reference::Inst_PirateWall_MoveYPixelsDown_ChangeDirOnCollision_Right) {
            const auto delta = int16_t(argument());
            if (move(e, room, rom, delta * pixel, true)) e.pc = facing_right(e) ?
                (delta > 0 ? reference::InstList_PirateWall_MovingUpRightWall_0 : reference::InstList_PirateWall_MovingDownRightWall_0) :
                (delta > 0 ? reference::InstList_PirateWall_MovingUpLeftWall_0 : reference::InstList_PirateWall_MovingDownLeftWall_0);
            continue;
        }
        if (op == reference::Instruction_PirateWall_RandomlyChooseADirection_LeftWall || op == reference::Instruction_PirateWall_RandomlyChooseADirection_RightWall) {
            e.pc = facing_right(e) ? ((tick & 1) ? reference::InstList_PirateWall_MovingUpRightWall_0 : reference::InstList_PirateWall_MovingDownRightWall_0) :
                ((tick & 1) ? reference::InstList_PirateWall_MovingUpLeftWall_0 : reference::InstList_PirateWall_MovingDownLeftWall_0); continue;
        }
        throw std::runtime_error("Unsupported native enemy callback " + std::to_string(op));
    }
    throw std::runtime_error("Unbounded native enemy animation");
}
uint16_t Enemies::tick(Room& room, const Rom& rom, Progression& p, RoomGameplay& gameplay, int sx, int sy, int radius,
    float camera_x, float camera_y, uint64_t tick) {
    uint16_t damage = 0;
    for (auto& e : enemies_) {
        e.previous_x = e.x; e.previous_y = e.y;
        if (!e.active) continue;
        if (e.decoration) {
            if (e.kind == (reference::EnemyHeaders_Elevator & 0xFFFF)) e.map = e.bank | rom.word(e.pc + ((tick / 2) & 1) * 4 + 2);
            continue;
        }
        if (e.flash) --e.flash;
        // Original camera activates gameplay; widescreen has no wider reach.
        if (!e.boss && (e.x / float(pixel) < camera_x - 64 || e.x / float(pixel) > camera_x + 320 ||
            e.y / float(pixel) < camera_y - 64 || e.y / float(pixel) > camera_y + 288)) continue;
        if (e.boss) {
            if (e.sleeping && !gameplay.statue_active()) { e.sleeping = false; e.timer = 1; }
            if (!e.sleeping && e.function != reference::Function_Torizo_WakeWhenBombTorizoChozoFinishesCrumbling) {
                if (e.function == reference::Function_Torizo_NormalMovement && e.health && !e.locked) {
                    if (e.health < 350 && !(e.behavior & 0x8000)) { e.gut_link = e.pc; script(e, reference::InstList_Torizo_SpecialCallable_BlowUpBombTorizosGut); }
                    else if (e.health < 100 && !(e.behavior & 0x4000)) {
                        e.link = facing_right(e) ? reference::InstList_Torizo_FacingLeft_Faceless_TurningLeft : reference::InstList_Torizo_FacingRight_Faceless_TurningRight;
                        script(e, reference::InstList_Torizo_Callable_BlowUpBombTorizosFace);
                    }
                }
                if (e.movement == reference::Function_Torizo_Movement_Jumping_Falling) move(e, room, rom, e.vx * 256, false);
                e.grounded = move(e, room, rom, e.vy * 256, true);
                if (!e.grounded) e.vy += 0x28;
                else if (e.movement == reference::Function_Torizo_Movement_Jumping_Falling && e.vy > 0) { script(e, e.link); e.vy = 0x100; }
                else e.vy = 0x100;
                if (e.turn && --e.turn == 0 && e.movement == reference::Function_Torizo_Movement_Walking)
                    script(e, facing_right(e) ? reference::InstList_Torizo_FacingLeft_TurningLeft : reference::InstList_Torizo_FacingRight_TurningRight);
            }
        } else if (e.kind == (reference::EnemyHeaders_PirateGreyWalking & 0xFFFF)) {
            if (e.function == reference::Function_PirateWalking_WalkingLeft || e.function == reference::Function_PirateWalking_WalkingRight) {
                if (std::abs(sy - e.y / pixel) < 16) script(e, sx < e.x / pixel ? reference::InstList_PirateWalking_FireLasersLeft : reference::InstList_PirateWalking_FireLasersRight);
                else {
                    move(e, room, rom, pixel, true);
                    const bool right = e.function == reference::Function_PirateWalking_WalkingRight;
                    e.graphics = right ? 0x8000 : 0;
                    if (move(e, room, rom, right ? 0x3800 : -0x3800, false) || std::abs(e.x / pixel - e.home_x) > e.range)
                        script(e, right ? reference::InstList_PirateWalking_LookingAround_FacingRight : reference::InstList_PirateWalking_LookingAround_FacingLeft);
                }
            }
        } else if (std::abs(sy - e.y / pixel) < 32 && tick % 64 == 0) attack(e, rom, 0);
        animate(e, room, rom, p, sx, sy, tick);
        if (!e.active) continue;
        for (auto& beam : gameplay.beams()) {
            if (!beam.active || !e.health || e.locked || e.sleeping || e.flash || (beam.weapon == 2 && beam.age < rom.word(reference::BombTimerResetValue))) continue;
            if (std::abs(beam.x / pixel - e.x / pixel) > e.radius_x + beam.radius_x ||
                std::abs(beam.y / pixel - e.y / pixel) > e.radius_y + beam.radius_y) continue;
            e.health = uint16_t(std::max(0, int(e.health) - beam.damage)); e.flash = 8;
            if (beam.weapon != 2) beam.active = false;
            if (!e.health) {
                if (e.boss) { script(e, reference::InstList_Torizo_DeathSequence_0); e.function = e.movement = 0; }
                else { e.active = false; ++killed_; }
            }
        }
        if (e.health && !e.sleeping && e.map && std::abs(sx - e.x / pixel) < e.radius_x + 5 && std::abs(sy - e.y / pixel) < e.radius_y + radius) damage = std::max(damage, e.damage);
    }
    if (room.enemy_quota && killed_ >= room.enemy_quota) {
        gameplay.release_grey(room, rom);
        if (room.header == 0x8F975C) p.events.set(0); // $84:BE01: Zebes awake after Pit pirates.
    }
    if (p.bosses[room.area] & 4) gameplay.release_grey(room, rom, true);
    for (auto& shot : shots_) {
        if (!shot.life) continue;
        --shot.life; shot.x += shot.vx; shot.y += shot.vy;
        if (shot.type == 1) shot.vx += shot.vx < 0 ? -0x1000 : 0x1000;
        if (shot.type == 2) shot.vy += 0x2800;
        if (shot.type != 3 && room_solid_pixel(room, rom, shot.x / pixel, shot.y / pixel)) shot.life = 0;
        if (std::abs(sx - shot.x / pixel) < 9 && std::abs(sy - shot.y / pixel) < radius + 4) { damage = std::max(damage, shot.damage); if (shot.type != 3) shot.life = 0; }
        if (shot.type == 2) for (auto& beam : gameplay.beams()) if (beam.active && std::abs(beam.x - shot.x) < 12 * pixel && std::abs(beam.y - shot.y) < 12 * pixel) {
            beam.active = false; shot.life = 0; p.missiles = uint16_t(std::min<int>(p.max_missiles, p.missiles + 2));
        }
    }
    std::erase_if(shots_, [](const auto& s) { return !s.life; });
    return damage;
}

namespace {
void paint_map(Image& image, const Rom& rom, uint32_t map, std::span<const uint8_t> tiles, const std::array<uint32_t, 256>& colors, int tile_base, int offset_x, int offset_y) {
    const auto count = rom.word(map); if (count > 128) throw std::runtime_error("Invalid enemy spritemap");
    for (unsigned i = 0; i < count; ++i) {
        const auto at = map + 2 + i * 5; const auto xword = rom.word(at), attributes = rom.word(at + 3);
        int ox = xword & 0x1FF; if (ox & 0x100) ox -= 512;
        const int oy = int8_t(rom.byte(at + 2)), dimension = xword & 0x8000 ? 16 : 8;
        for (int y = 0; y < dimension; ++y) for (int x = 0; x < dimension; ++x) {
            const int dx = offset_x + ox + x + image.width / 2, dy = offset_y + oy + y + image.height / 2;
            if (dx < 0 || dx >= image.width || dy < 0 || dy >= image.height) continue;
            const int tx = attributes & 0x4000 ? dimension - 1 - x : x, ty = attributes & 0x8000 ? dimension - 1 - y : y;
            const int tile = int(attributes & 0x1FF) - tile_base + tx / 8 + ty / 8 * 16;
            if (tile < 0 || size_t(tile + 1) * 32 > tiles.size()) continue;
            const auto value = tile_pixel(tiles, tile, tx % 8, ty % 8);
            if (value) image.pixels[dy * image.width + dx] = colors[((attributes >> 9) & 7) * 16 + value];
        }
    }
}
}
Image draw_enemy(const Rom& rom, uint16_t kind, uint32_t map, uint16_t) {
    const bool ship = kind == (reference::EnemyHeaders_ShipTop & 0xFFFF) || kind == (reference::EnemyHeaders_ShipBottomEntrance & 0xFFFF);
    Image image{ship ? 192 : 128, 128, std::vector<uint32_t>((ship ? 192 : 128) * 128)}; if (!map) return image;
    if (kind == (reference::EnemyHeaders_Elevator & 0xFFFF)) {
        const auto tiles = rom.slice(reference::Tiles_Standard_Sprite_0, 0x1800);
        auto colors = decode_palette(rom.slice(reference::Initial_Palette_spritePalette0, 256));
        paint_map(image, rom, map, tiles, colors, 0, 0, 0); return image;
    }
    const auto header = 0xA00000u | kind;
    const auto tiles = rom.slice(rom.pointer(header + 0x36), rom.word(header));
    auto colors = decode_palette(rom.slice((uint32_t(rom.byte(header + 12)) << 16) | rom.word(header + 2), 32));
    // Pirate graphics use a single palette; Torizo's parts use two.
    for (int i = 1; i < 8; ++i) std::copy_n(colors.begin(), 16, colors.begin() + i * 16);
    if (kind == (reference::EnemyHeaders_BombTorizo & 0xFFFF)) {
        const auto a = decode_palette(rom.slice(reference::Palette_Torizo_Normal_SpritePalette1, 32));
        const auto b = decode_palette(rom.slice(reference::Palette_Torizo_Normal_SpritePalette2, 32));
        std::copy_n(a.begin(), 16, colors.begin() + 16); std::copy_n(b.begin(), 16, colors.begin() + 32);
    }
    if (ship) { paint_map(image, rom, map, tiles, colors, 0x100, 0, 0); return image; }
    const auto parts = rom.word(map); if (parts > 16) throw std::runtime_error("Invalid extended enemy spritemap");
    for (unsigned i = 0; i < parts; ++i) {
        const auto at = map + 2 + i * 8;
        paint_map(image, rom, (map & 0xFF0000) | rom.word(at + 4), tiles, colors, 0x100,
            int16_t(rom.word(at)), int16_t(rom.word(at + 2)));
    }
    return image;
}
Image draw_enemy_shot(const Rom& rom, uint32_t map) {
    Image image{64, 64, std::vector<uint32_t>(4096)}; if (!map) return image;
    std::vector<uint8_t> tiles(0x4000);
    const auto a = rom.slice(reference::Tiles_Standard_Sprite_0, 0x1800), b = rom.slice(reference::Tiles_Standard_Sprite_1, 0x800);
    std::copy(a.begin(), a.end(), tiles.begin()); std::copy(b.begin(), b.end(), tiles.begin() + 0x1800);
    const auto boss = rom.slice(rom.pointer(reference::EnemyHeaders_BombTorizo + 0x36), 0x2000);
    std::copy(boss.begin(), boss.end(), tiles.begin() + 0x2000);
    auto colors = decode_palette(rom.slice(reference::Initial_Palette_spritePalette0, 256));
    const auto orbs = decode_palette(rom.slice(reference::Palette_Torizo_OrbProjectile, 32));
    std::copy_n(orbs.begin(), 16, colors.begin() + 48);
    paint_map(image, rom, map, tiles, colors, 0, 0, 0); return image;
}
}
