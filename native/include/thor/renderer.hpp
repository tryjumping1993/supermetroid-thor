#pragma once
#include "thor/content.hpp"
#include "thor/session.hpp"
#include <GLES3/gl3.h>

namespace thor {
class Renderer {
public:
    void initialize();
    void resize(int width, int height);
    void room(const Image& image, uint32_t generation);
    void samus(const Image& image, int pose, int frame);
    void draw(const RenderSnapshot& snapshot, bool widescreen);
    uint32_t generation = 0;
    int pose = -1, frame = -1;
private:
    void quad(GLuint texture, float x, float y, float width, float height);
    void upload(GLuint texture, const Image& image);
    GLuint program_ = 0, vertex_array_ = 0, buffer_ = 0, room_texture_ = 0, samus_texture_ = 0;
    GLint view_uniform_ = -1;
    int width_ = 1, height_ = 1, room_width_ = 0, room_height_ = 0;
    float logical_width_ = 256, logical_height_ = 224;
};
}
