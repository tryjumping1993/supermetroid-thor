#include "thor/content.hpp"
#include "reference_index.hpp"

namespace thor {
bool room_solid_pixel(const Room& room, const Rom& rom, int x, int y) {
    if (x < 0 || y < 0 || x >= room.width || y >= room.height) return true;
    const int columns = room.width / 16;
    int index = (y / 16) * columns + x / 16;
    // $94:9411/$9447: signed BTS offsets redirect the reaction. A zero
    // extension behaves as air. Bound traversal to avoid cycles in bad data.
    for (size_t hops = 0; hops < room.blocks.size(); ++hops) {
        if (index < 0 || size_t(index) >= room.blocks.size()) return true;
        const unsigned type = room.blocks[index] >> 12;
        const auto bts = room.bts.at(index);
        if (type == 5 || type == 13) {
            if (!bts) return false;
            index += int(int8_t(bts)) * (type == 13 ? columns : 1);
            continue;
        }
        if (type != 1) return type >= 8;
        int px = x % 16, py = y % 16;
        if (bts & 0x40) px = 15 - px;
        if (bts & 0x80) py = 15 - py;
        const unsigned shape = bts & 31;
        if (shape < 5) {
            // $94:8E54: four independently solid 8x8 quadrants.
            return rom.byte(reference::SquareSlopeDefinitions_Bank94 + shape * 4 + (py / 8) * 2 + px / 8) != 0;
        }
        // $94:8B2B: top boundary sampled at each X pixel, including empty
        // columns with heights >=16. This is geometry, not original Samus
        // slope alignment or shinespark collision behavior.
        return py >= rom.byte(reference::SlopeDefinitions_SlopeTopXOffsetByYPixel + shape * 16 + px);
    }
    return true;
}
}
