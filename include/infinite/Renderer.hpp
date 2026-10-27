#pragma once
#include "Navigation.hpp"
#include "Piloting.hpp"
#include "Presentation.hpp"
#include "World.hpp"
#include <memory>
struct SDL_Window;
namespace infinite {
struct FrameStats {
    double cpuMs = 0, gpuMs = 0, submitMs = 0;
    uint32_t drawCalls = 0;
    size_t gpuBytes = 0, visibleObjects = 0, atoms = 0;
    size_t lodTransitions = 0, sections = 0;
    size_t vertices = 0, triangles = 0, particles = 0, uploadBytes = 0;
    size_t uploadDeferred = 0, atomTriangles = 0;
    size_t effectBytes = 0;
    size_t terrainTiles = 0, rootCells = 0;
    size_t atmospheres = 0;
    size_t skyStars = 0;
};
class Renderer {
  public:
    Renderer(SDL_Window*, const Universe&);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void resize(int width, int height);
    FrameStats render(const Navigation&, RegionCache&, bool overlay, double fps, size_t markers,
                      std::string_view notice, bool looking = false);
    std::optional<Address> pick(double x, double y) const;
    void screenshot(const std::string& path);
    std::string backend() const;
    std::shared_ptr<const Geometry> submittedGeometry(const Address&) const;
    void setMode(RenderMode);
    RenderMode mode() const;
    void freezeVisualTime(double seconds);
    void setCockpit(const FlightInstruments*);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace infinite
