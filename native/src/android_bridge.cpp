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
        std::lock_guard lock(mutex); session = std::move(next); next_session_generation += 1000;
    } catch (const std::exception& e) { error(env, e); }
}
JNIEXPORT void JNICALL Java_org_supermetroid_thor_NativeBridge_advance(JNIEnv*, jobject, jlong timestamp, jint buttons) {
    std::lock_guard lock(mutex);
    if (session) { session->buttons(uint16_t(buttons)); session->advance(uint64_t(timestamp)); }
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
    const auto& s = session->state(); const auto& r = session->room();
    out << "{\"loaded\":true,\"room\":\"" << r.name << "\",\"roomIndex\":" << session->room_index()
        << ",\"area\":" << r.area << ",\"width\":" << r.width << ",\"height\":" << r.height
        << ",\"x\":" << s.x / 65536.f << ",\"y\":" << s.y / 65536.f << ",\"tick\":" << s.tick
        << ",\"alpha\":" << session->alpha() << ",\"paused\":" << (s.paused ? "true" : "false") << ",\"explored\":[";
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
        thor::Image next_room, next_sprite;
        bool wide, have_room = false, have_sprite = false;
        {
            std::lock_guard lock(mutex);
            if (!session) { renderer.draw({}, true); return; }
            snapshot = session->snapshot(interpolation); snapshot.generation += next_session_generation;
            wide = widescreen;
            if (renderer.generation != snapshot.generation) { next_room = session->room().foreground; have_room = true; }
            if (renderer.pose != snapshot.pose || renderer.frame != snapshot.frame) {
                next_sprite = thor::draw_samus(session->rom(), snapshot.pose, snapshot.frame); have_sprite = true;
            }
        }
        if (have_room) renderer.room(next_room, snapshot.generation);
        if (have_sprite) renderer.samus(next_sprite, snapshot.pose, snapshot.frame);
        renderer.draw(snapshot, wide);
    } catch (const std::exception& e) { error(env, e); }
}
}
