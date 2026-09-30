#include "thor/clock.hpp"
#include "thor/content.hpp"
#include "thor/session.hpp"
#include "thor/sram.hpp"
#include "reference_index.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <tuple>

namespace {
unsigned checks = 0;
void require(bool value, const char* description) {
    ++checks; if (!value) throw std::runtime_error(description);
}
template<class F> void rejects(F operation, const char* description) {
    bool threw = false; try { operation(); } catch (const std::exception&) { threw = true; }
    require(threw, description);
}
void decompression() {
    auto decode = [](std::initializer_list<uint8_t> data) { return thor::decompress(std::vector<uint8_t>(data)); };
    require(decode({2, 1, 2, 3, 0xFF}) == std::vector<uint8_t>({1,2,3}), "literal");
    require(decode({0x23, 9, 0xFF}) == std::vector<uint8_t>({9,9,9,9}), "byte fill");
    require(decode({0x44, 1, 2, 0xFF}) == std::vector<uint8_t>({1,2,1,2,1}), "odd word fill");
    require(decode({0x62, 254, 0xFF}) == std::vector<uint8_t>({254,255,0}), "increment wraps");
    require(decode({0, 7, 0x83, 0, 0, 0xFF}) == std::vector<uint8_t>({7,7,7,7,7}), "overlapping absolute copy");
    require(decode({0, 0x0F, 0xA0, 0, 0, 0xFF}) == std::vector<uint8_t>({0x0F,0xF0}), "inverted absolute copy");
    require(decode({0, 7, 0xC2, 1, 0xFF}) == std::vector<uint8_t>({7,7,7,7}), "relative copy");
    require(decode({0, 0x0F, 0xFC, 1, 1, 0xFF}) == std::vector<uint8_t>({0x0F,0xF0,0x0F}), "extended inverted relative copy");
    require(decode({0xE4, 33, 42, 0xFF}).size() == 34, "extended fill");
    rejects([&] { decode({0, 7}); }, "missing terminator");
    rejects([&] { decode({0x80, 0, 0, 0xFF}); }, "forward reference");
    rejects([&] { decode({0xC0, 0, 0xFF}); }, "zero distance");
    rejects([&] { thor::decompress(std::vector<uint8_t>{0x23, 9, 0xFF}, 3); }, "output overflow");
}
void clocks() {
    for (int fps : {60, 90, 120, 144}) {
        thor::TickClock clock; int ticks = 0;
        for (int i = 0; i <= fps * 10; ++i) clock.advance(uint64_t(i) * 1000000000ULL / fps, [&] { ++ticks; });
        require(ticks == 600, "tick cadence independent of display");
        require(clock.alpha() >= 0 && clock.alpha() < 1, "bounded interpolation");
        clock.reset(); clock.advance(100000000000ULL, [&] { ++ticks; });
        require(ticks == 600, "resume does not catch up suspended time");
        clock.advance(1, [&] { ++ticks; }); require(ticks == 600, "backward timestamp does not advance");
    }
}
void saves() {
    thor::Sram saves;
    std::vector<uint8_t> payload(thor::Sram::slot_size);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = uint8_t(i * 17 + 3);
    for (unsigned slot = 0; slot < 3; ++slot) {
        saves.write_slot(slot, payload); require(saves.valid(slot), "SRAM slot checksum");
        require(std::equal(payload.begin(), payload.end(), saves.slot(slot).begin()), "SRAM payload round trip");
    }
    auto raw = saves.bytes(); raw[0] ^= 1;
    thor::Sram recovered(raw); require(recovered.valid(0), "backup checksum accepted");
    raw[0x1FF0] ^= 1; require(!thor::Sram(raw).valid(0), "both bad checksums rejected");
    require(thor::Sram(raw).valid(1), "corruption isolated to slot");
    require(thor::Sram(saves.bytes()).bytes() == saves.bytes(), "byte-exact import/export");
    rejects([&] { thor::Sram(std::vector<uint8_t>(8191)); }, "wrong SRAM size");
    rejects([&] { saves.valid(3); }, "invalid SRAM slot");
}
void content(const std::string& path) {
    auto rom = thor::Rom::from_file(path);
    auto tile = std::vector<uint8_t>(32); tile[0] = 0x80; tile[17] = 0x80;
    require(thor::tile_pixel(tile, 0, 0, 0) == 9, "SNES bitplanes");
    require(thor::tile_pixel(tile, 0, 1, 0) == 0, "SNES pixel bit ordering");
    rejects([&] { rom.byte(0x7E0000); }, "WRAM is not ROM");
    rejects([&] { rom.slice(0xDFFFFF, 2); }, "ROM boundary");
    thor::Room geometry;
    geometry.width = geometry.height = 32;
    geometry.blocks = {0x1000, 0x5000, 0xD000, 0x8000};
    geometry.bts = {0, 255, 255, 0};
    require(!thor::room_solid_pixel(geometry, rom, 2, 2), "half-height slope upper half is air");
    require(thor::room_solid_pixel(geometry, rom, 2, 12), "half-height slope lower half solid");
    require(!thor::room_solid_pixel(geometry, rom, 18, 2), "horizontal extension redirects to slope");
    require(thor::room_solid_pixel(geometry, rom, 2, 28), "vertical extension redirects to slope");
    geometry.bts[0] = 0x80;
    require(thor::room_solid_pixel(geometry, rom, 2, 2), "vertical slope flip");
    geometry.bts[0] = 0x41;
    require(thor::room_solid_pixel(geometry, rom, 2, 2), "horizontal square-slope flip");
    require(!thor::room_solid_pixel(geometry, rom, 12, 2), "horizontal square-slope air half");
    geometry.bts[0] = 0x12;
    require(!thor::room_solid_pixel(geometry, rom, 0, 15), "triangle empty column");
    require(thor::room_solid_pixel(geometry, rom, 15, 1), "triangle surface");
    geometry.bts[1] = 0;
    require(!thor::room_solid_pixel(geometry, rom, 18, 12), "zero extension behaves as air");
    geometry.blocks[0] = 0x5000; geometry.bts[0] = 1; geometry.bts[1] = 255;
    require(thor::room_solid_pixel(geometry, rom, 2, 2), "extension cycle bounded");
    require(thor::room_solid_pixel(geometry, rom, -1, 0), "negative coordinates blocked");
    thor::Progression progression;
    const auto landing = thor::reference::rooms[0].header;
    require(thor::select_room_state(rom, landing, progression) == 0x8F9213, "landing default state");
    progression.events.set(0);
    require(thor::select_room_state(rom, landing, progression) == 0x8F922D, "landing awakened state");
    progression.max_power_bombs = 5;
    require(thor::select_room_state(rom, landing, progression) == 0x8F9247, "power bombs take priority over awakening");
    progression.events.set(14);
    require(thor::select_room_state(rom, landing, progression) == 0x8F9261, "escape takes priority over inventory");
    for (size_t i = 0; i < std::size(thor::reference::rooms); ++i) {
        auto room = thor::load_room(rom, i);
        require(room.state == thor::reference::rooms[i].state, "native condition chain matches reference default state");
        require(room.blocks.size() == size_t(room.width / 16) * (room.height / 16), "room block geometry");
        require(room.foreground.pixels.size() == size_t(room.width) * room.height, "room image geometry");
        require(room.doors.size() == thor::reference::rooms[i].door_count, "bounded source door list");
        for (const auto& door : room.doors) {
            require(door.address >= 0x838000 && door.address <= 0x83FFF4, "door header in bank 83");
            require(!door.destination || (door.destination_index < std::size(thor::reference::rooms) &&
                thor::reference::rooms[door.destination_index].header == door.destination), "door graph resolves destination");
        }
        if (i % 50 == 0) std::cout << "Decoded room " << i << ": " << room.name << std::endl;
    }
    for (int pose : {1, 2, 9, 10}) for (int frame = 0; frame < (pose > 2 ? 8 : 1); ++frame) {
        auto sprite = thor::draw_samus(rom, pose, frame);
        require(std::count_if(sprite.pixels.begin(), sprite.pixels.end(), [](auto p) { return p != 0; }) > 50, "Samus artwork decoded");
    }
    thor::Session a(thor::Rom::from_file(path)), b(thor::Rom::from_file(path));
    for (int i = 0; i <= 600; ++i) { a.buttons(thor::Right); a.advance(uint64_t(i) * 1000000000ULL / 60); }
    for (int i = 0; i <= 1200; ++i) { b.buttons(thor::Right); b.advance(uint64_t(i) * 1000000000ULL / 120); }
    const auto x = a.state(), y = b.state();
    require(std::tie(x.x,x.y,x.vx,x.vy,x.tick) == std::tie(y.x,y.y,y.vx,y.vy,y.tick), "same input gives identical 60/120 Hz simulation");
    a.select_room(1); const auto snap = a.snapshot();
    require(snap.generation != x.generation, "room transition resets generation");
    require(std::abs(snap.x - a.state().x / 65536.f) < .001f, "no interpolation across room transition");
    a.toggle_pause(); const auto paused = a.state();
    a.step(thor::Right | thor::Jump);
    require(a.state().tick == paused.tick && a.state().x == paused.x && a.state().y == paused.y, "pause freezes authoritative simulation");
    a.toggle_pause(); a.advance(100000000000ULL);
    require(a.state().tick == paused.tick, "session resume primes clock without catch-up");
    a.select_room(2);
    const int px = a.state().x / 65536, py = a.state().y / 65536;
    require(!thor::room_solid_pixel(a.room(), a.rom(), px - 5, py - 16) &&
            !thor::room_solid_pixel(a.room(), a.rom(), px + 4, py - 16) &&
            !thor::room_solid_pixel(a.room(), a.rom(), px - 5, py + 15) &&
            !thor::room_solid_pixel(a.room(), a.rom(), px + 4, py + 15), "Parlor viewer spawn clears its standing bounds");
    std::cout << "ROM fixtures: " << std::size(thor::reference::rooms) << " rooms, 18 Samus poses/frames" << std::endl;
}
void doors(const std::string& path) {
    thor::Session session(thor::Rom::from_file(path));
    const auto landing = session.room().doors;
    require(landing.size() == 4 && landing[0].address == 0x838916 && landing[0].destination == 0x8F92FD,
        "Landing Site exit 0 reaches Parlor");
    require(landing[0].direction == 5 && landing[0].cap_x == 0x4E && landing[0].cap_y == 6 &&
        landing[0].screen_x == 4 && landing[0].screen_y == 0 && landing[0].transition_speed == 0x8000,
        "Parlor entry coordinates and default transition speed decoded");
    session.step(thor::Right);
    session.toggle_pause();
    const auto before = session.state();
    session.traverse_door(0, 0);
    require(session.room_index() == 2 && session.room().header == 0x8F92FD, "native development door changes room");
    require(session.progression().entering_door == 0x8916, "source door pointer supplied to destination state selector");
    require(session.state().paused && session.state().tick == before.tick, "door traversal preserves tick and pause");
    require(session.state().generation == before.generation + 1, "door traversal changes render generation");
    require(session.state().vx == 0 && session.state().vy == 0, "provisional spawn clears velocity");
    auto safe = [&] {
        const auto& state = session.state();
        const int x = state.x / 65536, y = state.y / 65536;
        for (int cy = y - 16; cy <= y + 15; ++cy)
            for (int cx = x - 5; cx <= x + 4; ++cx)
                if (thor::room_solid_pixel(session.room(), session.rom(), cx, cy)) return false;
        return true;
    };
    require(safe(), "door spawn clears full standing rectangle");
    require(session.state().x / 65536 / 256 == 4 && session.state().y / 65536 / 256 == 0,
        "Parlor spawn stays in destination screen");
    require(session.snapshot().x == session.state().x / 65536.f, "no interpolation across door load");
    const auto arrived = session.state();
    rejects([&] { session.traverse_door(0, 1); }, "stale source room rejected");
    rejects([&] { session.traverse_door(2, 99); }, "invalid door rejected");
    require(session.room_index() == 2 && session.state().generation == arrived.generation &&
        session.state().x == arrived.x && session.progression().entering_door == 0x8916,
        "rejected traversal preserves session");
    session.traverse_door(2, 1);
    require(session.room_index() == 0 && safe(), "reverse Parlor door returns safely to Landing Site");
    require(session.state().x / 65536 / 256 == 0 && session.state().y / 65536 / 256 == 4,
        "return uses destination door screen instead of room-center spawn");
    session.traverse_door(0, 0);
    session.traverse_door(2, 4);
    require(session.room().name == "Climb" && safe(), "vertical Parlor exit reaches Climb safely");
    session.traverse_door(session.room_index(), 0);
    require(session.room_index() == 2 && safe(), "upward Climb return with custom ASM uses safe provisional spawn");
    session.traverse_door(2, 4);
    session.traverse_door(session.room_index(), 3);
    require(session.room().name == "Pit" && safe(), "Climb connected-room link reaches Pit");
    session.traverse_door(session.room_index(), 1);
    require(session.room().name == "ElevToBlueBrinstar" && safe(), "Pit link reaches elevator room");
    const auto elevator = session.state();
    rejects([&] { session.traverse_door(session.room_index(), 1); }, "elevator behavior explicitly rejected");
    rejects([&] { session.traverse_door(session.room_index(), 2); }, "null special transition rejected");
    require(session.state().generation == elevator.generation && session.state().x == elevator.x,
        "unsupported transitions leave session intact");
    session.toggle_pause();
    session.advance(100000000000ULL);
    require(session.state().tick == before.tick, "door load primes clock without elapsed-time catch-up");
    session.advance(100016640000ULL);
    require(session.state().vx == 0, "door load releases held movement input");
    std::cout << "Door fixtures: Landing Site / Parlor / Climb / Pit / elevator boundary" << std::endl;
}
}
int main(int argc, char** argv) {
    try {
        decompression(); clocks(); saves();
        if (argc > 1) { content(argv[1]); doors(argv[1]); }
        std::cout << "PASS: " << checks << " checks" << std::endl;
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL after " << checks << " checks: " << e.what() << std::endl; return 1; }
}
