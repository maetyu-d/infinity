#include "HyperionRenderer.hpp"
#include <HyperionShaders.hpp>
#include <algorithm>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>

namespace infinite {
namespace {
bgfx::ShaderHandle shader(const char* name) {
#define LOAD_SHADER(NAME, BACKEND)                                                                           \
    if (std::strcmp(name, #NAME) == 0)                                                                       \
    return bgfx::createShader(bgfx::copy(NAME##_##BACKEND, sizeof(NAME##_##BACKEND)))
#define LOAD_BACKEND(BACKEND)                                                                                \
    LOAD_SHADER(vs_hyperion, BACKEND);                                                                       \
    LOAD_SHADER(fs_hyperion, BACKEND);                                                                       \
    LOAD_SHADER(fs_hyperion_glow, BACKEND);                                                                  \
    LOAD_SHADER(vs_hyperion_screen, BACKEND);                                                                \
    LOAD_SHADER(fs_hyperion_blur, BACKEND);                                                                  \
    LOAD_SHADER(fs_hyperion_post, BACKEND)
    switch (bgfx::getRendererType()) {
    case bgfx::RendererType::Metal: {
        LOAD_BACKEND(metal);
        break;
    }
    case bgfx::RendererType::Vulkan: {
        LOAD_BACKEND(spirv);
        break;
    }
    case bgfx::RendererType::OpenGL: {
        LOAD_BACKEND(glsl);
        break;
    }
#if defined(_WIN32)
    case bgfx::RendererType::Direct3D11: {
        LOAD_BACKEND(dxbc);
        break;
    }
#endif
    default:
        break;
    }
#undef LOAD_BACKEND
#undef LOAD_SHADER
    throw std::runtime_error("No Hyperion shader for this backend");
}
bgfx::ProgramHandle program(const char* vs, const char* fs) {
    auto result = bgfx::createProgram(shader(vs), shader(fs), true);
    if (!bgfx::isValid(result))
        throw std::runtime_error("Hyperion shader program creation failed");
    return result;
}
constexpr uint64_t sampling = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
} // namespace
void HyperionRenderer::init() {
    if (bgfx::isValid(geometry_))
        return;
    geometry_ = program("vs_hyperion", "fs_hyperion");
    glow_ = program("vs_hyperion", "fs_hyperion_glow");
    blur_ = program("vs_hyperion_screen", "fs_hyperion_blur");
    post_ = program("vs_hyperion_screen", "fs_hyperion_post");
    sceneSampler_ = bgfx::createUniform("s_scene", bgfx::UniformType::Sampler);
    bloomSampler_ = bgfx::createUniform("s_bloom", bgfx::UniformType::Sampler);
    style_ = bgfx::createUniform("u_hyperion", bgfx::UniformType::Vec4, 4);
    pattern_ = bgfx::createUniform("u_pattern", bgfx::UniformType::Mat4);
    cavity_ = bgfx::createUniform("u_cavity", bgfx::UniformType::Vec4);
    postParams_ = bgfx::createUniform("u_post", bgfx::UniformType::Vec4, 2);
    screenLayout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
}
void HyperionRenderer::clearTargets() {
    if (bgfx::isValid(scene_))
        bgfx::destroy(scene_);
    scene_ = BGFX_INVALID_HANDLE;
    for (auto& target : bloom_) {
        if (bgfx::isValid(target))
            bgfx::destroy(target);
        target = BGFX_INVALID_HANDLE;
    }
    bytes_ = 0;
    width_ = height_ = 0;
}
HyperionRenderer::~HyperionRenderer() {
    clearTargets();
    for (auto p : {geometry_, glow_, blur_, post_})
        if (bgfx::isValid(p))
            bgfx::destroy(p);
    for (auto u : {sceneSampler_, bloomSampler_, style_, pattern_, postParams_, cavity_})
        if (bgfx::isValid(u))
            bgfx::destroy(u);
}
void HyperionRenderer::begin(RenderMode mode, int width, int height) {
    const auto previousMode = mode_;
    mode_ = mode;
    if (mode == RenderMode::Original) {
        // Restore every world view, including the per-atom instance views.
        if (previousMode != RenderMode::Original)
            for (bgfx::ViewId id = 0; id < 64; ++id)
                bgfx::setViewFrameBuffer(id, BGFX_INVALID_HANDLE);
        if (previousMode != RenderMode::Original)
            bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x03060cff, 1.f, 0);
        return;
    }
    init();
    const bool replaceTarget = width_ != width || height_ != height;
    if (width_ != width || height_ != height) {
        clearTargets();
        width_ = width;
        height_ = height;
        auto color = bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::RGBA16F,
                                           BGFX_TEXTURE_RT | sampling);
        auto depth = bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::D32F,
                                           BGFX_TEXTURE_RT_WRITE_ONLY);
        bgfx::TextureHandle attachments[] = {color, depth};
        scene_ = bgfx::createFrameBuffer(2, attachments, true);
        for (auto& target : bloom_)
            target = bgfx::createFrameBuffer(std::max(1, width / 3), std::max(1, height / 3),
                                             bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | sampling);
        if (!bgfx::isValid(scene_) || !bgfx::isValid(bloom_[0]) || !bgfx::isValid(bloom_[1]))
            throw std::runtime_error("Hyperion HDR target allocation failed");
        bytes_ = size_t(width) * height * 12 + size_t(std::max(1, width / 3)) * std::max(1, height / 3) * 16;
    }
    if (replaceTarget || previousMode == RenderMode::Original)
        for (bgfx::ViewId id = 0; id < 64; ++id)
            bgfx::setViewFrameBuffer(id, scene_);
    // Hyperion skyFragment's linear ink (.004,.007,.016), rounded to the
    // clear API's RGBA8 input; the HDR display transform is applied later.
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x010204ff, 1.f, 0);
}
void HyperionRenderer::bind(const ScenePresentation& p, const glm::vec3& eye, const glm::vec3& light,
                            const glm::mat4& pattern, float primitive, double seconds, glm::vec4 cavity) {
    init();
    std::array<glm::vec4, 4> data = {glm::vec4(p.strength, float(seconds), p.phase, primitive), p.material,
                                     glm::vec4(eye, 1), glm::vec4(light, 0)};
    bgfx::setUniform(style_, data.data(), 4);
    bgfx::setUniform(pattern_, glm::value_ptr(pattern));
    bgfx::setUniform(cavity_, glm::value_ptr(cavity));
}
void HyperionRenderer::bindEmissions(const Node& node, const glm::mat4& pattern, double seconds) {
    float phase = float(node.seed % 10000) * .0371f;
    phase -= std::floor(phase);
    std::array<glm::vec4, 4> data{};
    data[0] = {mode_ == RenderMode::Hybrid ? .24f : 1.f, float(seconds), phase, 3};
    bgfx::setUniform(style_, data.data(), 4);
    bgfx::setUniform(pattern_, glm::value_ptr(pattern));
}
void HyperionRenderer::screen(bgfx::ViewId id, bgfx::ProgramHandle p) {
    struct Vertex {
        float x, y, z, u, v;
    };
    float bottom = bgfx::getCaps()->originBottomLeft ? 0.f : 1.f;
    float top = 1.f - bottom;
    const Vertex vertices[] = {
        {-1, -1, 0, 0, bottom}, {3, -1, 0, 2, bottom}, {-1, 3, 0, 0, top * 2 - bottom}};
    bgfx::TransientVertexBuffer vb;
    bgfx::allocTransientVertexBuffer(&vb, 3, screenLayout_);
    std::memcpy(vb.data, vertices, sizeof(vertices));
    bgfx::setVertexBuffer(0, &vb);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(id, p);
}
void HyperionRenderer::finish() {
    if (mode_ == RenderMode::Original)
        return;
    for (unsigned i = 0; i < 2; ++i) {
        auto id = bgfx::ViewId(80 + i);
        bgfx::setViewFrameBuffer(id, bloom_[i]);
        bgfx::setViewRect(id, 0, 0, std::max(1, width_ / 3), std::max(1, height_ / 3));
        bgfx::setViewClear(id, BGFX_CLEAR_COLOR, 0x000000ff);
        std::array<glm::vec4, 2> params{};
        params[0] =
            i == 0 ? glm::vec4(2.f / width_, 0, 1, 0) : glm::vec4(0, 2.f / std::max(1, height_ / 3), 0, 0);
        bgfx::setUniform(postParams_, params.data(), 2);
        bgfx::setTexture(0, sceneSampler_, bgfx::getTexture(i == 0 ? scene_ : bloom_[0]), sampling);
        screen(id, blur_);
    }
    bgfx::setViewFrameBuffer(83, BGFX_INVALID_HANDLE);
    bgfx::setViewRect(83, 0, 0, width_, height_);
    std::array<glm::vec4, 2> params{};
    params[0] = {1.f / width_, 1.f / height_, mode_ == RenderMode::Hybrid ? .36f : 1.f, 0};
    bgfx::setUniform(postParams_, params.data(), 2);
    bgfx::setTexture(0, sceneSampler_, bgfx::getTexture(scene_), sampling);
    bgfx::setTexture(1, bloomSampler_, bgfx::getTexture(bloom_[1]), sampling);
    screen(83, post_);
}
} // namespace infinite
