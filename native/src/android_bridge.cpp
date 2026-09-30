#include "thor/session.hpp"
#include "thor/renderer.hpp"
#include "reference_index.hpp"
#include <jni.h>
#include <mutex>
#include <sstream>
#include <android/log.h>

namespace {
std::mutex mutex;
std::unique_ptr<thor::Session> session;
thor::Renderer renderer; // Only used by the GLSurfaceView GL thread.
std::unique_ptr<thor::Sram> sram;
unsigned selected_slot = 0;
bool widescreen = true, interpolation = true;
uint32_t next_session_generation = 1000;
void error(JNIEnv* env, const std::exception& e) {
    __android_log_print(ANDROID_LOG_ERROR, "ThorNative", "%s", e.what());
    env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), e.what());
}
jstring text(JNIEnv* env, const std::string& s) { return env->NewStringUTF(s.c_str()); }
std::vector<uint8_t> bytes(JNIEnv* env, jbyteArray value) {
    std::vector<uint8_t> result(env->GetArrayLength(value));
    env->GetByteArrayRegion(value, 0, jsize(result.size()), reinterpret_cast<jbyte*>(result.data()));
    return result;
}
}
extern "C" {
JNIEXPORT void JNICALL Java_org_supermetroid_thor_NativeBridge_loadRom(JNIEnv* env, jobject, jbyteArray data) {
    try {
        auto next = std::make_unique<thor::Session>(thor::Rom(bytes(env, data)));
        std::lock_guard lock(mutex); session = std::move(next); if (!sram) sram = std::make_unique<thor::Sram>(); next_session_generation += 1000;
    } catch (const std::exception& e) { error(env, e); }
}
JNIEXPORT void JNICALL Java_org_supermetroid_thor_NativeBridge_advance(JNIEnv* env, jobject, jlong timestamp, jint buttons) {
    try { std::lock_guard lock(mutex); if (session) { session->buttons(uint16_t(buttons)); session->advance(uint64_t(timestamp)); } }
    catch (const std::exception& e) { error(env, e); }
}
JNIEXPORT void JNICALL Java_org_supermetroid_thor_NativeBridge_suspend(JNIEnv*, jobject) {
    std::lock_guard lock(mutex); if (session) session->suspend();
}
JNIEXPORT void JNICALL Java_org_supermetroid_thor_NativeBridge_pause(JNIEnv*, jobject) {
    std::lock_guard lock(mutex); if (session) session->toggle_pause();
}
JNIEXPORT void JNICALL Java_org_supermetroid_thor_NativeBridge_options(JNIEnv*, jobject, jboolean wide, jboolean interpolate) {
    std::lock_guard lock(mutex); widescreen = wide; interpolation = interpolate;
}
JNIEXPORT void JNICALL Java_org_supermetroid_thor_NativeBridge_selectRoom(JNIEnv* env, jobject, jint index) {
    try { std::lock_guard lock(mutex); if (session) session->select_room(size_t(index)); }
    catch (const std::exception& e) { error(env, e); }
}
JNIEXPORT void JNICALL Java_org_supermetroid_thor_NativeBridge_newGame(JNIEnv* env, jobject) {
    try { std::lock_guard lock(mutex); if (session) session->new_game(); }
    catch (const std::exception& e) { error(env, e); }
}
JNIEXPORT void JNICALL Java_org_supermetroid_thor_NativeBridge_loadGame(JNIEnv* env, jobject) {
    try { std::lock_guard lock(mutex); if (!session || !sram) throw std::runtime_error("Load a ROM and SRAM first"); session->load_game(*sram, selected_slot); }
    catch (const std::exception& e) { error(env, e); }
}
JNIEXPORT void JNICALL Java_org_supermetroid_thor_NativeBridge_saveGame(JNIEnv* env, jobject) {
    try { std::lock_guard lock(mutex); if (!session || !sram) throw std::runtime_error("Load a ROM first"); session->save_game(*sram, selected_slot); }
    catch (const std::exception& e) { error(env, e); }
}
JNIEXPORT jstring JNICALL Java_org_supermetroid_thor_NativeBridge_rooms(JNIEnv* env, jobject) {
    std::ostringstream out; out << "[";
    for (size_t i = 0; i < std::size(thor::reference::rooms); ++i) {
        if (i) out << ',';
        out << '"' << thor::reference::rooms[i].name << '"';
    }
    out << ']'; return text(env, out.str());
}
JNIEXPORT void JNICALL Java_org_supermetroid_thor_NativeBridge_traverseDoor(JNIEnv* env, jobject, jint room, jint door) {
    try { std::lock_guard lock(mutex); if (session) session->traverse_door(size_t(room), size_t(door)); }
    catch (const std::exception& e) { error(env, e); }
}
JNIEXPORT jstring JNICALL Java_org_supermetroid_thor_NativeBridge_doors(JNIEnv* env, jobject) {
    std::lock_guard lock(mutex);
    if (!session) return text(env, "{\"roomIndex\":-1,\"doors\":[]}");
    std::ostringstream out; out << "{\"roomIndex\":" << session->room_index() << ",\"doors\":[";
    const auto& doors = session->room().doors;
    for (size_t i = 0; i < doors.size(); ++i) {
        const auto& d = doors[i]; if (i) out << ',';
        out << "{\"index\":" << i << ",\"destination\":\""
            << (d.destination ? thor::reference::rooms[d.destination_index].name : "Special transition")
            << "\",\"direction\":" << unsigned(d.direction & 3)
            << ",\"supported\":" << (d.destination && !(d.properties & 0x80) && d.direction <= 7 ? "true" : "false")
            << ",\"scripted\":" << (d.custom_asm ? "true" : "false") << '}';
    }
    out << "]}"; return text(env, out.str());
}
JNIEXPORT jstring JNICALL Java_org_supermetroid_thor_NativeBridge_companion(JNIEnv* env, jobject) {
    std::lock_guard lock(mutex);
    std::ostringstream out;
    if (!session) return text(env, "{\"loaded\":false}");
    const auto& s = session->state(); const auto& r = session->room(); const auto& p = session->progression();
    unsigned active_beams = 0;
    for (const auto& beam : session->gameplay().beams()) if (beam.active) ++active_beams;
    out << "{\"loaded\":true,\"room\":\"" << r.name << "\",\"roomIndex\":" << session->room_index()
        << ",\"area\":" << r.area << ",\"width\":" << r.width << ",\"height\":" << r.height
        << ",\"x\":" << s.x / 65536.f << ",\"y\":" << s.y / 65536.f << ",\"tick\":" << s.tick
        << ",\"alpha\":" << session->alpha() << ",\"paused\":" << (s.paused ? "true" : "false")
        << ",\"activeShots\":" << active_beams << ",\"openingDoors\":" << session->gameplay().doors().size()
        << ",\"health\":" << p.health << ",\"maxHealth\":" << p.max_health
        << ",\"missiles\":" << p.missiles << ",\"maxMissiles\":" << p.max_missiles
        << ",\"items\":" << p.collected_items << ",\"selectedMissiles\":" << (s.missile_selected ? "true" : "false")
        << ",\"dead\":" << (s.dead ? "true" : "false") << ",\"saveAvailable\":" << (session->save_station() >= 0 ? "true" : "false")
        << ",\"explored\":[";
    bool first = true;
    for (auto cell : session->explored()) { if (!first) out << ','; out << cell; first = false; }
    out << "],\"sram\":";
    if (!sram) out << "null";
    else {
        const auto saved = sram->summary(selected_slot);
        out << "{\"slot\":" << selected_slot << ",\"valid\":" << (saved.valid ? "true" : "false")
            << ",\"health\":" << saved.health << ",\"maxHealth\":" << saved.max_health
            << ",\"missiles\":" << saved.missiles << ",\"maxMissiles\":" << saved.max_missiles
            << ",\"supers\":" << saved.supers << ",\"powerBombs\":" << saved.power_bombs
            << ",\"reserve\":" << saved.reserve << ",\"items\":" << saved.collected_items
            << ",\"beams\":" << saved.collected_beams << ",\"area\":" << saved.area << "}";
    }
    out << '}'; return text(env, out.str());
}
JNIEXPORT void JNICALL Java_org_supermetroid_thor_NativeBridge_importSram(JNIEnv* env, jobject, jbyteArray data) {
    try { auto next = std::make_unique<thor::Sram>(bytes(env, data)); std::lock_guard lock(mutex); sram = std::move(next); }
    catch (const std::exception& e) { error(env, e); }
}
JNIEXPORT jbyteArray JNICALL Java_org_supermetroid_thor_NativeBridge_exportSram(JNIEnv* env, jobject) {
    std::lock_guard lock(mutex); if (!sram) return env->NewByteArray(0);
    auto result = env->NewByteArray(thor::Sram::size);
    env->SetByteArrayRegion(result, 0, thor::Sram::size, reinterpret_cast<const jbyte*>(sram->bytes().data())); return result;
}
JNIEXPORT void JNICALL Java_org_supermetroid_thor_NativeBridge_selectSlot(JNIEnv* env, jobject, jint slot) {
    if (slot < 0 || slot > 2) { env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), "Invalid SRAM slot"); return; }
    std::lock_guard lock(mutex); selected_slot = unsigned(slot);
}
JNIEXPORT void JNICALL Java_org_supermetroid_thor_NativeBridge_glInit(JNIEnv* env, jobject) {
    try { renderer.initialize(); } catch (const std::exception& e) { error(env, e); }
}
JNIEXPORT void JNICALL Java_org_supermetroid_thor_NativeBridge_glResize(JNIEnv*, jobject, jint w, jint h) { renderer.resize(w, h); }
JNIEXPORT void JNICALL Java_org_supermetroid_thor_NativeBridge_glDraw(JNIEnv* env, jobject) {
    try {
        thor::RenderSnapshot snapshot;
        thor::Image next_room, next_high, next_background, next_sprite;
        uint8_t layer_x = 0, layer_y = 0;
        std::vector<std::pair<uint64_t, thor::Image>> art;
        std::array<thor::Image, 30> next_beams;
        uint32_t revision = 0;
        bool wide, have_room = false, have_sprite = false, have_beams = false;
        {
            std::lock_guard lock(mutex);
            if (!session) { renderer.draw({}, true); return; }
            snapshot = session->snapshot(interpolation); snapshot.generation += next_session_generation;
            wide = widescreen;
            revision = session->room().visual_revision;
            if (renderer.generation != snapshot.generation || renderer.room_revision != revision) {
                next_room = session->room().foreground_low; next_high = session->room().foreground_high;
                next_background = session->room().background; layer_x = session->room().layer2_x; layer_y = session->room().layer2_y; have_room = true;
            }
            if (!renderer.beam_ready) {
                for (int i = 0; i < 10; ++i) {
                    next_beams[i] = thor::draw_power_beam(session->rom(), i);
                    const auto missile = 0x930000u | session->rom().word(thor::reference::ProjectileDataTable_NonBeam_Missile + 2 + i * 2);
                    next_beams[i + 10] = thor::draw_enemy_shot(session->rom(), 0x930000u | session->rom().word(missile + 2));
                    const auto bomb = 0x930000u | session->rom().word(thor::reference::ProjectileDataTable_NonBeam_Bomb + 2);
                    next_beams[i + 20] = thor::draw_enemy_shot(session->rom(), 0x930000u | session->rom().word(bomb + 2));
                }
                have_beams = true;
            }
            const bool changing = renderer.generation != snapshot.generation;
            std::set<uint64_t> seen;
            for (const auto& enemy : snapshot.enemies) {
                const uint64_t key = (uint64_t(enemy.kind) << 32) | enemy.map;
                if ((changing || !renderer.has_art(key)) && seen.insert(key).second)
                    art.emplace_back(key, thor::draw_enemy(session->rom(), enemy.kind, enemy.map, enemy.behavior));
            }
            for (const auto& shot : snapshot.enemy_shots)
                if ((changing || !renderer.has_art(shot.map)) && seen.insert(shot.map).second)
                    art.emplace_back(shot.map, thor::draw_enemy_shot(session->rom(), shot.map));
            if (renderer.pose != snapshot.pose || renderer.frame != snapshot.frame) {
                next_sprite = thor::draw_samus(session->rom(), snapshot.pose, snapshot.frame); have_sprite = true;
            }
        }
        if (have_room) renderer.room(next_room, next_high, next_background, layer_x, layer_y, snapshot.generation, revision);
        if (have_sprite) renderer.samus(next_sprite, snapshot.pose, snapshot.frame);
        if (have_beams) renderer.power_beam(next_beams);
        for (const auto& entry : art) renderer.art(entry.first, entry.second);
        renderer.draw(snapshot, wide);
    } catch (const std::exception& e) { error(env, e); }
}
}
