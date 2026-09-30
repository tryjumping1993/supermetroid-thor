#pragma once
#include "thor/content.hpp"

namespace thor {
struct SamusAnimation { uint16_t pose = 1, frame = 0, timer = 1; };
uint16_t input_pose(const Rom& rom, uint16_t pose, uint16_t held, uint16_t pressed, int32_t vx);
void animate_samus(const Rom& rom, SamusAnimation& animation, uint16_t equipped, bool grounded, bool jump_held);
uint8_t pose_radius(const Rom& rom, uint16_t pose);
uint8_t movement_type(const Rom& rom, uint16_t pose);
}
