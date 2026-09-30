#include "thor/content.hpp"
#include "reference_index.hpp"
#include <stdexcept>

namespace thor {
uint32_t select_room_state(const Rom& rom, uint32_t header, const Progression& p) {
    // Direct native translation of $8F:E5D2..E689. Walk the data condition
    // chain in original priority order; never execute assembly from the ROM.
    uint32_t at = header + 11;
    const unsigned area = rom.byte(header + 1);
    if (area >= p.bosses.size()) throw std::runtime_error("Invalid room area");
    for (unsigned checks = 0; checks < 32; ++checks) {
        const uint16_t condition = rom.word(at); at += 2;
        bool matches = false;
        switch (condition) {
        case reference::Use_StatePointer_inX & 0xFFFF: return at;
        case reference::UNUSED_RoomStateCheck_Door_8FE5EB & 0xFFFF:
            matches = rom.word(at) == p.entering_door; at += 2; break;
        case reference::RoomStateCheck_MainAreaBossIsDead & 0xFFFF:
            matches = (p.bosses[area] & 1) != 0; break;
        case reference::RoomStateCheck_EventHasBeenSet & 0xFFFF:
            matches = p.events.test(rom.byte(at++)); break;
        case reference::RoomStateCheck_BossIsDead & 0xFFFF:
            matches = (p.bosses[area] & rom.byte(at++)) != 0; break;
        case reference::UNUSED_RoomStateCheck_Morphball_8FE640 & 0xFFFF:
            matches = (p.collected_items & 4) != 0; break;
        case reference::RoomStateCheck_MorphballAndMissiles & 0xFFFF:
            matches = (p.collected_items & 4) && p.max_missiles; break;
        case reference::RoomStateCheck_PowerBombs & 0xFFFF:
            matches = p.max_power_bombs != 0; break;
        case reference::UNUSED_RoomStateCheck_SpeedBooster_8FE678 & 0xFFFF:
            matches = (p.collected_items & 0x2000) != 0; break;
        default: throw std::runtime_error("Untranslated room state condition");
        }
        if (matches) return 0x8F0000u | rom.word(at);
        at += 2;
    }
    throw std::runtime_error("Unterminated room state condition chain");
}
}
