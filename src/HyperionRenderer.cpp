#include "HyperionRenderer.hpp"
#include "infinite/MaterialTexture.hpp"
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
    LOAD_SHADER(vs_hyperion_smooth, BACKEND);                                                                \
    LOAD_SHADER(vs_hyperion_shadow, BACKEND);                                                                \
    LOAD_SHADER(fs_hyperion_shadow, BACKEND);                                                                \
    LOAD_SHADER(fs_hyperion_coverage, BACKEND);                                                              \
    LOAD_SHADER(fs_hyperion_cloud, BACKEND);                                                                 \
    LOAD_SHADER(fs_hyperion, BACKEND);                                                                       \
    LOAD_SHADER(fs_hyperion_flight, BACKEND);                                                                \
    LOAD_SHADER(fs_hyperion_flight_post, BACKEND);                                                           \
    LOAD_SHADER(fs_hyperion_sky, BACKEND);                                                                   \
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
struct HyperionRenderer::DetailBake {
    std::atomic<bool> cancel{false}, ready{false};
    MaterialTexture texture;
};
void HyperionRenderer::publishDetail() {
    if (!detailBake_ || !detailBake_->ready.load(std::memory_order_acquire))
        return;
    auto& pixels = detailBake_->texture.pixels;
    if (!pixels.empty()) {
        auto texture = bgfx::createTexture2D(MaterialTexture::size, MaterialTexture::size, true, 1,
                                             bgfx::TextureFormat::RGBA8, 0,
                                             bgfx::copy(pixels.data(), uint32_t(pixels.size())));
        if (bgfx::isValid(texture)) {
            bgfx::destroy(detail_);
            detail_ = texture;
            detailBytes_ = detailUploadBytes_ = pixels.size();
        }
    }
    detailBake_.reset();
}
void HyperionRenderer::init() {
    if (bgfx::isValid(geometry_))
        return;
    detailSampler_ = bgfx::createUniform("s_materialDetail", bgfx::UniformType::Sampler);
    noiseSampler_ = bgfx::createUniform("s_materialNoise", bgfx::UniformType::Sampler);
    const auto noise = materialNoiseTexture();
    noise_ = bgfx::createTexture2D(256, 256, false, 1, bgfx::TextureFormat::RGBA8, 0,
                                   bgfx::copy(noise.data(), uint32_t(noise.size())));
    noiseBytes_ = noise.size();
    detailUploadBytes_ += noiseBytes_;
    environment_ = bgfx::createUniform("u_environment", bgfx::UniformType::Vec4);
    surface_ = bgfx::createUniform("u_surface", bgfx::UniformType::Vec4);
    const uint8_t average[] = {128, 0, 128, 128};
    detail_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0,
                                    bgfx::copy(average, sizeof(average)));
    auto work = detailBake_ = std::make_shared<DetailBake>();
    detailTasks_.run([work] {
        try {
            work->texture = bakeMaterialTexture(&work->cancel);
        } catch (...) {
            work->cancel.store(true);
        }
        work->ready.store(true, std::memory_order_release);
    });
    geometry_ = program("vs_hyperion", "fs_hyperion");
    glow_ = program("vs_hyperion", "fs_hyperion_glow");
    blur_ = program("vs_hyperion_screen", "fs_hyperion_blur");
    post_ = program("vs_hyperion_screen", "fs_hyperion_post");
    sceneSampler_ = bgfx::createUniform("s_scene", bgfx::UniformType::Sampler);
    bloomSampler_ = bgfx::createUniform("s_bloom", bgfx::UniformType::Sampler);
    depthSampler_ = bgfx::createUniform("s_opaqueDepth", bgfx::UniformType::Sampler);
    atmosphere_ = bgfx::createUniform("u_atmosphere", bgfx::UniformType::Vec4, 4);
    atmosphereRotation_ = bgfx::createUniform("u_atmosphereRotation", bgfx::UniformType::Mat4);
    style_ = bgfx::createUniform("u_hyperion", bgfx::UniformType::Vec4, 4);
    pattern_ = bgfx::createUniform("u_pattern", bgfx::UniformType::Mat4);
    cavity_ = bgfx::createUniform("u_cavity", bgfx::UniformType::Vec4);
    meso_ = bgfx::createUniform("u_meso", bgfx::UniformType::Mat4);
    micro_ = bgfx::createUniform("u_micro", bgfx::UniformType::Mat4);
    microParams_ = bgfx::createUniform("u_microParams", bgfx::UniformType::Vec4);
    domain_ = bgfx::createUniform("u_materialDomain", bgfx::UniformType::Vec4);
    skyParams_ = bgfx::createUniform("u_sky", bgfx::UniformType::Vec4, 2);
    skyRotation_ = bgfx::createUniform("u_skyRotation", bgfx::UniformType::Mat4);
    postParams_ = bgfx::createUniform("u_post", bgfx::UniformType::Vec4, 2);
    sunSampler_ = bgfx::createUniform("s_sunShadow", bgfx::UniformType::Sampler);
    sunMatrix_ = bgfx::createUniform("u_sunFromWorld", bgfx::UniformType::Mat4);
    sunParams_ = bgfx::createUniform("u_sunShadowParams", bgfx::UniformType::Vec4);
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
    if (detailBake_)
        detailBake_->cancel.store(true);
    detailTasks_.wait();
    if (bgfx::isValid(detail_))
        bgfx::destroy(detail_);
    clearTargets();
    if (bgfx::isValid(noise_))
        bgfx::destroy(noise_);
    if (bgfx::isValid(sunTarget_))
        bgfx::destroy(sunTarget_);
    for (auto p : {geometry_, glow_, blur_, post_, flightGeometry_, flightPost_, sky_, smoothGeometry_[0],
                   smoothGeometry_[1], sunProgram_, cloudProgram_, coverageProgram_})
        if (bgfx::isValid(p))
            bgfx::destroy(p);
    for (auto u : {environment_,
                   noiseSampler_,
                   detailSampler_,
                   surface_,
                   sceneSampler_,
                   bloomSampler_,
                   style_,
                   pattern_,
                   postParams_,
                   cavity_,
                   depthSampler_,
                   atmosphere_,
                   atmosphereRotation_,
                   micro_,
                   microParams_,
                   meso_,
                   domain_,
                   skyParams_,
                   skyRotation_,
                   sunSampler_,
                   sunMatrix_,
                   sunParams_})
        if (bgfx::isValid(u))
            bgfx::destroy(u);
}
bgfx::ProgramHandle HyperionRenderer::geometryProgram(bool smooth) {
    init();
    if (smooth) {
        auto& handle = smoothGeometry_[flight_ ? 1 : 0];
        if (!bgfx::isValid(handle))
            handle = program("vs_hyperion_smooth", flight_ ? "fs_hyperion_flight" : "fs_hyperion");
        return handle;
    }
    if (!flight_)
        return geometry_;
    if (!bgfx::isValid(flightGeometry_))
        flightGeometry_ = program("vs_hyperion", "fs_hyperion_flight");
    return flightGeometry_;
}
void HyperionRenderer::begin(RenderMode mode, int width, int height, bool opticalDepth, bool flight) {
    detailUploadBytes_ = 0;
    flight_ = flight;
    atmosphereData_ = {};
    sunActive_ = false;
    sunUpdated_ = false;
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
    publishDetail();
    const bool replaceTarget = width_ != width || height_ != height || readableDepth_ != opticalDepth;
    if (replaceTarget) {
        clearTargets();
        readableDepth_ = opticalDepth;
        width_ = width;
        height_ = height;
        auto color = bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::RGBA16F,
                                           BGFX_TEXTURE_RT | sampling);
        auto depth =
            bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::D32F,
                                  opticalDepth ? BGFX_TEXTURE_RT | sampling : BGFX_TEXTURE_RT_WRITE_ONLY);
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
bgfx::ProgramHandle HyperionRenderer::sunProgram() {
    if (!bgfx::isValid(sunProgram_))
        sunProgram_ = program("vs_hyperion_shadow", "fs_hyperion_shadow");
    return sunProgram_;
}
bgfx::ProgramHandle HyperionRenderer::cloudProgram() {
    if (!bgfx::isValid(cloudProgram_))
        cloudProgram_ = program("vs_hyperion", "fs_hyperion_cloud");
    return cloudProgram_;
}
bgfx::ProgramHandle HyperionRenderer::coverageProgram() {
    if (!bgfx::isValid(coverageProgram_))
        coverageProgram_ = program("vs_hyperion_shadow", "fs_hyperion_coverage");
    return coverageProgram_;
}
bool HyperionRenderer::prepareSun(const SunShadowFrame& frame, uint64_t key) {
    if (mode_ == RenderMode::Original || !readableDepth_)
        return false;
    if (!bgfx::isValid(sunTarget_)) {
        if (!bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D16, BGFX_TEXTURE_RT))
            return false;
        auto texture = bgfx::createTexture2D(sunShadowResolution, sunShadowResolution, false, 1,
                                             bgfx::TextureFormat::D16, BGFX_TEXTURE_RT | sampling);
        sunTarget_ = bgfx::createFrameBuffer(1, &texture, true);
        if (!bgfx::isValid(sunTarget_))
            throw std::runtime_error("Sun shadow target allocation failed");
        sunBytes_ = size_t(sunShadowResolution) * sunShadowResolution * 2;
    }
    sunActive_ = true;
    sunFromWorld_ = glm::mat4(frame.textureFromWorld);
    const bool redraw = !sunValid_ || sunKey_ != key || sunSpan_ != frame.spanMetres ||
                        glm::length(frame.anchorPlanet - sunAnchor_) > .001 ||
                        glm::length(frame.lightPlanet - sunLight_) > 1e-7;
    if (!redraw)
        return false;
    sunUpdated_ = true;
    sunKey_ = key;
    sunAnchor_ = frame.anchorPlanet;
    sunLight_ = frame.lightPlanet;
    sunSpan_ = frame.spanMetres;
    sunValid_ = true;
    const glm::mat4 view(frame.view), projection(frame.projection);
    bgfx::setViewFrameBuffer(70, sunTarget_);
    bgfx::setViewRect(70, 0, 0, sunShadowResolution, sunShadowResolution);
    bgfx::setViewTransform(70, glm::value_ptr(view), glm::value_ptr(projection));
    bgfx::setViewClear(70, BGFX_CLEAR_DEPTH, 0, 1.f, 0);
    bgfx::touch(70);
    return true;
}
void HyperionRenderer::atmosphere(glm::vec3 eye, glm::vec3 light, const glm::mat4& rotation, float radius,
                                  float height, float strength, float aspect, float phase, float radiusKm) {
    atmosphereData_[0] = glm::vec4(eye, strength);
    atmosphereData_[1] = glm::vec4(light, height);
    atmosphereData_[2] = {radius, .00005f, 10000.f, phase};
    atmosphereData_[3] = {aspect * .7002075f, .7002075f, bgfx::getCaps()->originBottomLeft ? 1.f : -1.f,
                          radiusKm};
    atmosphereMatrix_ = rotation;
}
void HyperionRenderer::bind(const ScenePresentation& p, const glm::vec3& eye, const glm::vec3& light,
                            const glm::mat4& pattern, float primitive, double seconds, glm::vec4 cavity,
                            const glm::mat4& micro, float relief, const glm::mat4& meso, float mesoRelief) {
    init();
    std::array<glm::vec4, 4> data = {glm::vec4(p.strength, float(seconds), p.phase, primitive), p.material,
                                     glm::vec4(eye, 1), glm::vec4(light, 0)};
    bgfx::setUniform(style_, data.data(), 4);
    bgfx::setUniform(pattern_, glm::value_ptr(pattern));
    bgfx::setUniform(domain_, glm::value_ptr(p.domain));
    bgfx::setUniform(surface_, glm::value_ptr(p.surface));
    bgfx::setUniform(environment_, glm::value_ptr(p.environment));
    bgfx::setUniform(cavity_, glm::value_ptr(cavity));
    bgfx::setUniform(sunMatrix_, glm::value_ptr(sunFromWorld_));
    const glm::vec4 shadow{sunActive_ ? 1.f : 0.f, 1.f / sunShadowResolution, 0, 0};
    bgfx::setUniform(sunParams_, glm::value_ptr(shadow));
    bgfx::setTexture(4, sunSampler_, sunActive_ ? bgfx::getTexture(sunTarget_) : detail_,
                     sampling | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    {
        bgfx::setUniform(micro_, glm::value_ptr(micro));
        bgfx::setUniform(meso_, glm::value_ptr(meso));
        bgfx::setTexture(5, noiseSampler_, noise_);
        bgfx::setTexture(3, detailSampler_, detail_,
                         BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC);
        const glm::vec4 microParams{relief, detailBytes_ ? 1.f : 0.f, mesoRelief, 0};
        bgfx::setUniform(microParams_, glm::value_ptr(microParams));
    }
}
void HyperionRenderer::sky(const SpectralSky& sky, double seconds, float aspect, float tanHalfFov) {
    if (mode_ == RenderMode::Original || sky.strength <= .001f)
        return;
    init();
    if (!bgfx::isValid(sky_))
        sky_ = program("vs_hyperion_screen", "fs_hyperion_sky");
    const std::array<glm::vec4, 2> parameters{
        glm::vec4(sky.strength * (mode_ == RenderMode::Hybrid ? .24f : 1.f), sky.phase, sky.gas,
                  float(seconds)),
        glm::vec4(aspect * tanHalfFov, tanHalfFov, bgfx::getCaps()->originBottomLeft ? 1.f : -1.f, 0)};
    const auto rotation = glm::mat4_cast(glm::quat(sky.cameraToField));
    bgfx::setUniform(skyParams_, parameters.data(), 2);
    bgfx::setUniform(skyRotation_, glm::value_ptr(rotation));
    // First in sequential view 0, before stars and all opaque/depth-tested
    // universe geometry. The optical field never writes depth or adds objects.
    screen(0, sky_);
}
void HyperionRenderer::bindEmissions(const Node& node, const glm::mat4& pattern, double seconds) {
    const float phase = hyperionPhase(node.seed);
    std::array<glm::vec4, 4> data{};
    data[0] = {mode_ == RenderMode::Hybrid ? .24f : 1.f, float(seconds), phase, 3};
    bgfx::setUniform(style_, data.data(), 4);
    bgfx::setUniform(pattern_, glm::value_ptr(pattern));
    const glm::vec4 domain{};
    bgfx::setUniform(domain_, glm::value_ptr(domain));
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
            i == 0 ? glm::vec4(4.f / width_, 0, 1, 0) : glm::vec4(0, 4.f / std::max(1, height_ / 3), 0, 0);
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
    const bool optical = readableDepth_;
    if (optical) {
        if (!bgfx::isValid(flightPost_))
            flightPost_ = program("vs_hyperion_screen", "fs_hyperion_flight_post");
        bgfx::setTexture(2, depthSampler_, bgfx::getTexture(scene_, 1),
                         sampling | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
        bgfx::setUniform(atmosphere_, atmosphereData_.data(), 4);
        bgfx::setUniform(atmosphereRotation_, glm::value_ptr(atmosphereMatrix_));
    }
    screen(83, optical ? flightPost_ : post_);
}
} // namespace infinite
