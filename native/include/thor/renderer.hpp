#pragma once
#include "thor/content.hpp"
#include "thor/session.hpp"
#include <GLES3/gl3.h>
#include <unordered_map>

namespace thor {
class Renderer {
public:
    void initialize();
    void resize(int width, int height);
    void room(const Image& low, const Image& high, const Image& background, uint8_t layer_x, uint8_t layer_y, uint32_t generation, uint32_t revision);
    void samus(const Image& image, int pose, int frame);
    void power_beam(const std::array<Image, 30>& images);
    bool has_art(uint64_t key) const { return art_.contains(key); }
    void art(uint64_t key, const Image& image);
    void draw(const RenderSnapshot& snapshot, bool widescreen);
    uint32_t generation = 0;
    uint32_t room_revision = 0;
    bool beam_ready = false;
    int pose = -1, frame = -1;
private:
    void quad(GLuint texture, float x, float y, float width, float height);
    void upload(GLuint texture, const Image& image);
    void scene(bool old, float camera_x, float camera_y);
    struct Scene { GLuint low = 0, high = 0, background = 0; int width = 0, height = 0, bg_width = 0, bg_height = 0; uint8_t layer_x = 0, layer_y = 0; } current_, old_;
    struct Art { GLuint texture; int width, height; };
    std::unordered_map<uint64_t, Art> art_;
    GLuint program_ = 0, vertex_array_ = 0, buffer_ = 0, samus_texture_ = 0;
    std::array<GLuint, 30> beam_textures_{};
    GLint view_uniform_ = -1;
    int width_ = 1, height_ = 1;
    float logical_width_ = 256, logical_height_ = 224;
};
}
