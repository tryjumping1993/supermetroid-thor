#include "thor/renderer.hpp"
#include <stdexcept>
#include <string>

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
    generation = 0; pose = frame = -1;
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
    glGenTextures(1, &room_texture_); glGenTextures(1, &samus_texture_);
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
void Renderer::room(const Image& image, uint32_t next) { upload(room_texture_, image); room_width_ = image.width; room_height_ = image.height; generation = next; }
void Renderer::samus(const Image& image, int next_pose, int next_frame) { upload(samus_texture_, image); pose = next_pose; frame = next_frame; }
void Renderer::quad(GLuint texture, float x, float y, float width, float height) {
    const float vertices[]{x,y,0,0, x+width,y,1,0, x,y+height,0,1, x,y+height,0,1, x+width,y,1,0, x+width,y+height,1,1};
    glBindTexture(GL_TEXTURE_2D, texture); glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW); glDrawArrays(GL_TRIANGLES, 0, 6);
}
void Renderer::draw(const RenderSnapshot& s, bool widescreen) {
    glViewport(0, 0, width_, height_); glClearColor(0.025f, 0.035f, 0.055f, 1); glClear(GL_COLOR_BUFFER_BIT);
    if (!generation || height_ <= 0) return;
    logical_width_ = widescreen ? 224.f * width_ / height_ : 256.f;
    if (!widescreen) {
        int width = int(height_ * (256.f / 224.f));
        glViewport((width_ - width) / 2, 0, width, height_);
    }
    const float camera_x = s.camera_x - (logical_width_ - 256) / 2;
    const float camera_y = s.camera_y;
    glUseProgram(program_); glBindVertexArray(vertex_array_);
    glUniform2f(view_uniform_, logical_width_, logical_height_); glActiveTexture(GL_TEXTURE0);
    quad(room_texture_, -camera_x, -camera_y, float(room_width_), float(room_height_));
    quad(samus_texture_, s.x - camera_x - 32, s.y - camera_y - 32, 64, 64);
}
}
