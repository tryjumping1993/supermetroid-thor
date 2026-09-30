#include "thor/save_game.hpp"
#include "thor/enemies.hpp"
#include "thor/game.hpp"
#include "thor/clock.hpp"
#include "thor/content.hpp"
#include "thor/session.hpp"
#include "thor/sram.hpp"
#include "reference_index.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
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
void gameplay(const std::string& path) {
    const auto rom = thor::Rom::from_file(path);
    constexpr int32_t pixel = 65536;
    auto empty_room = [] {
        thor::Room room; room.width = room.height = 256;
        room.blocks.resize(256); room.bts.resize(256); return room;
    };
    for (int direction = 0; direction < 10; ++direction) {
        const auto image = thor::draw_power_beam(rom, direction);
        require(image.width == 16 && image.height == 16 &&
            std::any_of(image.pixels.begin(), image.pixels.end(), [](auto c) { return c != 0; }), "beam artwork decoded for direction");
    }
    thor::RoomGameplay shots;
    auto room = empty_room();
    require(shots.fire(rom, 64 * pixel, 64 * pixel, 2, true), "new press fires Power Beam");
    require(shots.beams()[0].vx == 4 * pixel && shots.beams()[0].vy == 0 && shots.beams()[0].damage == 20,
        "Power Beam speed and damage read from original tables");
    require(shots.cooldown() == 15 && !shots.fire(rom, 64 * pixel, 64 * pixel, 2, true), "shot cooldown blocks repeated firing");
    const auto initial_x = shots.beams()[0].x;
    shots.tick(room, rom, 0, 0);
    require(shots.beams()[0].x == initial_x + 4 * pixel, "beam advances one native tick");
    for (int i = 1; i < 15; ++i) shots.tick(room, rom, 0, 0);
    require(shots.fire(rom, 64 * pixel, 64 * pixel, 1, false) && shots.cooldown() == 25,
        "held firing uses original auto-fire cooldown");
    require(shots.beams()[1].vx == 0x2AB * 256 && shots.beams()[1].vy == -0x2AB * 256,
        "diagonal beam uses original diagonal speed");

    // Strike an extension segment rather than the root, in all orientations.
    for (unsigned orientation = 0; orientation < 4; ++orientation) {
        shots.reset(); room = empty_room();
        const size_t root = 4 * 16 + 8, stride = orientation < 2 ? 16 : 1;
        room.blocks[root] = 0xC000; room.bts[root] = uint8_t(0x40 + orientation);
        for (size_t i = 1; i < 4; ++i) {
            room.blocks[root + i * stride] = orientation < 2 ? 0xD000 : 0x5000;
            room.bts[root + i * stride] = uint8_t(-int(i));
        }
        const bool vertical = orientation < 2;
        require(shots.fire(rom, (vertical ? 96 : 168) * pixel, 104 * pixel,
            vertical ? 2 : 0, true), "beam fired toward blue cap extension");
        for (int i = 0; i < 12 && shots.doors().empty(); ++i) shots.tick(room, rom, 0, 0);
        require(shots.doors().size() == 1 && shots.doors()[0].block == root && shots.doors()[0].timer == 6,
            "extension resolves to blue door root and starts opening");
        require(!shots.beams()[0].active && room.visual_revision == 1, "door absorbs beam and invalidates room texture");
        for (int i = 0; i < 17; ++i) shots.tick(room, rom, 0, 0);
        require(shots.doors()[0].stage == 2 && room_solid_pixel(room, rom, 136, 72), "cap remains solid during opening frames");
        shots.tick(room, rom, 0, 0);
        require(shots.doors()[0].stage == 3 && room.visual_revision == 4, "three six-tick frames reach open cap");
        for (size_t i = 0; i < 4; ++i)
            require((room.blocks[root + i * stride] >> 12) == 0, "all cap segments become air");
        for (int i = 0; i < 94; ++i) shots.tick(room, rom, 0, 0);
        require(shots.doors().empty() && !room_solid_pixel(room, rom, 136, 72), "effect retirement leaves door open");
    }
    shots.reset(); room = empty_room(); room.blocks[4 * 16 + 8] = 0xC000; room.bts[4 * 16 + 8] = 0x44;
    shots.fire(rom, 96 * pixel, 72 * pixel, 2, true);
    for (int i = 0; i < 12; ++i) shots.tick(room, rom, 0, 0);
    require(shots.doors().empty() && room.blocks[4 * 16 + 8] == 0xC000, "Power Beam does not bypass coloured cap");
    shots.reset(); room = empty_room();
    room.width = 1024; room.blocks.resize(1024); room.bts.resize(1024);
    for (int i = 0; i < 5; ++i) {
        require(shots.fire(rom, 100 * pixel, 100 * pixel, 2, true), "available projectile slot fires");
        for (int tick = 0; tick < 15; ++tick)
            shots.tick(room, rom, shots.beams()[0].x / float(pixel) - 256, 0);
    }
    require(!shots.fire(rom, 100 * pixel, 100 * pixel, 2, true), "five active projectiles prevent a sixth shot");
    // Use an actual decoded room to exercise tile redraw and session integration.
    auto decoded = thor::load_room(rom, 2);
    const auto before = decoded.foreground.pixels;
    const auto root = size_t(6 * (decoded.width / 16) + 4);
    decoded.blocks[root] = 0xC000; decoded.bts[root] = 0x40;
    for (size_t i = 1; i < 4; ++i) { decoded.blocks[root + i * (decoded.width / 16)] = 0xD000; decoded.bts[root + i * (decoded.width / 16)] = uint8_t(-int(i)); }
    shots.reset(); shots.fire(rom, 40 * pixel, 104 * pixel, 2, true);
    for (int i = 0; i < 12; ++i) shots.tick(decoded, rom, 0, 0);
    require(decoded.visual_revision > 0 && decoded.foreground.pixels != before, "door phases redraw original room tiles");
    thor::Session session(thor::Rom::from_file(path));
    session.step(thor::Shoot);
    require(!session.snapshot(false).beams.empty(), "session publishes fired beam to renderer");
    session.toggle_pause(); const auto frozen = session.gameplay().beams()[0];
    session.step(thor::Shoot);
    require(session.gameplay().beams()[0].x == frozen.x && session.gameplay().cooldown() == 15, "pause freezes beam and cooldown");
    session.select_room(2);
    require(session.snapshot(false).beams.empty() && session.gameplay().doors().empty(), "room selection resets transient gameplay");
    std::cout << "Gameplay smoke: Power Beam / four blue cap orientations / pause / room reset" << std::endl;
}
void milestone2(const std::string& path) {
    const auto rom = thor::Rom::from_file(path);
    const auto station = thor::load_station(rom, 0, 0);
    require(station.x == 1152 && station.y == 1088 && station.camera_x == 1024, "native new game uses original ship load station");
    thor::Session session(thor::Rom::from_file(path));
    require(session.save_station() == 0 && session.state().x == station.x * 65536, "ship save available at original spawn");
    thor::Sram saved; session.save_game(saved, 1);
    require(saved.valid(1) && !saved.valid(0), "native station save writes selected slot only");
    thor::Progression p; p.equipped_items = p.collected_items = 0x1004; p.max_missiles = p.missiles = 5;
    p.events.set(0); p.items.set(26); p.opened_doors.set(29); p.bosses[0] = 4;
    p.map[0] = 0x80;
    thor::save_progression(rom, saved, 1, p, 0, 1);
    const auto restored = thor::restore_progression(rom, saved, 1);
    require(restored.items == p.items && restored.opened_doors == p.opened_doors && restored.events == p.events && restored.bosses == p.bosses && restored.missiles == 5, "vanilla payload restores item, door, event and boss progression");
    session.load_game(saved, 1);
    require(session.room().name == "CrateriaSave" && session.state().x / 65536 == 96 && session.state().y / 65536 == 152, "native load restores original station placement");
    p = {};
    auto morph = thor::load_room(rom, thor::room_index_from_header(0x8F9E9F), p);
    thor::RoomGameplay gameplay; gameplay.load(morph, rom, p);
    auto found = std::find_if(gameplay.pickups().begin(), gameplay.pickups().end(), [](const auto& item) { return item.item == 19; });
    require(found != gameplay.pickups().end(), "Morph Ball PLM decoded from original population");
    const auto root = found->root;
    require(found->revealed, "Morph Ball is initially exposed");
    gameplay.touch(morph, rom, int(root % (morph.width / 16)) * 16 + 8, int(root / (morph.width / 16)) * 16 + 8, 21);
    require((p.equipped_items & 4) && p.items.test(26), "Morph Ball collision updates authoritative inventory and item bit");
    auto missile = thor::load_room(rom, thor::room_index_from_header(0x8FA107), p);
    gameplay.load(missile, rom, p);
    require(!gameplay.pickups().empty() && !gameplay.pickups()[0].revealed, "first missile requires shooting Chozo orb");
    const auto mr = gameplay.pickups()[0].root; const int mx = int(mr % (missile.width / 16)) * 16 + 8, my = int(mr / (missile.width / 16)) * 16 + 8;
    gameplay.fire(rom, (mx + 32) * 65536, my * 65536, 7, true);
    for (int i = 0; i < 12; ++i) gameplay.tick(missile, rom, 0, 0);
    gameplay.touch(missile, rom, mx, my, 21);
    require(p.max_missiles == 5 && p.missiles == 5 && p.items.test(34), "first missile orb and pickup grant five missiles");
    auto pit = thor::load_room(rom, thor::room_index_from_header(0x8F975C), p);
    thor::Enemies enemies; gameplay.load(pit, rom, p); enemies.load(pit, rom, p);
    require(enemies.list().size() == 5 && pit.enemy_quota == 5, "Morph Ball plus missiles selects original five-pirate Pit population");
    for (int i = 0; i < 300; ++i) { gameplay.tick(pit, rom, 0, 0); enemies.tick(pit, rom, p, gameplay, 104, 112, 21, 0, 0, i); }
    for (const auto& e : enemies.list()) if (e.map) {
        const auto image = thor::draw_enemy(rom, e.kind, e.map);
        require(std::any_of(image.pixels.begin(), image.pixels.end(), [](auto c) { return c != 0; }), "pirate original sprite pixels decoded");
    }
    auto boss_room = thor::load_room(rom, thor::room_index_from_header(0x8F9804), p);
    gameplay.load(boss_room, rom, p); enemies.load(boss_room, rom, p);
    require(enemies.list().size() == 1 && enemies.list()[0].health == 800, "Bomb Torizo native header and health");
    p.equipped_items |= 0x1000;
    for (int i = 0; i < 2400; ++i) {
        gameplay.tick(boss_room, rom, 0, 0);
        enemies.tick(boss_room, rom, p, gameplay, (i / 240) % 2 ? 64 : 196, 160, 21, 0, 0, i);
        if (i > 1100 && i % 15 == 0) {
            auto& beam = gameplay.beams()[0]; beam = {}; beam.active = true; beam.damage = 20;
            beam.x = enemies.list()[0].x; beam.y = enemies.list()[0].y;
            enemies.tick(boss_room, rom, p, gameplay, 64, 160, 21, 0, 0, i);
        }
    }
    require(!gameplay.statue_active() && (p.bosses[0] & 4) && !enemies.list()[0].active, "native Bomb Torizo wakes, takes real projectile damage and completes original death script");
    std::cout << "Milestone 2 focused smoke: stations / SRAM / Morph Ball / missile / pirates / Bomb Torizo" << std::endl;
}
void wram(const std::string& path) {
    const auto rom = thor::Rom::from_file(path);
    thor::Game game(rom);
    game.Enemy_XPosition(0x40) = 0x1234;
    require(game.word_at(0x0F7A + 0x40) == 0x1234, "Enemy.XPosition indexed by enemy offset");
    game.Boyon_speedMultiplier(0x80) = 7;
    require(game.word_at(0x0FA8 + 0x80) == 7, "species overlay shares enemy RAM");
    require(thor::Game::Boyon_initialBounceSpeedTableIndex_address == 0x7800, "extra enemy variables live at $7E7800");
    game.SamusXPosition() = 0xFFFE; game.SamusXPosition() += 3;
    require(game.SamusXPosition() == 1, "16-bit proxy wraps like the accumulator");
    game.long_at(0x100) = 0xABCDEF;
    require(game.byte_at(0x101) == 0xCD, "long access is little-endian");
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
// Replays recorded raw controller input (hex buttons, frame count) through
// the native session: new game -> Landing Site -> Parlor -> Climb -> Pit ->
// Morph Ball -> first missile -> Flyway red door -> Bomb Torizo.
void route(const std::string& rom_path, const std::string& input_path) {
    thor::Session session(thor::Rom::from_file(rom_path));
    std::ifstream input(input_path);
    require(bool(input), "route input file opens");
    unsigned buttons = 0, count = 0;
    std::string room;
    while (input >> std::hex >> buttons >> std::dec >> count) {
        for (unsigned i = 0; i < count; ++i) session.step(uint16_t(buttons));
        if (session.room().name != room) { room = session.room().name; std::cout << "  route reached " << room << std::endl; }
    }
    const auto& p = session.progression();
    require(session.room().name == "BombTorizo" && !session.state().dead, "route ends alive in the Bomb Torizo room");
    require((p.equipped_items & 4) && (p.equipped_items & 0x1000), "route collected Morph Ball and Bombs");
    require(p.max_missiles == 5 && p.opened_doors.any(), "route collected the first missile and opened the red door");
    require(p.bosses[0] & 4, "route defeated Bomb Torizo natively");
    std::cout << "Route smoke: Landing Site to Bomb Torizo" << std::endl;
}
}
int main(int argc, char** argv) {
    try {
        decompression(); clocks(); saves();
        if (argc > 1) {
            wram(argv[1]);
            if (argc > 3 && std::string(argv[2]) == "--route") route(argv[1], argv[3]);
            else if (argc > 2 && std::string(argv[2]) == "--milestone2") { gameplay(argv[1]); milestone2(argv[1]); }
            else if (argc > 2 && std::string(argv[2]) == "--gameplay") gameplay(argv[1]);
            else { content(argv[1]); doors(argv[1]); gameplay(argv[1]); }
        }
        std::cout << "PASS: " << checks << " checks" << std::endl;
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL after " << checks << " checks: " << e.what() << std::endl; return 1; }
}
