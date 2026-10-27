#pragma once
#include "infinite/Presentation.hpp"
#include <array>
#include <bgfx/bgfx.h>

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
                    float scaleHeightRatio, float strength, float aspect, float phase);
    void bind(const ScenePresentation&, const glm::vec3& eye, const glm::vec3& light,
              const glm::mat4& pattern, float primitive, double visualTime, glm::vec4 cavity = {},
              const glm::mat4& micro = glm::mat4(0), float relief = 0);
    void bindEmissions(const Node&, const glm::mat4& pattern, double visualTime);
    bgfx::ProgramHandle geometryProgram();
    bgfx::ProgramHandle glowProgram() const {
        return glow_;
    }
    size_t targetBytes() const {
        return bytes_;
    }

  private:
    void init();
    void clearTargets();
    void screen(bgfx::ViewId, bgfx::ProgramHandle);
    RenderMode mode_ = RenderMode::Original;
    int width_ = 0, height_ = 0;
    size_t bytes_ = 0;
    bgfx::FrameBufferHandle scene_ = BGFX_INVALID_HANDLE;
    std::array<bgfx::FrameBufferHandle, 2> bloom_{{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
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
    bgfx::UniformHandle micro_ = BGFX_INVALID_HANDLE, microParams_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle domain_ = BGFX_INVALID_HANDLE, skyParams_ = BGFX_INVALID_HANDLE,
                        skyRotation_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle sky_ = BGFX_INVALID_HANDLE;
};
} // namespace infinite
