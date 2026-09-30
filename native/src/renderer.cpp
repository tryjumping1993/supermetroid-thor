#include "thor/renderer.hpp"
#include <stdexcept>
#include <string>
#include <cmath>
#include <algorithm>

namespace thor {
namespace {
GLuint shader(GLenum type, const char* source) {
    auto result = glCreateShader(type);
    glShaderSource(result, 1, &source, nullptr); glCompileShader(result);
    GLint okay = 0; glGetShaderiv(result, GL_COMPILE_STATUS, &okay);
    if (!okay) { char log[2048]; glGetShaderInfoLog(result, sizeof(log), nullptr, log); glDeleteShader(result); throw std::runtime_error(log); }
    return result;
}
}
void Renderer::initialize() {
    // Context loss invalidates all names. Do not reuse texture generation IDs.
    generation = room_revision = 0; pose = frame = -1; beam_ready = false;
    const auto vertex = shader(GL_VERTEX_SHADER, R"(#version 300 es
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
uniform vec2 viewSize;
out vec2 texCoord;
void main() { gl_Position=vec4(position.x/viewSize.x*2.0-1.0,1.0-position.y/viewSize.y*2.0,0,1); texCoord=uv; }
)");
    const auto fragment = shader(GL_FRAGMENT_SHADER, R"(#version 300 es
precision mediump float;
in vec2 texCoord;
uniform sampler2D image;
out vec4 color;
void main() { color=texture(image,texCoord); }
)");
    program_ = glCreateProgram(); glAttachShader(program_, vertex); glAttachShader(program_, fragment); glLinkProgram(program_);
    glDeleteShader(vertex); glDeleteShader(fragment);
    GLint okay; glGetProgramiv(program_, GL_LINK_STATUS, &okay);
    if (!okay) throw std::runtime_error("GL program link failed");
    view_uniform_ = glGetUniformLocation(program_, "viewSize");
    glGenVertexArrays(1, &vertex_array_); glBindVertexArray(vertex_array_);
    glGenBuffers(1, &buffer_); glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    art_.clear(); current_ = {}; old_ = {};
    for (auto* scene : {&current_, &old_}) { glGenTextures(1, &scene->low); glGenTextures(1, &scene->high); glGenTextures(1, &scene->background); }
    glGenTextures(1, &samus_texture_);
    glGenTextures(GLsizei(beam_textures_.size()), beam_textures_.data());
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
}
void Renderer::resize(int width, int height) { width_ = width; height_ = height; }
void Renderer::upload(GLuint texture, const Image& image) {
    GLint maximum; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    if (image.width > maximum || image.height > maximum) throw std::runtime_error("Room exceeds GPU texture dimensions");
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
}
void Renderer::room(const Image& low, const Image& high, const Image& background, uint8_t lx, uint8_t ly, uint32_t next, uint32_t revision) {
    if (generation != next) {
        std::swap(current_, old_);
        for (auto& entry : art_) glDeleteTextures(1, &entry.second.texture);
        art_.clear();
    }
    upload(current_.low, low); upload(current_.high, high);
    current_.width = low.width; current_.height = low.height; current_.layer_x = lx; current_.layer_y = ly;
    current_.bg_width = background.width; current_.bg_height = background.height;
    if (!background.pixels.empty()) upload(current_.background, background);
    generation = next; room_revision = revision;
}
void Renderer::art(uint64_t key, const Image& image) {
    GLuint texture; glGenTextures(1, &texture); upload(texture, image);
    art_.emplace(key, Art{texture, image.width, image.height});
}
void Renderer::samus(const Image& image, int next_pose, int next_frame) { upload(samus_texture_, image); pose = next_pose; frame = next_frame; }
void Renderer::power_beam(const std::array<Image, 30>& images) {
    for (size_t i = 0; i < images.size(); ++i) upload(beam_textures_[i], images[i]);
    beam_ready = true;
}
void Renderer::quad(GLuint texture, float x, float y, float width, float height) {
    const float vertices[]{x,y,0,0, x+width,y,1,0, x,y+height,0,1, x,y+height,0,1, x+width,y,1,0, x+width,y+height,1,1};
    glBindTexture(GL_TEXTURE_2D, texture); glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW); glDrawArrays(GL_TRIANGLES, 0, 6);
}
void Renderer::scene(bool old, float camera_x, float camera_y) {
    const auto& scene = old ? old_ : current_;
    if (scene.bg_width && scene.bg_height) {
        // Even scroll bytes are 8.8 multipliers; zero is the original half-rate.
        auto offset = [](float camera, uint8_t factor) { return camera * (factor ? float(factor & 0xFE) / 256 : .5f); };
        const float bx = offset(camera_x, scene.layer_x), by = offset(camera_y, scene.layer_y);
        if (scene.bg_width == 256 && scene.bg_height == 256) {
            const float x = -std::fmod(bx, 256.f), y = -std::fmod(by, 256.f);
            for (float yy = y - 256; yy < logical_height_; yy += 256)
                for (float xx = x - 256; xx < logical_width_; xx += 256) quad(scene.background, xx, yy, 256, 256);
        } else quad(scene.background, -bx, -by, float(scene.bg_width), float(scene.bg_height));
    }
    quad(scene.low, -camera_x, -camera_y, float(scene.width), float(scene.height));
}
void Renderer::draw(const RenderSnapshot& s, bool widescreen) {
    glViewport(0, 0, width_, height_); glDisable(GL_SCISSOR_TEST);
    glClearColor(0.025f, 0.035f, 0.055f, 1); glClear(GL_COLOR_BUFFER_BIT);
    if (!generation || height_ <= 0) return;
    // Original-width transitions keep room edges and activation consistent.
    const bool wide = widescreen && !s.transition_direction;
    logical_width_ = wide ? 224.f * width_ / height_ : 256.f;
    int viewport_x = 0, viewport_width = width_;
    if (!wide) { viewport_width = int(height_ * (256.f / 224.f)); viewport_x = (width_ - viewport_width) / 2; glViewport(viewport_x, 0, viewport_width, height_); }
    const float camera_x = s.camera_x - (logical_width_ - 256) / 2, camera_y = s.camera_y;
    glUseProgram(program_); glBindVertexArray(vertex_array_);
    glUniform2f(view_uniform_, logical_width_, logical_height_); glActiveTexture(GL_TEXTURE0);
    auto clip = [&](float x, float y, float w, float h) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(viewport_x + int(x / logical_width_ * viewport_width), int((224 - y - h) / 224 * height_),
            std::max(0, int(std::ceil(w / logical_width_ * viewport_width))), std::max(0, int(std::ceil(h / 224 * height_))));
    };
    if (s.transition_direction) {
        const unsigned direction = s.transition_direction - 1;
        const float progress = std::clamp(s.transition_progress, 0.f, 1.f);
        const float amount = (direction & 2 ? 224 : 256) * progress;
        if (direction == 0) clip(0, 0, 256 - amount, 224);
        else if (direction == 1) clip(amount, 0, 256 - amount, 224);
        else if (direction == 2) clip(0, 0, 256, 224 - amount);
        else clip(0, amount, 256, 224 - amount);
        const float old_x = s.source_x + (direction == 0 ? amount : direction == 1 ? -amount : 0);
        const float old_y = s.source_y + (direction == 2 ? amount : direction == 3 ? -amount : 0);
        scene(true, old_x, old_y);
        quad(old_.high, -old_x, -old_y, float(old_.width), float(old_.height));
        if (direction == 0) clip(256 - amount, 0, amount, 224);
        else if (direction == 1) clip(0, 0, amount, 224);
        else if (direction == 2) clip(0, 224 - amount, 256, amount);
        else clip(0, 0, 256, amount);
    }
    scene(false, camera_x, camera_y);
    auto object = [&](uint64_t key, float x, float y) {
        const auto found = art_.find(key); if (found == art_.end()) return;
        const auto& a = found->second; quad(a.texture, x - camera_x - a.width / 2, y - camera_y - a.height / 2, float(a.width), float(a.height));
    };
    for (const auto& enemy : s.enemies) if (!enemy.flash) object((uint64_t(enemy.kind) << 32) | enemy.map, enemy.x, enemy.y);
    if (s.samus_visible) quad(samus_texture_, s.x - camera_x - 32, s.y - camera_y - 32, 64, 64);
    for (const auto& shot : s.enemy_shots) object(shot.map, shot.x, shot.y);
    if (beam_ready) for (const auto& beam : s.beams) {
        const int size = beam.weapon ? 64 : 16;
        quad(beam_textures_.at(beam.weapon * 10 + beam.direction), beam.x - camera_x - size / 2, beam.y - camera_y - size / 2, float(size), float(size));
    }
    quad(current_.high, -camera_x, -camera_y, float(current_.width), float(current_.height));
    glDisable(GL_SCISSOR_TEST);
}
}
