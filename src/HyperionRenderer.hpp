#pragma once
#include "infinite/Presentation.hpp"
#include "infinite/SurfaceShading.hpp"
#include <array>
#include <bgfx/bgfx.h>
#include <memory>
#include <oneapi/tbb/task_group.h>

namespace infinite {
// Geometry shading, emission bloom and final display transform only. Owns no
// universe, navigation, generator, seed state or geometry cache.
class HyperionRenderer {
  public:
    ~HyperionRenderer();
    void begin(RenderMode, int width, int height, bool opticalDepth = false, bool flight = false);
    void finish();
    void sky(const SpectralSky&, double visualTime, float aspect, float tanHalfFov);
    void atmosphere(glm::vec3 eye, glm::vec3 light, const glm::mat4& cameraToPlanet, float radiusRenderUnits,
                    float scaleHeightRatio, float strength, float aspect, float phase, float radiusKm);
    void bind(const ScenePresentation&, const glm::vec3& eye, const glm::vec3& light,
              const glm::mat4& pattern, float primitive, double visualTime, glm::vec4 cavity = {},
              const glm::mat4& micro = glm::mat4(0), float relief = 0, const glm::mat4& meso = glm::mat4(0),
              float mesoRelief = 0);
    void bindEmissions(const Node&, const glm::mat4& pattern, double visualTime);
    bool prepareSun(const SunShadowFrame&, uint64_t stableKey);
    bgfx::ProgramHandle sunProgram();
    bgfx::ProgramHandle cloudProgram();
    bgfx::ProgramHandle coverageProgram();
    bool sunActive() const {
        return sunActive_;
    }
    size_t sunBytes() const {
        return sunBytes_;
    }
    bool sunUpdated() const {
        return sunUpdated_;
    }
    void completeSun(bool complete) {
        sunValid_ = complete;
        if (!complete)
            sunActive_ = false;
    }
    bgfx::ProgramHandle geometryProgram(bool smooth = false);
    bgfx::ProgramHandle glowProgram() const {
        return glow_;
    }
    size_t materialUploadBytes() const {
        return detailUploadBytes_;
    }
    size_t materialBytes() const {
        return detailBytes_ + noiseBytes_;
    }
    size_t targetBytes() const {
        return bytes_ + sunBytes_;
    }

  private:
    void init();
    void clearTargets();
    void publishDetail();
    struct DetailBake;
    std::shared_ptr<DetailBake> detailBake_;
    oneapi::tbb::task_group detailTasks_;
    bgfx::TextureHandle noise_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle noiseSampler_ = BGFX_INVALID_HANDLE;
    size_t noiseBytes_ = 0;
    bgfx::TextureHandle detail_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle environment_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle detailSampler_ = BGFX_INVALID_HANDLE, surface_ = BGFX_INVALID_HANDLE;
    size_t detailBytes_ = 0, detailUploadBytes_ = 0;
    void screen(bgfx::ViewId, bgfx::ProgramHandle);
    RenderMode mode_ = RenderMode::Original;
    int width_ = 0, height_ = 0;
    size_t bytes_ = 0;
    bgfx::FrameBufferHandle scene_ = BGFX_INVALID_HANDLE;
    std::array<bgfx::FrameBufferHandle, 2> bloom_{{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    std::array<bgfx::ProgramHandle, 2> smoothGeometry_{{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    bgfx::ProgramHandle geometry_ = BGFX_INVALID_HANDLE, glow_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle blur_ = BGFX_INVALID_HANDLE, post_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sceneSampler_ = BGFX_INVALID_HANDLE, bloomSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle style_ = BGFX_INVALID_HANDLE, pattern_ = BGFX_INVALID_HANDLE,
                        postParams_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle cavity_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout screenLayout_;
    bgfx::UniformHandle depthSampler_ = BGFX_INVALID_HANDLE, atmosphere_ = BGFX_INVALID_HANDLE,
                        atmosphereRotation_ = BGFX_INVALID_HANDLE;
    std::array<glm::vec4, 4> atmosphereData_{};
    glm::mat4 atmosphereMatrix_{1};
    bool readableDepth_ = false;
    bool flight_ = false;
    bgfx::ProgramHandle flightGeometry_ = BGFX_INVALID_HANDLE, flightPost_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle meso_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle micro_ = BGFX_INVALID_HANDLE, microParams_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle domain_ = BGFX_INVALID_HANDLE, skyParams_ = BGFX_INVALID_HANDLE,
                        skyRotation_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle sky_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle sunTarget_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle sunProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle cloudProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle coverageProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sunSampler_ = BGFX_INVALID_HANDLE, sunMatrix_ = BGFX_INVALID_HANDLE,
                        sunParams_ = BGFX_INVALID_HANDLE;
    glm::mat4 sunFromWorld_{1};
    glm::dvec3 sunAnchor_{}, sunLight_{};
    double sunSpan_ = 0;
    uint64_t sunKey_ = 0;
    size_t sunBytes_ = 0;
    bool sunActive_ = false, sunValid_ = false, sunUpdated_ = false;
};
} // namespace infinite
