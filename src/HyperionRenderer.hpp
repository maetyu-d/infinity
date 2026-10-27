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
    void begin(RenderMode, int width, int height);
    void finish();
    void bind(const ScenePresentation&, const glm::vec3& eye, const glm::vec3& light,
              const glm::mat4& pattern, float primitive, double visualTime, glm::vec4 cavity = {});
    void bindEmissions(const Node&, const glm::mat4& pattern, double visualTime);
    bgfx::ProgramHandle geometryProgram() {
        init();
        return geometry_;
    }
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
};
} // namespace infinite
