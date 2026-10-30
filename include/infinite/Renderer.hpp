#pragma once
#include "Activity.hpp"
#include "Navigation.hpp"
#include "Piloting.hpp"
#include "Presentation.hpp"
#include "SurfaceLife.hpp"
#include "World.hpp"
#include <memory>
struct SDL_Window;
namespace infinite {
struct CockpitViewport;
struct FrameStats {
    double cpuMs = 0, gpuMs = 0, submitMs = 0;
    uint32_t drawCalls = 0;
    size_t gpuBytes = 0, visibleObjects = 0, atoms = 0;
    size_t lodTransitions = 0, sections = 0;
    size_t vertices = 0, triangles = 0, particles = 0, uploadBytes = 0;
    size_t uploadDeferred = 0, atomTriangles = 0;
    size_t effectBytes = 0, materialBytes = 0;
    size_t shadowBytes = 0, shadowUpdates = 0, shadowActive = 0;
    size_t terrainTiles = 0, rootCells = 0;
    size_t atmospheres = 0, planetRings = 0;
    size_t skyStars = 0;
    size_t celestialContacts = 0, celestialLights = 0, skyGas = 0, skyGalaxies = 0;
    size_t lifeWorlds = 0, lifeScanning = 0;
    size_t moonWorlds = 0;
    size_t activityBodies = 0, activityVisible = 0, activityTriangles = 0, activityBytes = 0;
    size_t surfaceSites = 0, surfaceBuildings = 0, surfaceResidents = 0, surfaceTriangles = 0;
    size_t surfaceBytes = 0, surfaceGpuBytes = 0, surfacePending = 0, weatherCells = 0, weatherParticles = 0;
    size_t weatherPending = 0, weatherCacheBytes = 0, weatherGpuBytes = 0, weatherVolumes = 0;
};
class Renderer {
  public:
    Renderer(SDL_Window*, const Universe&, bool benchmark = false);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void resize(int width, int height);
    FrameStats render(const Navigation&, RegionCache&, bool overlay, double fps, size_t markers,
                      std::string_view notice, bool looking = false);
    std::optional<Address> pick(double x, double y) const;
    void screenshot(const std::string& path);
    std::string backend() const;
    void setSunShadows(bool enabled);
    void setPolarLights(bool enabled);   // Presentation switch for native negative-control captures.
    void setStellarOptics(bool enabled); // Native optics negative control.
    void setLandingShade(bool enabled);  // Native contact-shading negative control.
    void setPlanetRings(bool enabled);
    void setWeatherEffects(bool enabled); // Native precipitation, visibility and discharge control.
    void setCloudVolumes(bool enabled);   // Native optical-volume negative control; weather is unchanged.
    void setTerrainCoverageDebug(bool enabled); // Native raster-coverage validation only.
    std::shared_ptr<const Geometry> submittedGeometry(const Address&) const;
    std::shared_ptr<const Geometry> galaxySamples() const;
    std::span<const CelestialContact> celestialContacts() const;
    std::span<const ActivityContact> activityContacts() const;
    std::optional<ActivityId> pickActivity(double x, double y) const;
    std::shared_ptr<const SurfaceWorld> surfaceWorld(const Address&) const;
    uint64_t terrainGeometryDigest() const;
    CockpitViewport cockpitViewport() const;
    void setActivity(const ActivityState*);
    void setMode(RenderMode);
    RenderMode mode() const;
    void freezeVisualTime(double seconds);
    void setCockpit(const FlightInstruments*);
    void toggleCockpitHelp();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace infinite
