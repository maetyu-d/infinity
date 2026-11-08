#include "infinity/Renderer.hpp"
#include "HyperionRenderer.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/ExpeditionPlanner.hpp"
#include "infinity/MiningConsequences.hpp"
#include "infinity/Economy.hpp"
#include "infinity/NpcCombat.hpp"
#include "infinity/OrbitalDebris.hpp"
#include "infinity/PointLight.hpp"
#include "infinity/ProbePresentation.hpp"
#include "infinity/CavernHazard.hpp"
#include "infinity/StationControl.hpp"
#include "infinity/StellarEvolution.hpp"
#include "infinity/Streaming.hpp"
#include "infinity/SurfaceShading.hpp"
#include "infinity/Weapons.hpp"
#include "infinity/WorldDepth.hpp"
#include "infinity/SurfaceOperations.hpp"
#include "infinity/Urban.hpp"
#include "infinity/WarpTravel.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <algorithm>
#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <chrono>
#include <bit>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fs_debugdraw_lines.bin.h>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <map>
#include <numbers>
#include <oneapi/tbb/task_group.h>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <vs_debugdraw_lines.bin.h>

namespace infinity {
static const bgfx::EmbeddedShader embedded[] = {BGFX_EMBEDDED_SHADER(vs_debugdraw_lines),
                                                BGFX_EMBEDDED_SHADER(fs_debugdraw_lines),
                                                BGFX_EMBEDDED_SHADER_END()};
struct Callbacks final : bgfx::CallbackI {
    void fatal(const char*, uint16_t, bgfx::Fatal::Enum, const char* message) override {
        std::fprintf(stderr, "bgfx fatal: %s\n", message);
        std::abort();
    }
    void traceVargs(const char*, uint16_t, const char* format, va_list args) override {
        std::vfprintf(stderr, format, args);
    }
    void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override {
        return 0;
    }
    bool cacheRead(uint64_t, void*, uint32_t) override {
        return false;
    }
    void cacheWrite(uint64_t, const void*, uint32_t) override {}
    void screenShot(const char* filename, uint32_t width, uint32_t height, uint32_t pitch,
                    bgfx::TextureFormat::Enum, const void* data, uint32_t, bool flip) override {
        // Portable, lossless PPM; no extra codec dependency. Source is BGRA8.
        std::ofstream out(filename, std::ios::binary);
        out << "P6\n" << width << ' ' << height << "\n255\n";
        auto bytes = static_cast<const uint8_t*>(data);
        std::vector<char> row(width * 3);
        for (uint32_t y = 0; y < height; ++y) {
            auto p = bytes + (flip ? height - y - 1 : y) * pitch;
            for (uint32_t x = 0; x < width; ++x) {
                row[x * 3] = static_cast<char>(p[x * 4 + 2]);
                row[x * 3 + 1] = static_cast<char>(p[x * 4 + 1]);
                row[x * 3 + 2] = static_cast<char>(p[x * 4]);
            }
            out.write(row.data(), row.size());
        }
    }
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, uint32_t) override {}
};
struct GPUMesh {
    bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
    size_t count = 0;
};
static void destroy(GPUMesh& m) {
    if (bgfx::isValid(m.vertices))
        bgfx::destroy(m.vertices);
    if (bgfx::isValid(m.indices))
        bgfx::destroy(m.indices);
    m = {};
}
struct NormalWork {
    std::atomic<bool> cancel{false}, ready{false};
    std::vector<ShadingNormal> result;
};
struct GPUGeometry {
    GPUMesh solid, lines;
    bgfx::VertexBufferHandle normals = BGFX_INVALID_HANDLE;
    std::shared_ptr<NormalWork> normalWork;
    bgfx::VertexBufferHandle instances = BGFX_INVALID_HANDLE;
    std::weak_ptr<const Geometry> source;
    uint64_t lastUsed = 0;
    size_t bytes = 0;
    bgfx::DynamicVertexBufferHandle probeLight = BGFX_INVALID_HANDLE;
    std::optional<ProbeWallLighting> probeLightKey;
    uint64_t miningWearKey = UINT64_MAX;
};
struct Instance {
    glm::mat4 transform;
    glm::vec4 color;
};
static_assert(sizeof(Instance) == 80);
struct Pick {
    Address address;
    glm::dvec2 screen;
    double depth;
};
struct Renderer::Impl {
    const Universe& universe;
    SDL_Window* window;
    Callbacks callbacks;
    bgfx::VertexLayout layout, instanceLayout, normalLayout;
    oneapi::tbb::task_group normalTasks;
    std::atomic<unsigned> normalJobs{0};
    bgfx::ProgramHandle colorProgram = BGFX_INVALID_HANDLE;
    bgfx::SwapChain swapChain;
    uint32_t resetFlags = BGFX_RESET_VSYNC | BGFX_RESET_MAXANISOTROPY;
    std::array<GPUMesh, 4> atomSpheres;
    GPUMesh hyperionBillboardMesh;
    GPUMesh pointLightBillboardMesh;
    GPUMesh cabin;
    double cabinAspect = 0;
    std::optional<FlightInstruments> instruments;
    const ExpeditionPlan* expeditionPlan = nullptr;
    CockpitMotion cockpitMotion;
    CockpitMotionPose head;
    bool cockpitMotionEnabled = true;
    bool cockpitHelp = false, cockpitVisible = false, validateCockpit = false;
    std::vector<CockpitRect> cockpitHitRegions;
    std::vector<CockpitContactHit> cockpitContactHits;
    CockpitViewport hudViewport;
    bool polarLights = true;
    bool cloudVolumes = true;
    bool stellarOptics = true;
    bool pointLightFiltering = true;
    bool landingShade = true;
    bool weatherEffects = true;
    bool planetRings = true;
    float stellarExposure = 1;
    std::shared_ptr<const Geometry> skyGeometry;
    std::vector<CelestialContact> contacts;
    std::optional<Address> lifeSelection;
    std::vector<CelestialContact> skyOccluders;
    const ActivityState* activity = nullptr;
    const WeaponSystem* weapons = nullptr;
    const NpcCombat* npcCombat = nullptr;
    const CrewView* crew = nullptr;
    std::vector<ActivityBody> combatBodies;
    std::vector<ActivityBody> weaponTargets;
    std::shared_ptr<const EconomySnapshot> economy;
    const MarketView* market = nullptr;
    std::shared_ptr<const EconomicNetwork> lightingNetwork;
    std::vector<float> settlementLights;
    std::array<std::array<GPUMesh,3>,size_t(SurfaceMachine::Count)> workMeshes;
    struct WorkClock { SurfaceWorkClock clock, machinery; uint64_t used = 0; };
    std::map<ActivityId,WorkClock> workClocks;
    void updateEconomicLighting(double dt) {
        if (!economy || !economy->network || economy->catchingUp ||
            economy->portSignals.size() != economy->network->sites.size())
            return;
        if (lightingNetwork != economy->network) {
            lightingNetwork = economy->network;
            settlementLights.clear();
            for (const auto& signal : economy->portSignals)
                settlementLights.push_back(signal.lighting);
            return;
        }
        const float blend = float(1 - std::exp(-std::clamp(dt, 0., .1) * 2.));
        for (size_t i = 0; i < settlementLights.size(); ++i)
            settlementLights[i] = glm::mix(settlementLights[i], economy->portSignals[i].lighting, blend);
    }
    std::vector<ActivityContact> activityContacts;
    std::vector<std::pair<ActivityId, glm::dvec2>> activityPicks;
    static constexpr size_t debrisSurfaceRole = 6;
    std::array<std::array<GPUMesh, 3>, debrisSurfaceRole + 1> craftMeshes;
    size_t craftBytes = 0;
    SurfaceLifeCache surfaceLife;
    std::array<std::array<GPUMesh, 3>, size_t(SurfaceMesh::Count)> surfaceMeshes;
    size_t surfaceMeshBytes = 0;
    std::string weatherReport;
    std::string weatherDetail;
    Mesh weatherRain, weatherFlakes;
    double weatherSeverity = 0, weatherPriority = 1e100;
    std::unique_ptr<HyperionRenderer> hyperion;
    static constexpr RenderMode mode = RenderMode::Hyperion;
    std::optional<Address> specimen;
    std::shared_ptr<const std::vector<ProspectSite>> prospectSites;
    Mesh prospectMarkers;
    double visualTime = 0, fixedVisualTime = -1;
    double warpLight = 0, warpClock = 0, warpTransition = 1;
    bool warpEngaged = false, warpTransitionVisible = false;
    bool warpDestinationExit = false;
    std::optional<Address> warpOpticalDestination;
    double warpExitBore = .21;
    glm::dvec2 warpFocus{};
    bgfx::ViewId atomView = 2;
    std::map<const Geometry*, GPUGeometry> gpu;
    std::vector<Pick> picks;
    std::vector<Instance> billboards;
    std::vector<Instance> cosmicBillboards;
    std::vector<Instance> pointLightBillboards;
    std::vector<Instance> stellarBillboards;
    std::vector<LocalFrame> anchorFrames;
    std::map<Address, std::vector<LocalFrame>> frameFrames;
    std::map<Address, Pose> framePoses;
    int width = 1280, height = 800;
    uint64_t frame = 0;
    size_t gpuBytes = 0;
    size_t staticUploadBytes = 0;
    glm::dmat4 view{1}, projection{1};
    glm::dquat cameraQ{1, 0, 0, 0}, cameraInverse{1, 0, 0, 0};
    glm::dvec3 eye{};
    bool initialized = false;
    FrameStats stats;
    struct DetailState {
        unsigned level = 0;
        bool initialized = false;
        GeometryBlend blend;
        uint64_t used = 0;
    };
    std::map<Address, DetailState> details;
    std::map<std::string, DetailState> localSections;
    std::chrono::steady_clock::time_point previousTime = std::chrono::steady_clock::now();
    double frameSeconds = 0, fieldOfView = 1;
    std::string sectionLabel;
    TerrainStreamer terrain;
    bool terrainActive = false, terrainSunComplete = true, sunShadows = true;
    bool probeRemote = false;
    IceStress iceStress;
    bool probeOptimisation = true, exteriorHidden = false;
    uint64_t enclosedBore = 0;
    const Navigation* shipNavigation = nullptr;
    double probeSeconds = 0;
    GPUMesh probeHousing, probeCutter, probeLamps, probeRoller, probeDrive, probeActuator, probeRod, probeHotTeeth;
    size_t probeRigBytes = 0;
    std::vector<Vertex> probeLightScratch;
    Mesh probeFragments, probeDust, probeSparks;
    bool terrainCoverageDebug = false;
    std::optional<Address> terrainSunPlanet;
    glm::dvec3 terrainSunAnchor{};
    double terrainSunSpan = 0;
    explicit Impl(SDL_Window* w, const Universe& u) : universe(u), window(w), surfaceLife(u), terrain(u) {}
    bgfx::ViewId worldView() const {
        return instruments ? 2 : 0; // Flight sky precedes terrain (1) and opaque local geometry (2).
    }
    CockpitViewport cockpitViewport() const {
        int logicalW = width, logicalH = height;
        SDL_GetWindowSize(window, &logicalW, &logicalH);
        SDL_Rect safe{0, 0, logicalW, logicalH};
        if (!SDL_GetWindowSafeArea(window, &safe))
            safe = {0, 0, logicalW, logicalH};
        const double sx = double(width) / std::max(1, logicalW);
        const double sy = double(height) / std::max(1, logicalH);
        return {width, height, {safe.x * sx, safe.y * sy, safe.w * sx, safe.h * sy}};
    }
    ~Impl() {
        for (auto& [key, g] : gpu)
            if (g.normalWork)
                g.normalWork->cancel.store(true);
        normalTasks.wait();
        if (initialized) {
            for (auto& [key, g] : gpu) {
                destroy(g.solid);
                destroy(g.lines);
                if(bgfx::isValid(g.probeLight))bgfx::destroy(g.probeLight);
                if (bgfx::isValid(g.normals))
                    bgfx::destroy(g.normals);
                if (bgfx::isValid(g.instances))
                    bgfx::destroy(g.instances);
            }
            for (auto& mesh : atomSpheres)
                destroy(mesh);
            destroy(cabin);
            destroy(probeHousing);destroy(probeCutter);destroy(probeLamps);
            destroy(probeRoller);destroy(probeDrive);destroy(probeActuator);destroy(probeHotTeeth);
            destroy(probeRod);
            destroy(hyperionBillboardMesh);
            destroy(pointLightBillboardMesh);
            for (auto& role : craftMeshes)
                for (auto& mesh : role)
                    destroy(mesh);
            for (auto& role : surfaceMeshes)
                for (auto& mesh : role)
                    destroy(mesh);
            for (auto& role : workMeshes)
                for (auto& mesh : role)
                    destroy(mesh);
            hyperion.reset();
            if (bgfx::isValid(colorProgram))
                bgfx::destroy(colorProgram);
            bgfx::shutdown();
        }
    }
    GPUMesh upload(const Mesh& m, std::span<const uint32_t> colors = {}) {
        GPUMesh result;
        if (m.vertices.empty() || m.indexCount() == 0)
            return result;
        stats.uploadBytes += m.bytes();
        staticUploadBytes += m.bytes();
        const auto memory = bgfx::copy(m.vertices.data(), uint32_t(m.vertices.size() * sizeof(Vertex)));
        if (colors.size() == m.vertices.size()) {
            auto vertices = reinterpret_cast<Vertex*>(memory->data);
            for (size_t i = 0; i < colors.size(); ++i)
                vertices[i].color = colors[i];
        }
        result.vertices = bgfx::createVertexBuffer(memory, layout);
        result.indices = bgfx::createIndexBuffer(
            bgfx::copy(m.indexData(), static_cast<uint32_t>(m.indexCount() * m.indexWidth())),
            m.wideIndices() ? BGFX_BUFFER_INDEX32 : 0);
        result.count = m.indexCount();
        return result;
    }
    glm::dvec3 cameraSpace(glm::dvec3 p) const {
        return cameraInverse * (p - eye);
    }
    std::optional<glm::dvec2> project(glm::dvec3 p) const {
        auto clip = projection * view * glm::dvec4(p, 1);
        if (clip.w <= .0001)
            return {};
        auto q = glm::dvec3(clip) / clip.w;
        if (std::abs(q.x) > 1.2 || std::abs(q.y) > 1.2)
            return {};
        return glm::dvec2((q.x * .5 + .5) * width, (.5 - q.y * .5) * height);
    }
    const std::vector<LocalFrame>& framesFor(const Address& address) {
        auto it = frameFrames.find(address);
        if (it != frameFrames.end())
            return it->second;
        return frameFrames.emplace(address, universe.frames(address)).first->second;
    }
    Pose poseFor(const Address& address, const Navigation& nav) {
        auto it = framePoses.find(address);
        if (it != framePoses.end())
            return it->second;
        auto pose = relativePose(framesFor(address), address, anchorFrames, nav.anchor, nav.logDistance);
        framePoses.emplace(address, pose);
        return pose;
    }
    void addBillboard(glm::dvec3 p, double radius, glm::vec3 color, bool cosmic = false,
                      float opticalMaterial = 0, bool background = false) {
        ++stats.particles;
        if (background)
            color *= stellarExposure;
        const bool filteredPoint = pointLightFiltering && background && opticalMaterial == 0;
        if (filteredPoint) {
            const double depth = -cameraSpace(p).z;
            if (depth > 0 && radius > 0) {
                const double radiusPixels = radius * height * .5 * projection[1][1] / depth;
                const auto footprint = pointLightFootprint(radiusPixels);
                radius *= footprint.radiusScale;
                color *= float(footprint.radianceScale);
            }
        }
        auto right = cameraQ * glm::dvec3(radius, 0, 0), up = cameraQ * glm::dvec3(0, radius, 0);
        glm::mat4 transform(1);
        transform[0] = glm::vec4(glm::vec3(right), 0);
        transform[1] = glm::vec4(glm::vec3(up), 0);
        transform[3] = glm::vec4(glm::vec3(p), 1);
        (filteredPoint          ? pointLightBillboards
         : opticalMaterial >= 3 ? stellarBillboards
         : cosmic               ? cosmicBillboards
                                : billboards)
            .push_back({transform, glm::vec4(color, 1.f + opticalMaterial)});
    }
    bool prepareGPU(const std::shared_ptr<const Geometry>& geometry) {
        if (!geometry)
            return false;
        auto it = gpu.find(geometry.get());
        if (it != gpu.end() && it->second.source.expired()) {
            gpuBytes -= it->second.bytes;
            destroy(it->second.solid);
            destroy(it->second.lines);
            if(bgfx::isValid(it->second.probeLight))bgfx::destroy(it->second.probeLight);
            if (it->second.normalWork)
                it->second.normalWork->cancel.store(true);
            if (bgfx::isValid(it->second.normals))
                bgfx::destroy(it->second.normals);
            if (bgfx::isValid(it->second.instances))
                bgfx::destroy(it->second.instances);
            gpu.erase(it);
            it = gpu.end();
        }
        if (it != gpu.end())
            return true;
        size_t bytes = geometry->solid.bytes() + geometry->lines.bytes() +
                       geometry->atomInstances.size() * sizeof(Instance);
        if (staticUploadBytes && staticUploadBytes + bytes > 2 * 1024 * 1024) {
            ++stats.uploadDeferred;
            return false;
        }
        GPUGeometry item;
        item.solid = upload(geometry->solid, geometry->exposureColors);
        item.lines = upload(geometry->lines);
        if (!geometry->atomInstances.empty()) {
            size_t count = geometry->atomInstances.size() * sizeof(Instance);
            item.instances = bgfx::createVertexBuffer(
                bgfx::copy(geometry->atomInstances.data(), static_cast<uint32_t>(count)), instanceLayout);
            stats.uploadBytes += count;
            staticUploadBytes += count;
        }
        item.source = geometry;
        item.bytes = bytes;
        gpuBytes += bytes;
        gpu.emplace(geometry.get(), std::move(item));
        return true;
    }
    void prepareNormals(GPUGeometry& item, const std::shared_ptr<const Geometry>& geometry) {
        // One bounded CPU task at a time. Keep the original flat-shaded mesh
        // visible until its read-only lighting stream is ready.
        const auto count = geometry->solid.vertices.size();
        if (bgfx::isValid(item.normals) || !count || count > 65536 || geometry->address.kind() >= Kind::Grain)
            return;
        if (item.normalWork && item.normalWork->ready.load(std::memory_order_acquire)) {
            auto& data = item.normalWork->result;
            const size_t bytes = data.size() * sizeof(ShadingNormal);
            if (bytes && staticUploadBytes + bytes <= 2 * 1024 * 1024) {
                item.normals =
                    bgfx::createVertexBuffer(bgfx::copy(data.data(), uint32_t(bytes)), normalLayout);
                item.bytes += bytes;
                gpuBytes += bytes;
                stats.uploadBytes += bytes;
                staticUploadBytes += bytes;
                item.normalWork.reset();
            }
        } else if (!item.normalWork && normalJobs.load() == 0) {
            auto work = item.normalWork = std::make_shared<NormalWork>();
            ++normalJobs;
            normalTasks.run([this, geometry, work] {
                try {
                    work->result = surfaceNormals(geometry->solid, &work->cancel);
                } catch (...) {
                    work->cancel.store(true);
                }
                work->ready.store(true, std::memory_order_release);
                --normalJobs;
            });
        }
    }
    void drawMesh(const GPUMesh& m, const glm::mat4& model, float opacity = 1, bool lines = false,
                  bgfx::VertexBufferHandle normals = BGFX_INVALID_HANDLE, bool stellar = false,glm::vec3 tint=glm::vec3(1)) {
        if (!bgfx::isValid(m.vertices) || !bgfx::isValid(m.indices) ||
            bgfx::getAvailInstanceDataBuffer(1, 80) < 1)
            return;
        bgfx::InstanceDataBuffer idb;
        bgfx::allocInstanceDataBuffer(&idb, 1, 80);
        Instance item{model, glm::vec4(tint,opacity)};
        std::memcpy(idb.data, &item, sizeof(item));
        bgfx::setVertexBuffer(0, m.vertices);
        bgfx::setIndexBuffer(m.indices);
        const bool smooth = bgfx::isValid(normals);
        if (smooth)
            bgfx::setVertexBuffer(1, normals);
        bgfx::setInstanceDataBuffer(&idb);
        uint64_t state =
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_GREATER | BGFX_STATE_MSAA;
        if (lines)
            state |= BGFX_STATE_PT_LINES;
        if (opacity > .99f)
            state |= BGFX_STATE_WRITE_Z;
        else
            state |= BGFX_STATE_BLEND_ALPHA;
        bgfx::setState(state);
        bgfx::submit(worldView(), hyperion->geometryProgram(smooth, stellar));
    }
    void drawLines(const GPUMesh& m, const glm::mat4& model) {
        if (!bgfx::isValid(m.vertices))
            return;
        bgfx::setTransform(glm::value_ptr(model));
        bgfx::setVertexBuffer(0, m.vertices);
        bgfx::setIndexBuffer(m.indices);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_PT_LINES | BGFX_STATE_DEPTH_TEST_GEQUAL |
                       BGFX_STATE_MSAA);
        bgfx::submit(worldView(), colorProgram);
    }
    void transient(const Mesh& mesh, bgfx::ViewId viewId, bool lines, bool additive, bool depth = true,
                   bool writeDepth = false, bool alpha = false) {
        if (mesh.vertices.empty())
            return;
        uint32_t nv = static_cast<uint32_t>(mesh.vertices.size()),
                 ni = static_cast<uint32_t>(mesh.indexCount());
        if (bgfx::getAvailTransientVertexBuffer(nv, layout) < nv ||
            bgfx::getAvailTransientIndexBuffer(ni, mesh.wideIndices()) < ni)
            return;
        bgfx::TransientVertexBuffer vb;
        bgfx::TransientIndexBuffer ib;
        bgfx::allocTransientVertexBuffer(&vb, nv, layout);
        bgfx::allocTransientIndexBuffer(&ib, ni, mesh.wideIndices());
        std::memcpy(vb.data, mesh.vertices.data(), nv * sizeof(Vertex));
        std::memcpy(ib.data, mesh.indexData(), ni * mesh.indexWidth());
        bgfx::setVertexBuffer(0, &vb);
        bgfx::setIndexBuffer(&ib);
        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_MSAA;
        if (lines)
            state |= BGFX_STATE_PT_LINES | (depth ? BGFX_STATE_DEPTH_TEST_GEQUAL : 0);
        else if (depth)
            state |= BGFX_STATE_DEPTH_TEST_GEQUAL;
        if (writeDepth)
            state |= BGFX_STATE_WRITE_Z;
        if (additive)
            state |= BGFX_STATE_BLEND_ADD;
        else if (!depth || alpha)
            state |= BGFX_STATE_BLEND_ALPHA;
        bgfx::setState(state);
        bgfx::submit(viewId, colorProgram);
    }
    void bindPresentation(const Geometry& geometry, const Node& node, const Navigation& nav,
                          const glm::mat4& model, const Pose& pose, double radius, float primitive) {
        auto presentation = extractPresentation(node, geometry, mode);
        const auto& sourceFrames = framesFor(geometry.referenceFrame);
        double sourceFactor = std::pow(10.0, geometry.logUnits - sourceFrames.back().logRadius);
        // A patch and its enclosing section share the same authored material
        // frame. Sampling windows must never appear as coloured rectangles.
        const auto& materialAddress = presentation.materialFrame;
        if (materialAddress.kind() == Kind::Planet && node.address != materialAddress)
            describePlanetPresentation(presentation, universe.describe(materialAddress));
        if (materialAddress.kind() == Kind::Planet && presentation.planetRings[0].z > 0)
            presentation.planet[4] =
                glm::vec4(glm::vec3(glm::inverse(poseFor(materialAddress, nav).orientation) *
                                    lightAt(materialAddress, nav)),
                          0);
        if (!planetRings)
            presentation.planetRings = {};
        const auto& materialFrames = framesFor(materialAddress);
        auto transform = pointTransform(sourceFrames, geometry.referenceFrame, materialFrames,
                                        materialAddress, materialFrames.back().logRadius);
        glm::dmat4 mapping(1);
        auto linear = transform.linear * sourceFactor;
        for (unsigned i = 0; i < 3; ++i)
            mapping[i] = glm::dvec4(linear[i], 0);
        mapping[3] = glm::dvec4(transform.translation - transform.linear * transform.pivot, 1);
        auto pattern = glm::mat4(mapping);
        if (primitive == 1) {
            auto localEye = glm::inverse(pose.orientation) * (eye - pose.position) / radius;
            const auto light = glm::inverse(pose.orientation) * lightAt(materialAddress, nav);
            hyperion->bind(presentation, glm::vec3(localEye), glm::vec3(light), pattern, primitive,
                           visualTime);
        } else {
            auto light = lightAt(materialAddress, nav);
            glm::mat4 micro(0);
            float relief = 0;
            if (node.address.kind() >= Kind::Planet) {
                const double materialRadius = std::pow(10., materialFrames.back().logRadius);
                const double spacing =
                    materialAddress.kind() == Kind::Planet ? 8. : std::max(1e-12, materialRadius * .02);
                const auto localEye = glm::inverse(pose.orientation) * (eye - pose.position) / radius;
                const auto canonical = mapping * glm::dvec4(localEye, 1);
                const auto materialPose = poseFor(materialAddress, nav);
                const glm::dmat3 linear = glm::mat3_cast(glm::inverse(materialPose.orientation)) *
                                          (std::pow(10., nav.logDistance) / spacing);
                for (unsigned i = 0; i < 3; i++)
                    micro[i] = glm::vec4(glm::vec3(linear[i]), 0);
                micro[3] = glm::vec4(
                    glm::vec3(glm::mod(glm::dvec3(canonical) * (materialRadius / spacing), glm::dvec3(64))),
                    1);
                relief = float(spacing * .045 / std::pow(10., nav.logDistance));
            }
            hyperion->bind(presentation, glm::vec3(eye), glm::vec3(light), pattern * glm::inverse(model),
                           primitive, visualTime, {}, micro, relief);
        }
    }
    glm::dvec3 lightAt(const Address& material, const Navigation& nav) {
        if (instruments || (material.kind() >= Kind::Planet && material.kind() <= Kind::Mountain)) {
            auto system = material;
            while (system.kind() > Kind::System)
                system = system.parent();
            if (system.kind() == Kind::System && material.kind() > Kind::System) {
                const auto vector = poseFor(system, nav).position - poseFor(material, nav).position;
                if (glm::length(vector) > 1e-30)
                    return glm::normalize(vector);
            }
        }
        return poseFor(material, nav).orientation * glm::dvec3(.6, .7, -.4);
    }
    void drawGeometry(const std::shared_ptr<const Geometry>& geometry, const Node& node,
                      const Navigation& nav, float opacity) {
        if (!geometry || opacity < .001f)
            return;
        auto pose = poseFor(geometry->referenceFrame, nav);
        double r = std::pow(10.0, geometry->logUnits - nav.logDistance);
        if (geometry->section && geometry->inspectionRadius > 0 &&
            glm::length(eye - pose.position) >= geometry->inspectionRadius * r * .99)
            return; // An obsolete, smaller section must never occlude the focus.
        if (!prepareGPU(geometry))
            return;
        auto& item = gpu.at(geometry.get());
        item.lastUsed = frame;
        prepareNormals(item, geometry);
        stats.vertices += geometry->solid.vertices.size();
        stats.triangles += geometry->solid.indexCount() / 3;
        auto model = glm::translate(glm::mat4(1), glm::vec3(pose.position)) *
                     glm::mat4_cast(glm::quat(pose.orientation)) *
                     glm::scale(glm::mat4(1), glm::vec3(static_cast<float>(r)));
        if (bgfx::isValid(item.solid.vertices))
            bindPresentation(*geometry, node, nav, model, pose, r, bgfx::isValid(item.normals) ? .25f : 0.f);
        const auto wearTint=economy?prospectWearTint(universe,node,economy->exploration):glm::vec3(1);
        drawMesh(item.solid, model, opacity, false, item.normals, node.address.kind() == Kind::System,wearTint);
        if (bgfx::isValid(item.lines.vertices))
            bindPresentation(*geometry, node, nav, model, pose, r, 2);
        drawMesh(item.lines, model, opacity, true);
        const auto& sourceFrames = framesFor(geometry->referenceFrame);
        double sourceFactor = std::pow(10.0, geometry->logUnits - sourceFrames.back().logRadius);
        auto transform =
            pointTransform(sourceFrames, geometry->referenceFrame, anchorFrames, nav.anchor, nav.logDistance);
        std::unordered_set<uint64_t> represented;
        if (geometry->address.kind() == Kind::Galaxy) {
            if (geometry->address.ancestorOf(nav.anchor) &&
                nav.anchor.path.size() > geometry->address.path.size())
                represented.insert(nav.anchor.path[geometry->address.path.size()].id);
            for (auto& [address, state] : details)
                if (address.kind() == Kind::System && address.parent() == geometry->address &&
                    state.used + 1 >= frame && state.blend.current)
                    represented.insert(address.id());
        }
        size_t particleIndex = 0;
        for (const auto& particle : geometry->particles) {
            bool skip = false;
            if (geometry->address.kind() == Kind::Galaxy && particle.objectId != UINT64_MAX) {
                skip = represented.contains(particle.objectId);
            }
            // The addressed stellar proxy below is the same emitter at all scales.
            if (geometry->address.kind() == Kind::System && particleIndex == 0)
                skip = true;
            ++particleIndex;
            if (skip)
                continue;
            auto position = transform(particle.position * sourceFactor);
            double distanceScale = 1;
            float attenuation = 1;
            if (geometry->address.kind() == Kind::Cosmic) {
                const double distance = glm::length(position - eye);
                const double rootRadius = std::pow(10., rootCellLogMetres - nav.logDistance);
                attenuation =
                    float((1 - smoothstep(1.2, 1.9, distance / rootRadius)) / double(geometry->detail + 1));
                if (attenuation < .001f)
                    continue;
                // Preserve the actual emitter's direction/angular size beyond
                // the local depth range. This is the streamed scene, not a sky texture.
                if (distance > 1e-12) {
                    distanceScale = 500 / distance;
                    position = eye + (position - eye) * distanceScale;
                }
            }
            auto p = cameraSpace(position);
            if (p.z >= 0)
                continue;
            const bool stellar = geometry->address.kind() == Kind::System &&
                                 glm::dot(particle.position, particle.position) < 1e-24;
            double size = std::clamp(static_cast<double>(particle.radius) * r * distanceScale, -p.z * .0005,
                                     -p.z * (stellar ? .8 : .035));
            if (LOD::visible(p, size, static_cast<double>(width) / height, fieldOfView))
                addBillboard(position, size, particle.color * (opacity * attenuation),
                             geometry->address.kind() == Kind::Cosmic,
                             stellar ? 3.f + hyperionPhase(node.seed) * .25f
                             : geometry->address.kind() == Kind::Cosmic ? 2.f
                             : geometry->address.kind() == Kind::Galaxy && particle.objectId == UINT64_MAX
                                 ? 1.f
                                 : 0.f,
                             geometry->address.kind() <= Kind::Galaxy);
        }
        if (!geometry->lattice.atoms.empty() && bgfx::isValid(item.instances) && atomView < 64) {
            double diameter =
                LOD::pixels(r * geometry->lattice.atoms.front().radius,
                            std::max(.05, glm::length(cameraSpace(pose.position)) - r), height, 1);
            unsigned level = atomPresentationLevel(diameter, false);
            unsigned rings = level == 3   ? 24
                             : level == 2 ? 12
                             : level == 1 ? 8
                                          : 4,
                     sectors = level == 3   ? 40
                               : level == 2 ? 20
                               : level == 1 ? 12
                                            : 6;
            size_t count = geometry->lattice.atoms.size();
            auto localView = glm::mat4(view) * model;
            glm::mat4 proj(projection);
            auto viewId = atomView++;
            bgfx::setViewTransform(viewId, glm::value_ptr(localView), glm::value_ptr(proj));
            bgfx::setViewRect(viewId, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
            bgfx::setVertexBuffer(0, atomSpheres[level].vertices);
            bgfx::setIndexBuffer(atomSpheres[level].indices);
            bgfx::setInstanceDataBuffer(item.instances, 0, static_cast<uint32_t>(count));
            uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_GREATER | BGFX_STATE_MSAA;
            if (opacity > .99f)
                state |= BGFX_STATE_WRITE_Z;
            else
                state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_FACTOR, BGFX_STATE_BLEND_INV_FACTOR);
            bgfx::setState(state, rgba(glm::vec3(opacity), opacity));

            bindPresentation(*geometry, node, nav, model, pose, r, 1);
            bgfx::submit(viewId, hyperion->geometryProgram());
            stats.atoms += count;
            stats.atomTriangles += count * rings * sectors * 2;
        }
    }
    void drawPlanetRings(const Node& planet, const Navigation& nav) {
        if (!planetRings || planet.address.kind() != Kind::Planet || hasSolidSurface(planet))
            return;
        const auto t = planetTraits(planet);
        if (!t.rings.present())
            return;
        auto detail = details.find(planet.address);
        if (detail == details.end() || !detail->second.blend.current)
            return;
        const auto pose = poseFor(planet.address, nav);
        const double radius = std::pow(10., planet.frame.logRadius - nav.logDistance);
        const auto centre = pose.position - eye;
        if (glm::length(centre) > worldFarPlane * .8 || radius > worldFarPlane ||
            !LOD::visible(cameraInverse * centre, radius * t.rings.outerRadius, double(width) / height,
                          fieldOfView))
            return;
        const auto frame = tangentFrame(t.rings.axis);
        const auto rotation = pose.orientation * frame;
        const glm::mat4 view = glm::mat4_cast(glm::quat(cameraInverse)), proj(projection);
        bgfx::setViewTransform(65, glm::value_ptr(view), glm::value_ptr(proj));
        bgfx::setViewRect(65, 0, 0, uint16_t(width), uint16_t(height));
        const auto model = glm::translate(glm::mat4(1), glm::vec3(centre)) *
                           glm::mat4_cast(glm::quat(rotation)) *
                           glm::scale(glm::mat4(1), glm::vec3(float(radius * t.rings.outerRadius)));
        const Vertex vertices[] = {
            {-1, 0, -1, 0xffffffff}, {1, 0, -1, 0xffffffff}, {1, 0, 1, 0xffffffff}, {-1, 0, 1, 0xffffffff}};
        const uint16_t indices[] = {0, 1, 2, 0, 2, 3};
        if (bgfx::getAvailTransientVertexBuffer(4, layout) < 4 || bgfx::getAvailTransientIndexBuffer(6) < 6)
            return;
        bgfx::TransientVertexBuffer vb;
        bgfx::TransientIndexBuffer ib;
        bgfx::allocTransientVertexBuffer(&vb, 4, layout);
        bgfx::allocTransientIndexBuffer(&ib, 6);
        std::memcpy(vb.data, vertices, sizeof(vertices));
        std::memcpy(ib.data, indices, sizeof(indices));
        bgfx::setTransform(glm::value_ptr(model));
        bgfx::setVertexBuffer(0, &vb);
        bgfx::setIndexBuffer(&ib);
        const auto light = glm::inverse(rotation) * lightAt(planet.address, nav);
        const auto localEye = glm::inverse(rotation) * (-centre / radius);
        const auto program = hyperion->ringProgram(t, glm::vec3(light), glm::vec3(localEye), 1);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_GEQUAL | BGFX_STATE_BLEND_ALPHA |
                       BGFX_STATE_MSAA);
        bgfx::submit(65, program);
        ++stats.planetRings;
    }
    void drawBlend(DetailState& state, const Node& node, const Navigation& nav, float opacity) {
        if (state.blend.active())
            ++stats.lodTransitions;
        drawGeometry(state.blend.previous, node, nav, opacity * (1 - state.blend.weight()));
        drawGeometry(state.blend.current, node, nav, opacity * state.blend.weight());
    }
    void stellarSky(const Node& galaxy, const Navigation& nav, RegionCache& cache) {
        if(exteriorHidden)return;
        if (!instruments)
            return;
        // Share the ordinary galaxy's fade interval so magnification never
        // removes its stars before a smaller reference frame becomes ready.
        const double radius = std::pow(10., galaxy.frame.logRadius - nav.logDistance);
        if (radius <= 3)
            return;
        const float handoff = float(smoothstep(3., 12., radius));
        // Refine the same addressed stellar population asynchronously. A pixel
        // floor, like Hyperion skyFragment's derivative-filtered points, keeps
        // unresolved emitters from disappearing between MSAA samples.
        auto geometry = cache.request(galaxy, 4, 6);
        if (!geometry)
            return;
        skyGeometry = geometry;
        const auto galaxyEye = universe.pointRelative(
            nav.anchor, eye * std::pow(10., nav.logDistance - anchorFrames.back().logRadius), galaxy.address,
            galaxy.frame.logRadius);
        const auto orientation = poseFor(galaxy.address, nav).orientation;
        const double factor = std::pow(10., geometry->logUnits - galaxy.frame.logRadius);
        uint64_t active = UINT64_MAX;
        if (nav.anchor.path.size() > galaxy.address.path.size())
            active = nav.anchor.path[galaxy.address.path.size()].id;
        for (const auto& star : geometry->particles) {
            if (star.objectId == active)
                continue;
            const auto delta = orientation * (star.position * factor - galaxyEye);
            const double distance = glm::length(delta);
            if (distance < 1e-15)
                continue;
            const auto direction = delta / distance;
            const auto local = cameraInverse * direction;
            if (local.z >= 0)
                continue;
            const bool gas = star.objectId == UINT64_MAX;
            const double pixelAngle = 2 * std::tan(fieldOfView * .5) / height;
            const double angular = gas ? std::clamp(double(star.radius) * factor / distance, .004, .04)
                                       : std::clamp(double(star.radius) * factor / distance,
                                                    pixelAngle * 1.05, pixelAngle * 2.7);
            const double size = 500 * angular;
            if (LOD::visible(local * 500., size, double(width) / height, fieldOfView)) {
                const double physicalDistance = distance * std::pow(10., galaxy.frame.logRadius);
                if (celestialOccluded(local, physicalDistance, skyOccluders))
                    continue;
                const auto point = eye + direction * 500.;
                addBillboard(point, size, star.color * (handoff * (gas ? .5f : 1.f)), true, gas ? 1.f : 0.f,
                             true);
                if (gas)
                    ++stats.skyGas;
                else {
                    ++stats.skyStars;
                    if (auto screen = project(point))
                        picks.push_back(
                            {galaxy.address.child(Kind::System, star.objectId), *screen, physicalDistance});
                }
            }
        }
    }
    void galaxySky(const Node& root, const Navigation& nav, RegionCache& cache, double exposure) {
        if(exteriorHidden)return;
        if (!instruments || exposure < .0001)
            return;
        const auto catalog = cache.catalog(root);
        if (!catalog)
            return;
        const auto rootEye = universe.pointRelative(
            nav.anchor, eye * std::pow(10., nav.logDistance - anchorFrames.back().logRadius), root.address,
            root.frame.logRadius);
        const auto orientation = poseFor(root.address, nav).orientation;
        const double pixelAngle = 2 * std::tan(fieldOfView * .5) / height;
        for (const auto& galaxy : *catalog) {
            if (galaxy.address.ancestorOf(nav.anchor))
                continue;
            const auto delta = orientation * (galaxy.frame.position - rootEye);
            const double distance = glm::length(delta);
            if (distance <= 1e-15)
                continue;
            const auto direction = delta / distance;
            const auto local = cameraInverse * direction;
            if (local.z >= 0 ||
                celestialOccluded(local, distance * std::pow(10., root.frame.logRadius), skyOccluders))
                continue;
            const double angular =
                std::clamp(std::pow(10., galaxy.frame.logRadius - root.frame.logRadius) / distance,
                           pixelAngle * .85, pixelAngle * 2.);
            if (LOD::visible(local * 500., angular * 500., double(width) / height, fieldOfView)) {
                const auto point = eye + direction * 500.;
                const glm::vec3 tint = galaxy.galaxy.type == GalaxyType::Elliptical
                                           ? glm::vec3(.65, .49, .32)
                                           : glm::vec3(.34, .48, .68);
                addBillboard(point, angular * 500., tint * float(exposure), true, 0, true);
                if (auto screen = project(point))
                    picks.push_back(
                        {galaxy.address, *screen, distance * std::pow(10., root.frame.logRadius)});
                ++stats.skyGalaxies;
            }
        }
    }
#include "SurfaceRendering.hpp"
    void renderLanding(const Navigation& nav, const std::optional<Node>& planet) {
        if (!instruments || !planet || !terrainActive || exteriorHidden || probeRemote || specimen)
            return;
        const auto& landing = instruments->landing;
        if (landing.body != planet->address)
            return;
        const auto up = landing.direction;
        const auto tangent = landing.forward - up * glm::dot(landing.forward, up);
        const auto ahead = glm::length(tangent) > .01 ? glm::normalize(tangent) :
                           glm::normalize(glm::cross(up, std::abs(up.y) < .8 ? glm::dvec3(0, 1, 0) :
                                                                                            glm::dvec3(1, 0, 0)));
        const auto right = glm::cross(ahead, up);
        const double radius = std::pow(10., planet->frame.logRadius);
        const double units = std::pow(10., nav.logDistance);
        const double size = instruments->hullMetres / 12.;
        const bool settled = landing.phase == LandingPhase::Landed;
        const bool flare = landing.phase == LandingPhase::Flare;
        const auto planetEye = universe.pointRelative(
            nav.anchor, eye * std::pow(10., nav.logDistance - anchorFrames.back().logRadius),
            planet->address, 0);
        const auto orientation = poseFor(planet->address, nav).orientation;
        auto groundPoint = [&](glm::dvec2 p) {
            const auto n = glm::normalize(up + (right * p.x + ahead * p.y) * (size / radius));
            return orientation *
                   (n * (radius + universe.terrainHeight(*planet, n) + (settled ? .85 : .46) * size) -
                    planetEye) / units;
        };
        Mesh light;
        auto strip = [&](glm::dvec3 a, glm::dvec3 b, glm::dvec3 across, double width, glm::vec3 color) {
            const auto edge = orientation * across * (width * size / units);
            const auto first = uint32_t(light.vertices.size());
            for (int i : {-1, 0, 1}) {
                const auto tint = rgba(i == 0 ? color : glm::vec3(0));
                for (auto p : {a + double(i) * edge, b + double(i) * edge})
                    light.vertices.push_back({float(p.x), float(p.y), float(p.z), tint});
            }
            for (uint32_t i : {0, 1, 2, 1, 3, 2, 2, 3, 4, 3, 5, 4})
                light.indices.push_back(first + i);
        };
        const glm::vec3 violet(.71f, .20f, .96f), cyan(.12f, .86f, 1.f), mint(.33f, 1.f, .70f);
        const auto rim = settled || flare ? mint : cyan;
        // Four open corners keep the actual terrain and pad markings readable.
        const double ring = settled ? 20. + (1. - landing.settle) * 16. : 16.;
        for (int quadrant = 0; quadrant < 4; ++quadrant) {
            const double start = quadrant * std::numbers::pi / 2. - .57;
            glm::dvec3 previous{};
            for (int i = 0; i <= 6; ++i) {
                const double angle = start + i * .19;
                const glm::dvec2 direction(std::cos(angle), std::sin(angle));
                const auto point = groundPoint(direction * ring);
                if (i != 0) {
                    const float gain = settled ? float(landing.settle * landing.settle) :
                                       flare ? .92f : .66f;
                    const auto outward = right * direction.x + ahead * direction.y;
                    strip(previous, point, outward, settled ? 1.5 : .74, rim * gain);
                    strip(previous, point, outward, .28, rim * gain);
                }
                previous = point;
            }
        }
        if (!settled) {
            // Chase the lights toward the footprint. The forward section is
            // visible through a level cockpit during the vertical final.
            for (int row = 0; row < 7; ++row) {
                const double along = 22. + 12. * row + 1.35 * row * row;
                const double spread = 8. + 1.2 * row;
                const double wave = .5 + .5 * std::cos(instruments->flightSeconds * 6. + row * .85);
                const auto color = glm::mix(rim, violet, float(row) / 9.) *
                                   float((.54 + .46 * wave * wave) * (flare ? .75 : 1.));
                for (double side : {-1., 1.}) {
                    const auto a = groundPoint({side * spread, along});
                    const auto b = groundPoint({side * (spread + 2.8), along + 7.});
                    strip(a, b, right, 1.35, color * .68f);
                    strip(a, b, right, .45, color);
                }
            }
        }
        const auto view = glm::mat4_cast(glm::quat(cameraInverse));
        const glm::mat4 proj(projection);
        bgfx::setViewTransform(62, glm::value_ptr(view), glm::value_ptr(proj));
        bgfx::setViewRect(62, 0, 0, uint16_t(width), uint16_t(height));
        bgfx::setTransform(glm::value_ptr(glm::mat4(1)));
        stats.landingGuideVertices = light.vertices.size();
        transient(light, 62, false, true);
    }
    void renderActivity(const std::vector<Node>& nodes, const Navigation& nav, RegionCache& cache) {
        activityContacts.clear();
        weaponTargets.clear();
        combatBodies.clear();
        activityPicks.clear();
        stats.activityBytes = craftBytes;
        renderSurfaceEnvironment(nodes, nav, cache);
        if (!activity || !activity->enabled || nav.anchor.kind() >= Kind::Rock)
            return;
        std::array<std::array<std::vector<Instance>, 3>, debrisSurfaceRole + 1> batches;
        // Like the terrain pass, subtract the double-precision eye before
        // uploading floats. Derivative normals then remain stable near a hull.
        const auto rotationView = glm::mat4_cast(glm::quat(cameraInverse));
        const glm::mat4 actorProjection(projection);
        bgfx::setViewTransform(62, glm::value_ptr(rotationView), glm::value_ptr(actorProjection));
        bgfx::setViewRect(62, 0, 0, uint16_t(width), uint16_t(height));
        Mesh exhaust;
        Mesh dockingLights;
        std::vector<ActivityContact> debrisContacts;
        const double units = std::pow(10., nav.logDistance);
        for (const auto& system : nodes) {
            if (system.address.kind() != Kind::System)
                continue;
            const auto planets = cache.catalog(system);
            if (!planets)
                continue;
            const auto* offence = weapons && npcCombat
                                      ? weapons->hunterOffence(system.address, activity->seconds)
                                      : nullptr;
            for (const auto& planet : *planets) {
                const bool moon = isMoon(planet.address);
                const bool pinned = activity->selected && activity->selected->home == planet.address;
                const bool pinnedDebris = pinned && isOrbitalDebris(*activity->selected);
                if (moon && !pinnedDebris && !planet.address.ancestorOf(nav.anchor) &&
                    !(nav.selected && planet.address.ancestorOf(*nav.selected)))
                    continue;
                const auto pose = poseFor(planet.address, nav);
                const auto planetEye = universe.pointRelative(
                    nav.anchor, eye * std::pow(10., nav.logDistance - anchorFrames.back().logRadius),
                    planet.address, 0);
                const auto shipEye=probeRemote&&shipNavigation
                    ?universe.pointRelative(shipNavigation->anchor,shipNavigation->cameraPosition()*
                        std::pow(10.,shipNavigation->logDistance-universe.describe(shipNavigation->anchor).frame.logRadius),
                        planet.address,0):planetEye;
                const double localRange = glm::length(planetEye);
                const double planetRadius = std::pow(10., planet.frame.logRadius);
                const bool local = !moon && std::min(localRange, glm::length(shipEye)) <
                                               std::max(2e7, planetRadius * 6);
                const bool pinnedTraffic = pinned && !pinnedDebris;
                std::vector<ActivityBody> bodies;
                if (!moon) {
                    if (local || pinnedTraffic)
                        bodies = sampleActivity(planet, activity->seconds);
                    else {
                        bodies.push_back(sampleActivity(planet, 0, activity->seconds));
                        bodies.push_back(sampleActivity(planet, 1, activity->seconds));
                    }
                }
                if (!moon && economy && economy->network && economy->network->system == system.address &&
                    !local && !pinnedTraffic)
                    for (const auto& f : economy->flights)
                        if (f.id.home == planet.address)
                            bodies.push_back(sampleActivity(planet, f.id.slot, activity->seconds));
                if (economy && economy->network && economy->network->system == system.address)
                    for (auto& body : bodies)
                        if (body.kind == ActivityKind::Freighter) {
                            const auto it = std::find_if(economy->flights.begin(), economy->flights.end(),
                                                         [&](const auto& f) { return f.id == body.id; });
                            if (it != economy->flights.end())
                                body = sampleEconomicFreight(universe, *economy->network, *it,
                                                             activity->seconds);
                        }
                if (npcCombat && (local || pinnedTraffic))
                    bodies.push_back(sampleRaider(universe, planet.address, activity->seconds));
                if (npcCombat && !moon && (local || pinnedTraffic)) {
                    bool richLane = false, highValueLane = false, weatherHold = false;
                    if (economy && economy->network && economy->network->system == system.address) {
                        const auto& sites = economy->network->sites;
                        const auto servesPlanet = [&](unsigned site) {
                            return site < sites.size() && sites[site].id.home == planet.address;
                        };
                        for (const auto& f : economy->flights) {
                            if (f.kg <= 0)
                                continue;
                            if (f.phase == FreightPhase::Transit &&
                                (servesPlanet(f.source) || servesPlanet(f.destination))) {
                                const auto value = f.kg * goodInfo(f.good).baseCents;
                                richLane |= value >= 10000;
                                highValueLane |= value >= 90000;
                            }
                            weatherHold |= f.phase == FreightPhase::Holding && f.weatherHold &&
                                           servesPlanet(f.destination);
                        }
                    }
                    const bool pair = mix64(planet.seed) % 3 == 0;
                    const auto add = [&](uint32_t slot, bool scheduled) {
                        const ActivityId id{planet.address, slot};
                        if (scheduled || (pinnedTraffic && *activity->selected == id) ||
                            npcCombat->agents().contains(id))
                            bodies.push_back(sampleCombatActivity(universe, id, activity->seconds));
                    };
                    add(cargoRaiderSlot, richLane && !pair);
                    add(missileWingSlot, richLane && pair);
                    add(cutoffRaiderSlot, richLane && pair);
                    add(factionHunterSlot, offence && offence->id.home == planet.address);
                    const auto localRole = mix64(planet.seed ^ 0x52414944504f4f4cULL) % 4;
                    add(skirmisherSlot, localRole <= 1);
                    add(scavengerSlot, localRole == 2);
                    add(raiderScoutSlot, localRole == 3);
                    if (npcCombat->reinforcementActive(planet.address, activity->seconds))
                        bodies.push_back(sampleCombatActivity(universe, {planet.address, pursuitRaiderSlot},
                                                             activity->seconds));
                    add(siegeRaiderSlot, highValueLane && pair);
                    add(saboteurSlot, weatherHold);
                }
                if (economy)
                    for (auto& body : bodies)
                        if (body.kind == ActivityKind::Harbour)
                            annotateEconomicPort(body, *economy);
                if (!probeRemote && (!instruments || !instruments->shrinking)) {
                    if (localRange > planetRadius * 1.03 && localRange < planetRadius * 4.5) {
                        for (auto& body : sampleOrbitalDebris(planet, activity->seconds))
                            bodies.push_back(std::move(body));
                    } else if (pinnedDebris) {
                        bodies.push_back(sampleActivity(planet, activity->selected->slot, activity->seconds));
                    }
                }
                for (auto body : bodies) {
                    const bool natural = isOrbitalDebris(body.id);
                    if (npcCombat && !natural) {
                        const auto range = glm::length(body.position - shipEye);
                        if (range < 30000 && combatBodies.size() < 512)
                            combatBodies.push_back(body);
                        npcCombat->apply(body);
                    }
                    if (weapons && !natural)
                        weapons->apply(body);
                    ++stats.activityBodies;
                    const auto deltaSI = pose.orientation * (body.position - planetEye);
                    const double distance = glm::length(deltaSI);
                    if (!natural && glm::length(body.position-shipEye) < 10000 * (instruments ? instruments->hullMetres / 12 : 1) + body.radius &&
                        weaponTargets.size() < 256) {
                        auto target = body;
                        // Position and velocity already include the wreck overlay; fire() is idempotent for
                        // disabled actors.
                        weaponTargets.push_back(target);
                    }
                    if (distance <= 1e-12)
                        continue;
                    const auto direction = cameraInverse * (deltaSI / distance);
                    const bool selected = activity->selected && *activity->selected == body.id;
                    const bool occulted = celestialOccluded(direction, distance, skyOccluders);
                    if (body.kind != ActivityKind::Debris || (natural && (!occulted || selected))) {
                        const auto velocity = cameraInverse * (pose.orientation * body.velocity);
                        auto& contacts = natural ? debrisContacts : activityContacts;
                        contacts.push_back(
                            {body, direction, distance,
                             instruments ? glm::length(velocity -
                                                       glm::inverse(instruments->gunAim * head.orientation) *
                                                           instruments->localVelocity)
                                         : glm::length(velocity),
                             selected, occulted});
                    }
                    // The outbound lane can remain in view after its harbour is behind the ship.
                    if (selected && !occulted && distance < 18000 && (!instruments || !instruments->shrinking))
                        if (const auto berth = dockingBerth(body)) {
                            const auto phase = instruments ? instruments->docking.phase : DockingPhase::None;
                            const auto lane = dockingLane(*berth, activity->seconds, phase);
                            stats.dockingGuideVertices += lane.vertices.size();
                            const auto base = uint32_t(dockingLights.vertices.size());
                            for (auto vertex : lane.vertices) {
                                const auto local = glm::dvec3(vertex.x, vertex.y, vertex.z);
                                const auto relative = pose.orientation *
                                    ((berth->metres - planetEye) + berth->orientation * local) / units;
                                vertex.x = float(relative.x);
                                vertex.y = float(relative.y);
                                vertex.z = float(relative.z);
                                dockingLights.vertices.push_back(vertex);
                            }
                            for (size_t i = 0; i < lane.indexCount(); ++i)
                                dockingLights.indices.push_back(base + lane.index(i));
                        }
                    const bool closeRock = landableDebris(body) && distance < body.radius * 2.2;
                    if ((!closeRock && direction.z >= 0) || occulted ||
                        (distance > 2e6 && body.kind != ActivityKind::Harbour && !selected))
                        continue;
                    const double angular = body.radius / distance;
                    if (!closeRock && !LOD::visible(direction * 500., std::max(.5, angular * 500.), double(width) / height,
                                      fieldOfView))
                        continue;
                    const auto point = eye + deltaSI / units;
                    const auto shell = eye + cameraQ * direction * 500.;
                    const double pixels = angular * height / std::tan(fieldOfView * .5);
                    if (body.kind != ActivityKind::Debris || (natural && (selected || pixels > 1.6)))
                        if (auto screen = project(shell))
                            activityPicks.emplace_back(body.id, *screen);
                    if (natural && pixels < .20 && !selected)
                        continue;
                    const auto role = landableDebris(body) ? debrisSurfaceRole :
                        size_t(body.kind == ActivityKind::Raider ? ActivityKind::Patrol : body.kind);
                    const unsigned detail = pixels > 48 ? 2 : pixels > 8 ? 1 : 0;
                    if ((closeRock || (pixels > 1.6 && -cameraSpace(point).z > .00005)) &&
                        distance / units < 8000) {
                        const Random shape{body.seed};
                        auto axes = natural ? glm::dvec3(1., landableDebris(body) ? 1. : .66 + .34 * shape.unit(40),
                                                                .54 + .46 * shape.unit(41))
                                                  : glm::dvec3(1.);
                        switch (raiderRole(body.id)) {
                        case RaiderRole::Skirmisher: axes = {1.20, .57, 1.30}; break;
                        case RaiderRole::Siege: axes = {1.12, 1.05, 1.35}; break;
                        case RaiderRole::Saboteur: axes = {.82, .62, 1.27}; break;
                        case RaiderRole::Scavenger: axes = {1.26, .81, 1.02}; break;
                        case RaiderRole::Scout: axes = {.81, .50, 1.18}; break;
                        case RaiderRole::Pursuit: axes = {1.18, .77, 1.41}; break;
                        default: break;
                        }
                        auto model = glm::translate(glm::dmat4(1), deltaSI / units) *
                                     glm::mat4_cast(pose.orientation * body.orientation) *
                                     glm::scale(glm::dmat4(1), axes * (body.radius / units));
                        batches[role][detail].push_back({glm::mat4(model), glm::vec4(body.tint, 1)});
                    } else {
                        const double footprint =
                            std::max(angular, 2 * std::tan(fieldOfView * .5) / height * .85);
                        addBillboard(shell, 500 * footprint,
                                     body.tint * (body.kind == ActivityKind::Debris ? .25f : .75f), true);
                    }
                    ++stats.activityVisible;
                    if (selected && landableDebris(body) && distance < 1400) {
                        const auto north = body.orientation * glm::dvec3(0, 1, 0);
                        const auto pad = debrisPad(body, glm::dot(planetEye - body.position, north) < 0 ? -1 : 1);
                        const auto right = glm::cross(pad.forward, pad.up);
                        const auto local = [&](double x, double z) {
                            return pose.orientation *
                                (pad.surface - planetEye + pad.up * .075 + right * x + pad.forward * z) / units;
                        };
                        const glm::vec3 tint(.26f, .94f, .85f);
                        for (double x : {-1., 1.})
                            for (double z : {-1., 1.}) {
                                addLine(dockingLights, local(x * 12, z * 12), local(x * 8, z * 12), tint);
                                addLine(dockingLights, local(x * 12, z * 12), local(x * 12, z * 8), tint);
                            }
                    }
                    if (distance < 150000 && body.kind != ActivityKind::Debris) {
                        // Hyperion drawFactionCraft's two engine traces, kept in
                        // actual body coordinates rather than screen particles.
                        const auto rotation = pose.orientation * body.orientation;
                        const double pulse = .65 + .08 * std::sin(activity->seconds * 7 + body.phase);
                        if (body.kind != ActivityKind::Harbour && glm::length(body.velocity) > 1)
                            for (int side : {-1, 1}) {
                                const auto engine = point + rotation * glm::dvec3(side * .44, 0, 1.16) *
                                                                (body.radius / units);
                                addLine(exhaust, engine,
                                        engine + rotation * glm::dvec3(0, 0, pulse) * (body.radius / units),
                                        body.tint * 1.3f);
                            }
                        if (body.kind == ActivityKind::Harbour) {
                            for (int side : {-1, 1}) {
                                const auto lamp =
                                    point + rotation * glm::dvec3(side * .82, .15, 0) * (body.radius / units);
                                const double radius =
                                    std::max(body.radius * .025 / units, distance / units * .00065);
                                addBillboard(lamp, radius,
                                             body.tint *
                                                 float(.5 + .5 * std::sin(activity->seconds * 2 + side)));
                            }
                            // The animated cargo hook is the harbour's equipment.
                            const double travel = .5 + .5 * std::sin(activity->seconds * .55);
                            const auto base =
                                point + rotation * glm::dvec3(.45, .64, .18) * (body.radius / units);
                            const auto hook = base + rotation * glm::dvec3(0, -.08 - .14 * travel, 0) *
                                                         (body.radius / units);
                            addLine(exhaust, base, hook, body.tint);
                        }
                    }
                }
            }
            for (size_t role = 0; role < batches.size(); role++)
                for (unsigned detail = 0; detail < 3; detail++) {
                    const auto& instances = batches[role][detail];
                    if (instances.empty())
                        continue;
                    auto& mesh = craftMeshes[role][detail];
                    if (!bgfx::isValid(mesh.vertices)) {
                        const auto source = role == debrisSurfaceRole ? landableDebrisMesh(detail) :
                                                                       activityMesh(ActivityKind(role), detail);
                        mesh = upload(source);
                        craftBytes += source.bytes();
                    }
                    const auto count = bgfx::getAvailInstanceDataBuffer(uint32_t(instances.size()), 80);
                    if (!count)
                        continue;
                    bgfx::InstanceDataBuffer buffer;
                    bgfx::allocInstanceDataBuffer(&buffer, count, 80);
                    std::memcpy(buffer.data, instances.data(), count * sizeof(Instance));
                    bgfx::setVertexBuffer(0, mesh.vertices);
                    bgfx::setIndexBuffer(mesh.indices);
                    bgfx::setInstanceDataBuffer(&buffer);
                    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_GREATER |
                                   BGFX_STATE_MSAA);
                    {
                        Geometry source;
                        const ScenePresentation presentation{
                            source, system.address, {.43, .65, .035, .01}, float(role) / 6, 1.f};
                        hyperion->bind(presentation, {}, glm::vec3(lightAt(system.address, nav)),
                                       glm::mat4(1), 0, activity->seconds);
                    }
                    bgfx::submit(62, hyperion->geometryProgram());
                    stats.activityTriangles += count * mesh.count / 3;
                }
        }
        std::sort(activityContacts.begin(), activityContacts.end(),
                  [](const auto& a, const auto& b) { return a.distance < b.distance; });
        if (activityContacts.size() > 80) {
            auto port = std::find_if(activityContacts.begin() + 80, activityContacts.end(),
                                     [](const auto& c) { return c.body.kind == ActivityKind::SurfacePort; });
            if (port != activityContacts.end())
                activityContacts[78] = *port;
            auto pinned = std::find_if(activityContacts.begin() + 80, activityContacts.end(),
                                       [](const auto& c) { return c.selected; });
            if (pinned != activityContacts.end())
                activityContacts[79] = *pinned;
            activityContacts.resize(80);
        }
        std::sort(debrisContacts.begin(), debrisContacts.end(), [](const auto& a, const auto& b) {
            return a.selected != b.selected ? a.selected : a.distance < b.distance;
        });
        if (debrisContacts.size() > orbitalDebrisCapacity)
            debrisContacts.resize(orbitalDebrisCapacity);
        for (auto& contact : debrisContacts)
            activityContacts.push_back(std::move(contact));
        transient(exhaust, worldView(), true, true);
        transient(dockingLights, 62, true, true);
        stats.activityBytes = craftBytes;
    }
    void renderNode(const Node& node, const Navigation& nav, RegionCache& cache, unsigned maximum = 4) {
        if(exteriorHidden&&node.address.kind()<=Kind::Surface)return;
        auto pose = poseFor(node.address, nav);
        auto cp = cameraSpace(pose.position);
        double r = pose.scale;
        auto kind = node.address.kind();
        // A compact remnant can subtend a large disc while its enclosing
        // system exceeds the normal cell-size guard. Cull that immutable mesh
        // using its physical photosphere, retaining the original model scale.
        const bool compactStellar =
            node.stellar && kind == Kind::System && node.address == nav.anchor && r > 1e4;
        const double bounds = compactStellar ? node.stellarRadiusMetres / std::pow(10., nav.logDistance)
                              : kind == Kind::System && r > 1e-6 && r < 1e4
                                  ? universe.systemRadiusMetres(node) / std::pow(10., nav.logDistance)
                                  : r;
        double pixels = LOD::pixels(bounds, glm::length(cp), height, 1.0);
        auto& state = details[node.address];
        bool stellarProxy =
            kind == Kind::System && (node.address.ancestorOf(nav.anchor) || state.blend.current);
        if (stellarProxy && !instruments && cp.z < 0) {
            double physical = std::pow(10.0, std::log10(node.stellarRadiusMetres) - nav.logDistance) * 2.5;
            double radius = std::clamp(physical, -cp.z * .001, -cp.z * .035);
            float intensity = static_cast<float>(.35 + .65 * std::min(node.massSolar / 3, 1.0));
            if (LOD::visible(cp, radius, static_cast<double>(width) / height, fieldOfView))
                addBillboard(pose.position, radius, stellarRadiance(node) * intensity, false,
                             3.f + hyperionPhase(node.seed) * .25f);
        } else if (node.address == nav.anchor && pixels < 1.5 && cp.z < 0 && kind != Kind::Cosmic)
            addBillboard(pose.position, -cp.z * .0015, material(node.environment.mineral).color);
        if (kind != Kind::Cosmic &&
            (r < 1e-8 || (r > 1e4 && !compactStellar) || pixels < .7 ||
             !LOD::visible(cp, bounds, static_cast<double>(width) / height, fieldOfView)))
            return;
        float opacity = node.address.ancestorOf(nav.anchor) && node.address != nav.anchor
                            ? static_cast<float>(1 - smoothstep(3, 12, r))
                            : 1.f;
        if (kind == Kind::Cosmic)
            opacity = 1;
        if (opacity < .005)
            return;
        if (kind == Kind::Mountain && node.address != nav.anchor && pixels < 160)
            return;
        if (kind == Kind::Surface && node.address != nav.anchor && pixels < 100)
            return;
        state.level = screenDetail(kind, pixels, state.initialized ? state.level : 0);
        state.level = std::min(state.level, maximum);
        state.initialized = true;
        state.used = frame;
        unsigned viewKey =
            kind == Kind::Planet ? terrainView(glm::inverse(pose.orientation) * (eye - pose.position)) : 0;
        auto candidate = cache.request(node, state.level,
                                       kind == Kind::Cosmic         ? 8
                                       : node.address == nav.anchor ? 0
                                                                    : 4,
                                       viewKey);
        const bool streamedTerrain = terrainActive && streamedTerrainCovers(node.address, nav.anchor, false);
        // Retain the original node/LOD reference for identity and inspection;
        // streamed tiles provide the terrain draw without duplicate GPU buffers.
        state.blend.update(streamedTerrain || prepareGPU(candidate) ? candidate : nullptr, frameSeconds);
        if (state.blend.current) {
            ++stats.visibleObjects;
            if (isMoon(node.address))
                ++stats.moonWorlds;
            if (!streamedTerrain)
                drawBlend(state, node, nav, opacity);
        }
    }
    void renderSection(const Node& parent, const Navigation& nav, RegionCache& cache) {
        if(exteriorHidden)return;
        if (parent.address.kind() < Kind::Planet || parent.address.kind() > Kind::Grain)
            return;
        if (terrainActive && parent.address.kind() <= Kind::Mountain)
            return;
        auto pose = poseFor(parent.address, nav);
        float opacity = static_cast<float>(smoothstep(3, 12, pose.scale));
        if (opacity < .005)
            return;
        // Quantisation only selects cached sampling windows. Every window queries
        // the same parent's density in its actual hierarchical reference frame.
        int bucket = sectionBucket(nav.logDistance);
        auto key = parent.address.str() + "|" + nav.anchor.str();
        auto& state = localSections[key];
        state.used = frame;
        auto candidate = cache.requestSection(parent, nav.anchor, bucket);
        state.blend.update(prepareGPU(candidate) ? candidate : nullptr, frameSeconds);
        if (state.blend.current) {
            drawBlend(state, parent, nav, opacity);
            ++stats.sections;
            sectionLabel = parent.label();
        }
    }
    void renderTerrain(const Node& planet, const Navigation& nav, RegionCache& cache) {
        if (!terrainActive)
            return;
        const auto transform = pointTransform(framesFor(planet.address), planet.address, anchorFrames,
                                              nav.anchor, nav.logDistance);
        const auto rotationView = glm::mat4_cast(glm::quat(cameraInverse));
        const glm::mat4 proj(projection);
        bgfx::setViewTransform(1, glm::value_ptr(rotationView), glm::value_ptr(proj));
        bgfx::setViewRect(1, 0, 0, uint16_t(width), uint16_t(height));
        const auto mapping = pointTransform(anchorFrames, nav.anchor, framesFor(planet.address),
                                            planet.address, planet.frame.logRadius);
        const double units = std::pow(10., nav.logDistance - anchorFrames.back().logRadius);
        glm::dmat4 pattern(1);
        for (unsigned i = 0; i < 3; ++i)
            pattern[i] = glm::dvec4(mapping.linear[i] * units, 0);
        pattern[3] = glm::dvec4(mapping(eye * units), 1);
        bool updateSun = false;
        if (landingShade && instruments && !instruments->shrinking && nav.anchor.kind() < Kind::Rock &&
            instruments->altitude >= 0 && instruments->altitude < instruments->hullMetres / 12. * 38.) {
            const auto pose = poseFor(planet.address, nav);
            const double radiusMetres = std::pow(10., planet.frame.logRadius);
            const auto planetEye = glm::dvec3(pattern[3]) * radiusMetres;
            const auto direction = glm::normalize(planetEye);
            const double groundHeight = universe.terrainHeight(planet, direction);
            const double altitude = glm::length(planetEye) - radiusMetres - groundHeight;
            const double size = instruments->hullMetres / 12.;
            const double shade = 1 - smoothstep(size, size * 38, std::max(0., altitude));
            if (shade > 0 && size > 0) {
                const double units = std::pow(10., nav.logDistance);
                const auto point =
                    pose.orientation * (direction * (radiusMetres + groundHeight) - planetEye) / units;
                hyperion->landingContact(
                    planet.address, glm::vec3(point), glm::vec3(pose.orientation * direction),
                    float((3.2 * size + std::max(0., altitude) * .22) / units), float(shade * .35));
            }
        }
        SunShadowFrame sun;
        if (!exteriorHidden && sunShadows && instruments && !instruments->shrinking && nav.anchor.kind() < Kind::Rock &&
            std::pow(10., nav.logDistance) > .01) {
            const auto pose = poseFor(planet.address, nav);
            const double radiusMetres = std::pow(10., planet.frame.logRadius);
            const auto planetEye = glm::dvec3(pattern[3]) * radiusMetres;
            const auto direction = glm::normalize(planetEye);
            const auto light = glm::inverse(pose.orientation) * lightAt(planet.address, nav);
            const double groundHeight = universe.terrainHeight(planet, direction);
            const double altitude = glm::length(planetEye) - radiusMetres - groundHeight;
            if (altitude < 60000 && glm::dot(direction, light) > .035) {
                // Reuse the existing single map. Its snapped, power-of-two span
                // follows approach altitude; no additional texture is allocated.
                terrainSunSpan = std::clamp(std::exp2(std::ceil(std::log2(std::max(2048., altitude * 8.)))),
                                            2048., 262144.);
                terrainSunAnchor = direction * (radiusMetres + groundHeight);
                sun = sunShadowFrame(terrainSunAnchor, light, pose.orientation, planetEye,
                                     std::pow(10., nav.logDistance), terrainSunSpan,
                                     bgfx::getCaps()->homogeneousDepth, bgfx::getCaps()->originBottomLeft);
                uint64_t key = descriptorFingerprint(planet);
                for (const auto& patch : terrain.patches())
                    key = mix64(key ^ encodeTile(patch.tile) ^ (uint64_t(patch.stitch) << 32));
                if(activity && economy)
                    if(const auto world=surfaceLife.find(planet.address))
                        for(const auto& city:world->settlements)
                            if(glm::length(city.landingPoint-planetEye)<city.radius+1200){
                                // Reuse the single shadow target; nearby working equipment
                                // receives bounded 4 Hz caster updates while the ship holds still.
                                key=mix64(key^uint64_t(activity->seconds*4));break;
                            }
                if (terrain.morphing())
                    key = mix64(key ^ frame);
                updateSun = hyperion->prepareSun(sun, key);
                terrainSunPlanet = planet.address;
            }
        }
        std::vector<Borehole> localOpenings;
        if (economy) {
            const auto metres=glm::dvec3(pattern[3])*std::pow(10.,planet.frame.logRadius);
            for(const auto& saved:economy->exploration.probes.holes)
                if(saved.planet==planet.address&&probeSeconds>=saved.impactTime()&&glm::length(saved.entry-metres)<2500){
                    auto b=saved;if(probeSeconds<b.blastTime())b.shaftRadius=1.3;localOpenings.push_back(b);
                }
        }
        for (const auto& patch : terrain.patches()) {
            auto displayed = patch.geometry;
            if (!localOpenings.empty()) {
                std::vector<Borehole> openings;
                const auto& positions = patch.geometry->precisePositions;
                if (!positions.empty()) {
                    glm::dvec3 middle{};
                    for (const auto& point : positions) middle += point/double(positions.size());
                    double span = 0;
                    for (const auto& point : positions) span = std::max(span,glm::length(point-middle));
                    for (const auto& saved : localOpenings)
                        if (glm::length(saved.entry/std::pow(10.,patch.geometry->logUnits)-middle) <
                                span+saved.shaftRadius*2/std::pow(10.,patch.geometry->logUnits)) {
                            auto b=saved;
                            openings.push_back(b);
                        }
                }
                if (!openings.empty())
                    if (auto cut = cache.requestExcavatedTerrain(patch.geometry,std::move(openings))) displayed=cut;
            }
            const auto& g = *displayed;
            if (displayed != patch.geometry) ++stats.cutTerrainTiles;
            if(exteriorHidden)continue; // Keep aperture jobs warm without drawing covered terrain.
            const auto vertexCount = uint32_t(g.solid.vertices.size());
            if (!vertexCount || !g.solid.indexCount()) continue;
            std::array<glm::dvec3,81> ordinaryPoints;
            std::vector<glm::dvec3> extraPoints(vertexCount==81?0:vertexCount);
            std::span<glm::dvec3> points=vertexCount==81?std::span<glm::dvec3>(ordinaryPoints)
                                                      :std::span<glm::dvec3>(extraPoints);
            glm::dvec3 centre{};
            for (unsigned i = 0; i < points.size(); ++i) {
                const auto position = displayed == patch.geometry ? terrain.position(patch,i) : g.precisePositions[i];
                points[i] = transform.linear * (position - transform.pivot) +
                            (transform.translation - eye);
                centre += points[i] / double(points.size());
            }
            double radius = 0;
            for (auto p : points)
                radius = std::max(radius, glm::length(p - centre));
            const bool visible =
                LOD::visible(cameraInverse * centre, radius, double(width) / height, fieldOfView);
            // Caster visibility is evaluated in the light frame, independently
            // of the cockpit view. Reuse exactly the current geomorphed vertices.
            bool caster = false;
            if (updateSun) {
                const auto q = glm::dvec3(sun.textureFromWorld * glm::dvec4(centre, 1));
                const double r = radius * std::pow(10., nav.logDistance) / sun.spanMetres;
                caster =
                    q.x + r > 0 && q.y + r > 0 && q.z + r > 0 && q.x - r < 1 && q.y - r < 1 && q.z - r < 1;
            }
            if (!visible && !caster)
                continue;
            const bool smoothTerrain = g.terrainNormals.size() == vertexCount;
            const uint32_t count = uint32_t(g.solid.indexCount());
            if ((smoothTerrain && bgfx::getAvailTransientVertexBuffer(vertexCount, normalLayout) < vertexCount) ||
                bgfx::getAvailTransientVertexBuffer(vertexCount, layout) < vertexCount ||
                bgfx::getAvailTransientIndexBuffer(count, g.solid.wideIndices()) < count ||
                bgfx::getAvailInstanceDataBuffer(1, 80) < 1) {
                ++stats.uploadDeferred;
                if (caster)
                    terrainSunComplete = false;
                continue;
            }
            bgfx::TransientVertexBuffer vertices, normals;
            bgfx::TransientIndexBuffer indices;
            bgfx::InstanceDataBuffer instances;
            bgfx::allocTransientVertexBuffer(&vertices, vertexCount, layout);
            bgfx::allocTransientIndexBuffer(&indices, count, g.solid.wideIndices());
            bgfx::allocInstanceDataBuffer(&instances, 1, 80);
            auto data = reinterpret_cast<Vertex*>(vertices.data);
            for (unsigned i = 0; i < points.size(); ++i)
                data[i] = {float(points[i].x), float(points[i].y), float(points[i].z),
                           smoothTerrain
                               ? (g.exposureColors.size() == vertexCount ? g.exposureColors[i] : g.terrainAlbedo[i])
                               : g.solid.vertices[i].color};
            if (smoothTerrain) {
                bgfx::allocTransientVertexBuffer(&normals, vertexCount, normalLayout);
                auto output = reinterpret_cast<ShadingNormal*>(normals.data);
                for (unsigned i = 0; i < vertexCount; ++i)
                    output[i] = packNormal(glm::vec3(
                        glm::normalize(glm::dmat3(transform.linear) * glm::dvec3(g.terrainNormals[i]))));
            }
            std::memcpy(indices.data, g.solid.indexData(), count * g.solid.indexWidth());
            const Instance identity{glm::mat4(1), glm::vec4(1)};
            std::memcpy(instances.data, &identity, sizeof(identity));
            if (caster) {
                bgfx::setVertexBuffer(0, &vertices);
                bgfx::setIndexBuffer(&indices);
                bgfx::setInstanceDataBuffer(&instances);
                bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
                bgfx::submit(70, hyperion->sunProgram());
            }
            stats.uploadBytes +=
                vertexCount * sizeof(Vertex) + count * g.solid.indexWidth() +
                (smoothTerrain ? vertexCount * sizeof(ShadingNormal) : 0);
            if (!visible)
                continue;
            auto presentation = extractPresentation(planet, g, mode);
            const auto light = lightAt(planet.address, nav);
            glm::vec4 cavity{};
            if (nav.anchor.kind() >= Kind::Rock)
                cavity = glm::vec4(glm::vec3(-eye), float(std::max(1.3, glm::length(eye) * 1.1)));
            glm::mat4 micro(0), meso(0);
            float relief = 0, mesoRelief = 0;
            {
                const double radiusMetres = std::pow(10., planet.frame.logRadius);
                micro = periodicMaterialFrame(pattern, radiusMetres, 8, 64);
                meso = periodicNoiseFrame(pattern, radiusMetres, 256, 4096);
                relief = float(.36 / std::pow(10., nav.logDistance));
                mesoRelief = float(8. / std::pow(10., nav.logDistance));
            }
            hyperion->bind(presentation, {}, glm::vec3(light), glm::mat4(pattern),
                           smoothTerrain ? -2.f : -1.f, visualTime, cavity, micro, relief, meso, mesoRelief);
            bgfx::setVertexBuffer(0, &vertices);
            if (smoothTerrain)
                bgfx::setVertexBuffer(1, &normals);
            bgfx::setIndexBuffer(&indices);
            bgfx::setInstanceDataBuffer(&instances);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                           BGFX_STATE_DEPTH_TEST_GREATER | BGFX_STATE_MSAA);
            bgfx::submit(1, terrainCoverageDebug ? hyperion->coverageProgram()
                                                 : hyperion->geometryProgram(smoothTerrain));
            ++stats.terrainTiles;
            stats.vertices += vertexCount;
            stats.triangles += count / 3;
        }
        if (updateSun)
            hyperion->completeSun(terrainSunComplete);
        if (terrain.morphing())
            ++stats.lodTransitions;
    }
    bool enclosedCavernView(const Navigation& nav,RegionCache& cache) {
        enclosedBore=0;
        if(!probeOptimisation||!economy||specimen||nav.anchor.kind()>Kind::Surface)return false;
        if(!probeRemote&&(!instruments||instruments->shrinking))return false;
        if(nav.selected&&nav.selected->kind()>=Kind::Rock)return false;
        const double units=std::pow(10.,nav.logDistance);
        for(const auto& b:economy->exploration.probes.holes){
            if(probeSeconds<b.impactTime())continue;
            if(economy->network&&b.planet.parent()!=economy->network->system)continue;
            const auto planet=universe.describe(b.planet);const auto pose=poseFor(b.planet,nav);
            const auto planetEye=glm::inverse(pose.orientation)*(eye-pose.position)*units;
            if(glm::length(planetEye-b.entry)>b.finalDepth()+b.roomRadius()+40)continue;
            const bool complete=probeSeconds>=b.blastTime();
            const auto geometry=cache.requestBore(planet,b,complete,
                complete?b.finalDepth():probeOperation(b,probeSeconds).reached);
            if(!geometry||geometry->solid.vertices.size()<boreSectors||!prepareGPU(geometry))continue;
            double radius=b.shaftRadius;
            for(unsigned k=0;k<boreSectors;++k){const auto& v=geometry->solid.vertices[k];
                radius=std::max(radius,glm::length(glm::dvec3(v.x,v.y,v.z)));}
            if(boreEnclosesView(b,probeSeconds,planetEye,glm::inverse(pose.orientation)*(cameraQ*glm::dvec3(0,0,-1)),
                                fieldOfView,double(width)/height,radius+.5,worldNearPlane*units)){
                enclosedBore=b.id;return true;
            }
        }
        return false;
    }
    void renderBores(const Navigation& nav, RegionCache& cache) {
        if (!economy) return;
        const auto rotation = glm::mat4_cast(glm::quat(cameraInverse));
        const glm::mat4 projectionMatrix(projection);
        bgfx::setViewTransform(1,glm::value_ptr(rotation),glm::value_ptr(projectionMatrix));
        bgfx::setViewRect(1,0,0,uint16_t(width),uint16_t(height));
        std::array<std::pair<const Borehole*,double>,maxBoreholes> ranked{};
        size_t count=0;
        for(const auto& b:economy->exploration.probes.holes){
            if(economy->network&&b.planet.parent()!=economy->network->system)continue;
            const auto planet=universe.describe(b.planet);
            const auto point=universe.pointRelative(b.planet,b.entry/std::pow(10.,planet.frame.logRadius),nav.anchor,nav.logDistance);
            double distance=glm::length(point-eye)*std::pow(10.,nav.logDistance);
            if(probeSeconds<b.impactTime()){
                const auto sample=probePose(b,probeSeconds);
                const auto head=universe.pointRelative(b.planet,sample.position/std::pow(10.,planet.frame.logRadius),nav.anchor,nav.logDistance);
                distance=std::min(distance,glm::length(head-eye)*std::pow(10.,nav.logDistance));
            }
            // The enclosing mesh must survive the eight-cave draw limit,
            // even if many other entrances are nearer than its surface rim.
            if(b.id==enclosedBore)distance=-1;
            if(distance<2500&&count<ranked.size())ranked[count++]={&b,distance};
        }
        std::sort(ranked.begin(),ranked.begin()+count,[](const auto& a,const auto& b){return a.second<b.second;});
        unsigned visible = 0;
        for (size_t index=0;index<std::min(size_t(8),count);++index) {
            const auto& b=*ranked[index].first;
            if (economy->network && b.planet.parent()!=economy->network->system) continue;
            const auto planet = universe.describe(b.planet);
            const auto pose = poseFor(b.planet,nav);
            const auto point = universe.pointRelative(b.planet,b.entry/std::pow(10.,planet.frame.logRadius),
                                                       nav.anchor,nav.logDistance);
            const double scale = std::pow(10.,-nav.logDistance);
            if (ranked[index].second > 2500 || visible >= 8) continue;
            ++visible;
            const auto state=probePose(b,probeSeconds);
            auto pre = cache.requestBore(planet,b,false,probeOperation(b,probeSeconds).reached);
            auto open = cache.requestBore(planet,b,true);
            const auto visual=probeVisual(b,probeSeconds,material(planet.environment.mineral).density);
            const auto q = pose.orientation*tangentFrame(glm::normalize(b.entry));
            const auto* feed=economy->network?latestProbe(economy->exploration.probes,economy->network->system):nullptr;
            if(probeRemote&&feed&&feed->id==b.id){
                if(!bgfx::isValid(probeHousing.vertices)){
                    const auto rig=probeRig();
                    probeHousing=upload(rig.housing);probeCutter=upload(rig.cutter);probeLamps=upload(rig.lamps);
                    probeRoller=upload(rig.roller);probeDrive=upload(rig.drive);probeActuator=upload(rig.actuator);probeHotTeeth=upload(rig.hotTeeth);
                    auto polished=rig.actuator;for(auto& v:polished.vertices)v.color=rgba({.46f,.56f,.64f});
                    probeRod=upload(polished);
                    probeRigBytes=rig.housing.bytes()+rig.cutter.bytes()+rig.lamps.bytes()+rig.roller.bytes()+rig.drive.bytes()+rig.actuator.bytes()+rig.hotTeeth.bytes()+polished.bytes();
                }
                const auto lens=universe.pointRelative(b.planet,state.position/std::pow(10.,planet.frame.logRadius),nav.anchor,nav.logDistance);
                const auto base=glm::translate(glm::mat4(1),glm::vec3(lens-eye))*
                    glm::mat4_cast(glm::quat(pose.orientation*state.attitude))*glm::scale(glm::mat4(1),glm::vec3(float(scale)));
                auto part=[&](const GPUMesh& mesh,const glm::mat4& model,bool lamp,glm::vec3 tint=glm::vec3(1),bool polished=false){
                    if(!bgfx::isValid(mesh.vertices))return;
                    if(!lamp&&!polished){
                        // Large fixed-finish plates retain authored face shading.
                        // Only rollers need the more expensive moving specular
                        // response; both paths share the Hyperion HDR display.
                        bgfx::setTransform(glm::value_ptr(model));bgfx::setVertexBuffer(0,mesh.vertices);bgfx::setIndexBuffer(mesh.indices);
                        bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_GREATER|BGFX_STATE_MSAA);
                        bgfx::submit(1,colorProgram);stats.probeRigTriangles+=mesh.count/3;return;
                    }
                    if(!bgfx::getAvailInstanceDataBuffer(1,80))return;
                    bgfx::InstanceDataBuffer instance;bgfx::allocInstanceDataBuffer(&instance,1,80);
                    const Instance item{model,glm::vec4(tint,1)};std::memcpy(instance.data,&item,sizeof(item));
                    Geometry dummy;
                    const ScenePresentation metal{dummy,b.planet,{.48f,.58f,0,0},hyperionPhase(b.id),1,{3,0,0,0}};
                    hyperion->bind(metal,{},glm::vec3(cameraQ*glm::dvec3(-.3,.5,1)),glm::mat4(1),lamp?2.f:0.f,probeSeconds);
                    hyperion->localLamp();bgfx::setVertexBuffer(0,mesh.vertices);bgfx::setIndexBuffer(mesh.indices);
                    bgfx::setInstanceDataBuffer(&instance);
                    bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_GREATER|BGFX_STATE_MSAA);
                    bgfx::submit(1,hyperion->geometryProgram());stats.probeRigTriangles+=mesh.count/3;
                };
                part(probeHousing,base,false);part(probeLamps,base,true);
                const auto mechanism=probeMechanism(visual);
                part(probeCutter,base*glm::mat4(mechanism.head),false);
                part(probeDrive,base*glm::mat4(mechanism.drive),false);
                if(visual.heat>.02)part(probeHotTeeth,base*glm::mat4(mechanism.head),true,glm::vec3(float(visual.heat)));
                for(const auto& roller:mechanism.rollers)part(probeRoller,base*glm::mat4(roller),false,{1,1,1},true);
                for(unsigned piston=0;piston<2;++piston){
                    part(probeActuator,base*glm::mat4(mechanism.barrels[piston]),false,{.42f,.51f,.62f});
                    part(probeRod,base*glm::mat4(mechanism.rods[piston]),false);
                }
            }
            if(!probeRemote&&(state.phase==ProbePhase::Flight||state.phase==ProbePhase::Beacon)){
                const auto metres=state.phase==ProbePhase::Flight?state.position+state.forward*2.4:state.position;
                const auto head=universe.pointRelative(b.planet,metres/std::pow(10.,planet.frame.logRadius),nav.anchor,nav.logDistance);
                const auto forward=pose.orientation*state.forward;
                if(glm::length(head-eye)/scale>2){
                    Mesh pod;
                    appendSphere(pod,head-eye,.24*scale,{.35f,.65f,.82f},5,8);
                    appendSphere(pod,head-eye-forward*(.55*scale),.18*scale,{.22f,.32f,.48f},4,8);
                    transient(pod,1,false,false,true,true);
                    addBillboard(head,.16*scale,{.35f,.85f,1.4f});
                    if(state.phase==ProbePhase::Flight)
                        for(unsigned k=0;k<12;++k){const double length=std::min(double(k+1),state.progress*glm::length(b.entry-b.launch));
                            addBillboard(head-forward*(length*scale),(.12+k*.012)*scale,
                                glm::vec3(.3f,.65f,1.2f)*float(std::exp(-k*.35)));}
                }
            }
            if (probeSeconds < b.impactTime()) continue;
            const bool complete = probeSeconds >= b.blastTime();
            const auto geometry = complete ? open : pre;
            if (!geometry || !prepareGPU(geometry)) continue;
            auto& uploaded = gpu.at(geometry.get()); uploaded.lastUsed = frame;
            const auto progress = complete ? 1. : std::max(.01,state.progress);
            const auto model = glm::translate(glm::mat4(1),glm::vec3(point-eye))*
                glm::mat4_cast(glm::quat(q))*glm::scale(glm::mat4(1),glm::vec3(float(scale)));
            // Keep the work-light stream in the geometry's existing GPU budget.
            // A stationary beacon reuses it; real camera/light changes update it.
            const auto localEye=glm::inverse(q)*(eye-point)/scale;
            const auto localForward=probeRemote&&feed&&feed->id==b.id
                ?glm::inverse(tangentFrame(glm::normalize(b.entry)))*state.forward
                :glm::inverse(q)*(cameraQ*glm::dvec3(0,0,-1));
            const auto nv=uint32_t(geometry->solid.vertices.size());
            const auto wear=cavernWear(universe,b,economy->exploration);
            auto lightingBore=b;lightingBore.depth=b.finalDepth()*progress;lightingBore.chamberRadius=b.roomRadius();
            lightingBore.controls.clear();
            if(probeOptimisation){
                const auto lighting=probeLighting(lightingBore,visual,localEye,localForward,complete);
                if(!bgfx::isValid(uploaded.probeLight)){
                    uploaded.probeLight=bgfx::createDynamicVertexBuffer(nv,layout);
                    if(!bgfx::isValid(uploaded.probeLight))throw std::runtime_error("Probe work-light allocation failed");
                    // The generic upload's vertex buffer is no longer needed:
                    // replace it, instead of retaining a second GPU copy.
                    if(bgfx::isValid(uploaded.solid.vertices))bgfx::destroy(uploaded.solid.vertices);
                    uploaded.solid.vertices=BGFX_INVALID_HANDLE;
                }
                if(!uploaded.probeLightKey||!(*uploaded.probeLightKey==lighting)||uploaded.miningWearKey!=wear.key){
                    probeLightScratch.resize(nv);
                    for(unsigned n=0;n<nv;++n){const auto& v=geometry->solid.vertices[n];
                        const double y=complete?double(v.y):std::max(double(v.y),-lightingBore.depth);
                        const auto albedo=glm::vec3(v.color&255,(v.color>>8)&255,(v.color>>16)&255)/255.f;
                        const auto surface=complete&&b.geologyVersion?wear.shade({v.x,y,v.z},albedo):albedo;
                        probeLightScratch[n]={v.x,float(y),v.z,rgba(lighting.shade({v.x,y,v.z},surface))};
                    }
                    bgfx::update(uploaded.probeLight,0,bgfx::copy(probeLightScratch.data(),nv*sizeof(Vertex)));
                    uploaded.probeLightKey=lighting;uploaded.miningWearKey=wear.key;stats.uploadBytes+=nv*sizeof(Vertex);++stats.probeLightUploads;
                }
                bgfx::setVertexBuffer(0,uploaded.probeLight);
            }else{
                if(bgfx::getAvailTransientVertexBuffer(nv,layout)<nv)continue;
                bgfx::TransientVertexBuffer lit;bgfx::allocTransientVertexBuffer(&lit,nv,layout);
                auto vertices=reinterpret_cast<Vertex*>(lit.data);
                for(unsigned n=0;n<nv;++n){const auto& v=geometry->solid.vertices[n];
                    const double y=complete?double(v.y):std::max(double(v.y),-lightingBore.depth);
                    const auto albedo=glm::vec3(v.color&255,(v.color>>8)&255,(v.color>>16)&255)/255.f;
                    const auto surface=complete&&b.geologyVersion?wear.shade({v.x,y,v.z},albedo):albedo;
                    vertices[n]={v.x,float(y),v.z,rgba(probeWallLight(lightingBore,visual,{v.x,y,v.z},localEye,localForward,surface,complete))};
                }
                bgfx::setVertexBuffer(0,&lit);stats.uploadBytes+=nv*sizeof(Vertex);++stats.probeLightUploads;
            }
            bgfx::setTransform(glm::value_ptr(model));bgfx::setIndexBuffer(uploaded.solid.indices);
            bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_GREATER|BGFX_STATE_MSAA);
            bgfx::submit(1,colorProgram);stats.probeLitVertices+=nv;
            stats.vertices += geometry->solid.vertices.size(); stats.triangles += geometry->solid.indexCount()/3;
            ++stats.cavernMeshes;++stats.visibleObjects;stats.cavernTriangles+=geometry->solid.indexCount()/3;
            if(complete&&b.geologyVersion){
                if(wear.minedKg)stats.miningWearVertices+=nv;
                auto tailings=cavernTailings(b,wear);stats.miningTailings+=tailings.vertices.size()/4;
                const auto lighting=probeLighting(lightingBore,visual,localEye,localForward,true);
                for(auto& v:tailings.vertices){const auto albedo=glm::vec3(v.color&255,(v.color>>8)&255,(v.color>>16)&255)/255.f;
                    v.color=rgba(lighting.shade({v.x,v.y,v.z},albedo));const auto p=point-eye+q*glm::dvec3(v.x,v.y,v.z)*scale;v.x=float(p.x);v.y=float(p.y);v.z=float(p.z);}
                bgfx::setTransform(glm::value_ptr(glm::mat4(1)));transient(tailings,1,false,false,true,true);
            }
            if (visual.flash > .001) {
                const auto centre=point-pose.orientation*glm::normalize(b.entry)*(b.finalDepth()*scale);
                addBillboard(centre,(.3+3*std::max(0.,visual.blastAge))*scale,glm::vec3(3,1.1,.2)*float(visual.flash));
            }
            stats.probeFlash=std::max(stats.probeFlash,visual.flash);
            auto motes=probeMotes(b,visual,probeSeconds,material(planet.environment.mineral).color);
            if (complete) {
                const auto fault = iceFault(lightingBore);
                if (fault && glm::length(localEye - fault->face.local) < lightingBore.chamberRadius * 2 + 4) {
                    const bool active = iceStress.bore == b.id;
                    const bool falling = active && iceStress.falling();
                    const double strain = active ? iceStress.load : 0.;
                    Mesh cracks;
                    cracks.vertices.reserve(144);
                    cracks.indices.reserve(288);
                    const glm::dvec3 roomCentre(0, -lightingBore.depth, 0);
                    const float opening = falling ? 1.f : float(strain);
                    const auto rim = glm::mix(glm::vec3(.20f, .39f, .48f), glm::vec3(.72f, .91f, 1.f), opening);
                    const auto shadow = rgba({.035f, .075f, .11f});
                    for (const auto& crack : iceCracks(lightingBore, *fault)) {
                        if (!falling && strain < crack.threshold)
                            continue;
                        const auto radial = glm::normalize((crack.from + crack.to) * .5 - roomCentre);
                        const auto across = glm::normalize(glm::cross(radial, crack.to - crack.from)) * (.014 + .042 * opening);
                        const auto first = uint32_t(cracks.vertices.size());
                        for (const auto end : {crack.from, crack.to}) {
                            for (int side = -1; side <= 1; ++side) {
                                const auto p = point - eye + q * (end + across * double(side)) * scale;
                                const auto colour = side == 0 ? shadow : rgba(rim * (side < 0 ? .6f : 1.f));
                                cracks.vertices.push_back({float(p.x), float(p.y), float(p.z), colour});
                            }
                        }
                        cracks.indices.insert(cracks.indices.end(), {first, first+3, first+1, first+1, first+3, first+4,
                                                                     first+1, first+4, first+2, first+2, first+4, first+5});
                    }
                    bgfx::setTransform(glm::value_ptr(glm::mat4(1)));
                    transient(cracks, 1, false, false);
                    if (falling) {
                        const auto chips = iceFallMotes(lightingBore, *fault, iceStress.fallAge);
                        for (size_t i = 0; i < chips.count && motes.count < motes.items.size(); ++i)
                            motes.items[motes.count++] = chips.items[i];
                    }
                }
            }
            if(complete&&b.geologyVersion&&weapons){
                for(const auto& shot:weapons->shots())if(shot.utility&&!shot.enemy&&shot.age<.12&&shot.reference==b.planet){
                    const auto local=glm::inverse(tangentFrame(glm::normalize(b.entry)))*(shot.to-b.entry);
                    const auto faces=cavernFinds(b);const CavernFind* closest=nullptr;double distance=4;
                    for(const auto& f:faces){const auto d=glm::length(local-(f.local+f.inward*1.6));if(d<distance){closest=&f;distance=d;}}
                    if(closest){const auto cutting=cavernMiningMotes(b,*closest,probeSeconds);stats.miningParticles+=cutting.count;
                        for(size_t n=0;n<cutting.count&&motes.count<motes.items.size();++n)motes.items[motes.count++]=cutting.items[n];break;}
                }
            }
            Mesh localFragments,localDust,localSparks;
            auto& fragments=probeOptimisation?probeFragments:localFragments;
            auto& dust=probeOptimisation?probeDust:localDust;
            auto& sparks=probeOptimisation?probeSparks:localSparks;
            for(auto* mesh:{&fragments,&dust,&sparks}){mesh->vertices.clear();mesh->indices.clear();}
            if(probeOptimisation){
                fragments.vertices.reserve(128);fragments.indices.reserve(384);
                dust.vertices.reserve(960);dust.indices.reserve(2304);
                sparks.vertices.reserve(24);sparks.indices.reserve(24);
            }
            static const auto circle=[] {std::array<glm::dvec3,9> points{};
                for(unsigned k=0;k<points.size();++k){const double a=k*6.283185307179586/8;points[k]={std::cos(a),std::sin(a),0};}return points;}();
            for(size_t k=0;k<motes.count;++k){const auto& mote=motes.items[k];
                const auto centre=point-eye+q*mote.position*scale;
                if(mote.spark){
                    addBillboard(centre+eye,mote.radius*scale,mote.colour*float(mote.opacity));
                    const auto tail=centre+q*glm::dvec3(-mote.position.z*.035,-.10,mote.position.x*.035)*scale;
                    addLine(sparks,centre,tail,mote.colour*float(mote.opacity*.7));
                }else if(mote.fragment){
                    const auto size=mote.radius*scale;const auto start=uint32_t(fragments.vertices.size());
                    const auto colour=mote.colour*float(.22+visual.flash*.8);
                    for(const auto corner:{glm::dvec3(1,0,0),glm::dvec3(-.5,.8,0),glm::dvec3(-.5,-.5,.7),glm::dvec3(0,0,-.7)}){
                        const auto p=centre+q*(corner*mote.stretch)*size;fragments.vertices.push_back({float(p.x),float(p.y),float(p.z),rgba(colour,float(mote.opacity))});}
                    fragments.indices.insert(fragments.indices.end(),{start,start+1,start+2,start,start+3,start+1,start,start+2,start+3,start+1,start+3,start+2});
                }else{
                    const auto start=uint32_t(dust.vertices.size());
                    const auto tint=glm::vec3(.12f,.16f,.19f)+mote.colour*.12f+glm::vec3(1,.4,.1)*float(visual.flash*.4);
                    dust.vertices.push_back({float(centre.x),float(centre.y),float(centre.z),rgba(tint,mote.opacity)});
                    for(unsigned side=0;side<9;++side){
                        const auto p=centre+cameraQ*circle[side]*(mote.radius*scale);
                        dust.vertices.push_back({float(p.x),float(p.y),float(p.z),rgba(tint,0)});
                        if(side)dust.indices.insert(dust.indices.end(),{start,start+side,start+side+1});}
                    ++stats.probeDustParticles;
                }
            }
            transient(fragments,1,false,false,true,true,true);
            transient(sparks,1,true,true,true);
            bgfx::setViewTransform(64,glm::value_ptr(rotation),glm::value_ptr(projectionMatrix));
            bgfx::setViewRect(64,0,0,uint16_t(width),uint16_t(height));
            // Soft alpha dust is drawn after opaque geometry, with depth
            // testing retained so the cloud cannot shine through solid rock.
            transient(dust,64,false,false,true,false,true);
        }
    }
    void prune() {
        std::erase_if(details, [&](const auto& state) { return frame > state.second.used + 2; });
        std::erase_if(localSections, [&](const auto& state) { return frame > state.second.used + 2; });
        while (true) {
            auto victim = gpu.end();
            for (auto it = gpu.begin(); it != gpu.end(); ++it)
                if (it->second.source.expired() || frame > it->second.lastUsed + 120 ||
                    gpuBytes > 32 * 1024 * 1024) {
                    if (victim == gpu.end() || it->second.lastUsed < victim->second.lastUsed)
                        victim = it;
                }
            if (victim == gpu.end())
                break;
            gpuBytes -= victim->second.bytes;
            destroy(victim->second.solid);
            destroy(victim->second.lines);
            if(bgfx::isValid(victim->second.probeLight))bgfx::destroy(victim->second.probeLight);
            if (victim->second.normalWork)
                victim->second.normalWork->cancel.store(true);
            if (bgfx::isValid(victim->second.normals))
                bgfx::destroy(victim->second.normals);
            if (bgfx::isValid(victim->second.instances))
                bgfx::destroy(victim->second.instances);
            gpu.erase(victim);
        }
    }
};
Renderer::Renderer(SDL_Window* window, const Universe& universe, bool benchmark)
    : impl_(std::make_unique<Impl>(window, universe)) {
    auto& p = *impl_;
    bgfx::Init init;
    init.callback = &p.callbacks;
    init.fallback = false;
    auto props = SDL_GetWindowProperties(window);
#if defined(__APPLE__)
    init.type = bgfx::RendererType::Metal;
    init.swapChain.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#elif defined(_WIN32)
    init.type = bgfx::RendererType::Direct3D11;
    init.swapChain.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    init.type = bgfx::RendererType::Vulkan;
    init.swapChain.nwh = reinterpret_cast<void*>(
        static_cast<uintptr_t>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)));
    init.swapChain.ndt = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    if (!init.swapChain.nwh)
        throw std::runtime_error("This build uses X11. Start with SDL_VIDEODRIVER=x11 on Wayland.");
#endif
    SDL_GetWindowSizeInPixels(window, &p.width, &p.height);
    init.swapChain.width = p.width;
    init.swapChain.height = p.height;
    init.swapChain.flags = BGFX_SWAP_CHAIN_MSAA_X4;
    // Reversed-Z needs floating-point storage in Original mode too. A fixed
    // 24-bit default swap-chain depth buffer would retain the far-world bug.
    init.swapChain.formatDepthStencil = bgfx::TextureFormat::D32F;
    p.resetFlags = BGFX_RESET_MAXANISOTROPY | (benchmark ? 0 : BGFX_RESET_VSYNC);
    init.reset = p.resetFlags;
    init.limits.maxTransientVbSize = 8 * 1024 * 1024;
    init.limits.maxTransientIbSize = 3 * 1024 * 1024;
    if (!init.swapChain.nwh)
        throw std::runtime_error("SDL did not provide a native window handle");
    if (!bgfx::init(init))
        throw std::runtime_error("bgfx could not initialize a graphics device");
    // Shadow casters are submitted during extraction but execute before colour
    // draws. Materials sample this frame's map using their actual positions.
    std::array<bgfx::ViewId, 84> order{};
    order[0] = 70;
    size_t next = 1;
    for (bgfx::ViewId id = 0; id < order.size(); id++)
        if (id != 70)
            order[next++] = id;
    bgfx::setViewOrder(0, uint16_t(order.size()), order.data());

    p.initialized = true;
    p.swapChain = init.swapChain;
    p.layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    p.normalLayout.begin().add(bgfx::Attrib::Normal, 2, bgfx::AttribType::Int16, true).end();
    p.colorProgram = bgfx::createProgram(
        bgfx::createEmbeddedShader(embedded, bgfx::getRendererType(), "vs_debugdraw_lines"),
        bgfx::createEmbeddedShader(embedded, bgfx::getRendererType(), "fs_debugdraw_lines"), true);
    if (!bgfx::isValid(p.colorProgram))
        throw std::runtime_error("bgfx shader program creation failed");
    for (unsigned i = 0; i < 4; ++i) {
        Mesh sphere;
        appendSphere(sphere, {}, 1, {1, 1, 1},
                     i == 3   ? 24
                     : i == 2 ? 12
                     : i == 1 ? 8
                              : 4,
                     i == 3   ? 40
                     : i == 2 ? 20
                     : i == 1 ? 12
                              : 6);
        p.atomSpheres[i] = p.upload(sphere);
    }
    Mesh quad;
    quad.vertices = {{0, 0, 0, rgba({1, 1, 1})},
                     {-1, -1, 0, rgba({0, 0, 0})},
                     {1, -1, 0, rgba({0, 0, 0})},
                     {1, 1, 0, rgba({0, 0, 0})},
                     {-1, 1, 0, rgba({0, 0, 0})}};
    quad.indices = {0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 1};
    for (auto& vertex : quad.vertices)
        vertex.color = rgba({1, 1, 1});
    p.hyperionBillboardMesh = p.upload(quad);
    for (size_t i = 1; i < quad.vertices.size(); ++i) {
        quad.vertices[i].x *= float(pointLightSupport);
        quad.vertices[i].y *= float(pointLightSupport);
        quad.vertices[i].color = rgba({0, 0, 0});
    }
    p.pointLightBillboardMesh = p.upload(quad);
    p.hyperion = std::make_unique<HyperionRenderer>();
    p.instanceLayout.begin();
    for (unsigned i = 0; i < 5; ++i)
        p.instanceLayout.add(static_cast<bgfx::Attrib::Enum>(bgfx::Attrib::TexCoord0 + i), 4,
                             bgfx::AttribType::Float);
    p.instanceLayout.end();
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, UINT32_C(0x03060cff), 0.0f, uint8_t(0));
    bgfx::setViewMode(0, bgfx::ViewMode::Default);
    bgfx::setViewMode(1, bgfx::ViewMode::Sequential);
    bgfx::setDebug(BGFX_DEBUG_TEXT);
    p.billboards.reserve(40000);
}
Renderer::~Renderer() = default;
void Renderer::setMode(RenderMode mode) {
    if (mode != RenderMode::Hyperion)
        throw std::invalid_argument("Only Hyperion rendering is available");
}
RenderMode Renderer::mode() const {
    return impl_->mode;
}
void Renderer::freezeVisualTime(double seconds) {
    impl_->fixedVisualTime = seconds;
}
void Renderer::setCockpit(const FlightInstruments* instruments) {
    if (instruments)
        impl_->instruments = *instruments;
    else {
        impl_->instruments.reset();
        impl_->cockpitMotion.reset();
        impl_->head = {};
    }
}
void Renderer::setCockpitValidation(bool enabled) {
    impl_->validateCockpit = enabled;
}
void Renderer::setCockpitMotionEnabled(bool enabled) {
    impl_->cockpitMotionEnabled = enabled;
    if (!enabled) {
        impl_->cockpitMotion.reset();
        impl_->head = {};
    }
}
bool Renderer::cockpitMotionEnabled() const {
    return impl_->cockpitMotionEnabled;
}
CockpitMotionPose Renderer::cockpitMotionPose() const {
    return impl_->head;
}
void Renderer::cockpitImpulse(CockpitImpulse type, double strength, glm::dvec3 direction) {
    if (impl_->cockpitMotionEnabled && impl_->instruments)
        impl_->cockpitMotion.impulse(type, strength, direction);
}
void Renderer::toggleCockpitHelp() {
    impl_->cockpitHelp = !impl_->cockpitHelp;
}
std::shared_ptr<const Geometry> Renderer::galaxySamples() const {
    return impl_->skyGeometry;
}
std::span<const CelestialContact> Renderer::celestialContacts() const {
    return impl_->contacts;
}
std::span<const ActivityContact> Renderer::activityContacts() const {
    return impl_->activityContacts;
}
std::span<const ProspectSite> Renderer::prospects() const {
    return impl_->prospectSites ? std::span<const ProspectSite>(*impl_->prospectSites)
                                : std::span<const ProspectSite>{};
}
void Renderer::setSpecimen(std::optional<Address> source) {
    impl_->specimen = std::move(source);
}
void Renderer::setProbeView(bool remote, double seconds) {
    impl_->probeRemote = remote;
    impl_->probeSeconds = seconds;
}
void Renderer::setIceStress(const IceStress& stress) { impl_->iceStress = stress; }
void Renderer::setProbeOptimisation(bool enabled) { impl_->probeOptimisation=enabled; }
void Renderer::setShipNavigation(const Navigation* nav) { impl_->shipNavigation=nav; }
std::shared_ptr<const SurfaceWorld> Renderer::surfaceWorld(const Address& planet) const {
    return impl_->surfaceLife.find(planet);
}
uint64_t Renderer::terrainGeometryDigest() const {
    uint64_t digest = 0;
    for (const auto& patch : impl_->terrain.patches())
        digest = mix64(digest ^ encodeTile(patch.tile) ^ patch.stitch ^ geometryFingerprint(*patch.geometry));
    return digest;
}
CockpitViewport Renderer::cockpitViewport() const {
    return impl_->hudViewport;
}
void Renderer::setEconomy(std::shared_ptr<const EconomySnapshot> economy, const MarketView* market) {
    impl_->economy = std::move(economy);
    impl_->market = market;
}
void Renderer::setNpcCombat(const NpcCombat* combat) {
    impl_->npcCombat = combat;
}
void Renderer::setCrew(const CrewView* crew) {
    impl_->crew = crew;
}
void Renderer::setExpeditionPlan(const ExpeditionPlan* plan) { impl_->expeditionPlan=plan; }
std::span<const ActivityBody> Renderer::combatBodies() const {
    return impl_->combatBodies;
}
void Renderer::setWeapons(const WeaponSystem* weapons) {
    impl_->weapons = weapons;
}
std::span<const ActivityBody> Renderer::weaponTargets() const {
    return impl_->weaponTargets;
}
void Renderer::setActivity(const ActivityState* state) {
    impl_->activity = state;
}
bool Renderer::contactsToggleHit(double x,double y) const {
    const auto& p=*impl_;if(!p.cockpitVisible)return false;
    const auto point=p.hudViewport.toCanvas({x,y});
    return std::any_of(p.cockpitContactHits.begin(),p.cockpitContactHits.end(),[&](const auto& hit){return hit.catalogue&&hit.bounds.contains(point);});
}
std::optional<ActivityId> Renderer::pickActivity(double x, double y) const {
    const auto& p = *impl_;
    if (p.cockpitVisible && (p.cockpitHelp || (p.expeditionPlan&&p.expeditionPlan->open) || (p.market && p.market->open) || (p.crew && p.crew->open) ||
                             p.instruments->phase == "MINING PROBE CAMERA"))
        return {};
    if (!p.activity || !p.activity->enabled)
        return {};
    if(p.cockpitVisible){const auto point=p.hudViewport.toCanvas({x,y});
        for(const auto& hit:p.cockpitContactHits)if(hit.bounds.contains(point))return hit.activity;
    }
    if (p.cockpitVisible &&
        std::any_of(p.cockpitHitRegions.begin(), p.cockpitHitRegions.end(),
                    [&](const auto& r) { return r.contains(p.hudViewport.toCanvas({x, y})); }))
        return {};
    double best = 18 * 18;
    std::optional<ActivityId> selected;
    for (const auto& [id, point] : p.activityPicks) {
        const double distance = glm::dot(point - glm::dvec2(x, y), point - glm::dvec2(x, y));
        if (distance < best) {
            best = distance;
            selected = id;
        }
    }
    return selected;
}
void Renderer::setSunShadows(bool enabled) {
    impl_->sunShadows = enabled;
}
void Renderer::setPolarLights(bool enabled) {
    impl_->polarLights = enabled;
}
void Renderer::setStellarOptics(bool enabled) {
    impl_->stellarOptics = enabled;
}
void Renderer::setPointLightFiltering(bool enabled) {
    impl_->pointLightFiltering = enabled;
}
void Renderer::setLandingShade(bool enabled) {
    impl_->landingShade = enabled;
}
void Renderer::setPlanetRings(bool enabled) {
    impl_->planetRings = enabled;
}
void Renderer::setWeatherEffects(bool enabled) {
    impl_->weatherEffects = enabled;
}
void Renderer::setCloudVolumes(bool enabled) {
    impl_->cloudVolumes = enabled;
}
void Renderer::setTerrainCoverageDebug(bool enabled) {
    impl_->terrainCoverageDebug = enabled;
}
std::shared_ptr<const Geometry> Renderer::submittedGeometry(const Address& address) const {
    auto found = impl_->details.find(address);
    return found == impl_->details.end() ? nullptr : found->second.blend.current;
}
void Renderer::resize(int w, int h) {
    if (w <= 0 || h <= 0)
        return;
    auto& p = *impl_;
    p.width = w;
    p.height = h;
    p.swapChain.width = w;
    p.swapChain.height = h;
    bgfx::reset(p.resetFlags, &p.swapChain);
}
FrameStats Renderer::render(const Navigation& nav, RegionCache& cache, bool overlay, double fps,
                            size_t markers, std::string_view notice, bool looking) {
    auto start = std::chrono::steady_clock::now();
    auto& p = *impl_;
    ++p.frame;
    if (p.skyGeometry && !p.skyGeometry->address.ancestorOf(nav.anchor))
        p.skyGeometry.reset();
    p.frameSeconds = std::clamp(std::chrono::duration<double>(start - p.previousTime).count(), 0.0, .1);
    p.previousTime = start;
    p.visualTime = p.fixedVisualTime >= 0 ? p.fixedVisualTime : p.visualTime + p.frameSeconds;
    p.updateEconomicLighting(p.frameSeconds);
    p.sectionLabel.clear();
    p.stats = {};
    p.exteriorHidden=false;
    p.surfaceLife.beginFrame();
    p.weatherReport.clear();
    p.weatherDetail.clear();
    p.weatherSeverity = 0;
    p.weatherPriority = 1e100;
    p.staticUploadBytes = 0;
    p.picks.clear();
    p.billboards.clear();
    p.cosmicBillboards.clear();
    p.pointLightBillboards.clear();
    p.stellarBillboards.clear();
    p.frameFrames.clear();
    p.framePoses.clear();
    p.cockpitVisible = p.instruments.has_value() && overlay;
    if (p.cockpitMotionEnabled && !p.probeRemote && p.cockpitVisible && !p.cockpitHelp && !(p.market && p.market->open)) {
        const auto& t = *p.instruments;
        p.cockpitMotion.update({t.localAcceleration, t.localAngularVelocity, t.contactImpulse, t.hullMetres,
                                t.hull, t.engine, t.contactSequence, t.landed},
                               p.fixedVisualTime >= 0 ? 1. / 60 : p.frameSeconds);
        p.head = p.cockpitMotion.pose();
    } else {
        p.cockpitMotion.reset();
        p.head = {};
    }
    p.stats.cockpitMotionDegrees = glm::length(p.head.angles) * 180 / std::numbers::pi;
    p.stats.cockpitMotionMetres = glm::length(p.head.translation);
    p.hudViewport = p.cockpitViewport();
    p.cockpitHitRegions.clear();
    p.cockpitContactHits.clear();
    p.atomView = p.instruments ? 3 : 2;
    p.contacts.clear();
    p.lifeSelection = nav.selected;
    p.skyOccluders.clear();
    auto nodes = p.universe.lineage(nav.anchor);
    p.anchorFrames.clear();
    for (const auto& node : nodes) {
        p.anchorFrames.push_back(node.frame);
        p.frameFrames.emplace(node.address, p.anchorFrames);
    }
    if (p.specimen) {
        std::erase_if(nodes, [&](const Node& node) { return !p.specimen->ancestorOf(node.address); });
        if (nodes.empty()) throw std::runtime_error("Specimen view escaped its source lineage");
    }
    cache.beginFrame();
    // Queue the actual destination before it enters the ordinary camera LOD.
    // Existing async generation, cache eviction and upload budgets still apply.
    if (p.instruments && p.instruments->warpDestination && !p.specimen && !p.probeRemote) {
        const auto target = p.universe.describe(*p.instruments->warpDestination);
        cache.request(target, 0);
        if (target.address.kind() == Kind::Planet)
            cache.catalog(p.universe.describe(target.address.parent()));
    }
    const auto stationAim = p.instruments ? p.instruments->gunAim : glm::dquat(1, 0, 0, 0);
    p.cameraQ = nav.orientation * stationAim * p.head.orientation;
    p.cameraInverse = glm::inverse(p.cameraQ);
    p.eye = nav.cameraPosition();
    if (p.instruments && p.instruments->playerGun < maxWeaponMounts)
        p.eye += nav.orientation * glm::dvec3(weaponMountLateralMetres(p.instruments->playerGun), 0, 0) *
                 (p.instruments->hullMetres / 12 / std::pow(10., nav.logDistance));
    p.prospectSites.reset();
    std::optional<Node> prospectPlanet;
    if (!p.specimen && !p.probeRemote && nav.anchor.kind() < Kind::Rock && (!p.instruments || !p.instruments->shrinking)) {
        for (const auto& node : nodes)
            if (node.address.kind() == Kind::Planet && hasSolidSurface(node))
                prospectPlanet = node;
        if (!prospectPlanet && nav.selected && nav.selected->kind() == Kind::Planet) {
            const auto selected = p.universe.describe(*nav.selected);
            if (hasSolidSurface(selected))
                prospectPlanet = selected;
        }
        if (prospectPlanet) {
            const auto planetEye = p.universe.pointRelative(
                nav.anchor, p.eye * std::pow(10., nav.logDistance - p.anchorFrames.back().logRadius),
                prospectPlanet->address, 0);
            auto region = tileAt(glm::normalize(planetEye), 7);
            auto depleted = [&](Tile t) {
                if (!p.economy)
                    return false;
                const auto key = prospectPlanet->address.child(Kind::Surface, encodeTile(t));
                for (const auto& pool : p.economy->exploration.prospects)
                    if (pool.address == key)
                        return pool.extracted == pool.capacity;
                return false;
            };
            if (depleted(region)) {
                std::optional<Tile> next;
                double score = -2;
                for (unsigned edge = 0; edge < 4; ++edge) {
                    auto candidate = region;
                    for (unsigned step = 0; step < 3; ++step) {
                        candidate = terrainNeighbor(candidate, edge);
                        const auto proximity = glm::dot(tileDirection(candidate), glm::normalize(planetEye));
                        if (!depleted(candidate) && proximity > score) {
                            score = proximity;
                            next = candidate;
                        }
                    }
                }
                if (next)
                    region = *next;
            }
            p.prospectSites = cache.prospects(*prospectPlanet, region);
        }
    }
    p.view = glm::mat4_cast(p.cameraInverse) * glm::translate(glm::dmat4(1), -p.eye);
    const double fov = p.instruments ? 70. * std::numbers::pi / 180. : 1.;
    p.fieldOfView = fov;
    p.projection =
        worldProjection(fov, static_cast<double>(p.width) / p.height, bgfx::getCaps()->homogeneousDepth);
    p.exteriorHidden=p.enclosedCavernView(nav,cache);
    p.stats.hiddenExterior=p.exteriorHidden;
    glm::mat4 v(p.view), proj(p.projection);
    const bool opticalDepth =
        (!p.instruments || p.instruments->altitude >= 0) && nav.anchor.kind() < Kind::Rock &&
        std::any_of(
            nodes.begin(), nodes.end(),
            [](const Node& n) {
                return n.address.kind() == Kind::Planet && planetClimate(n).pressureBar > .015;
            });
    p.hyperion->begin(p.mode, p.width, p.height, bool(p.instruments) || opticalDepth, bool(p.instruments));
    const Node* opticalPlanet = nullptr;
    double opticalDistance = 1e100;
    if (nav.anchor.kind() < Kind::Rock && (!p.instruments || !p.instruments->shrinking)) {
        for (const auto& planet : nodes) {
            if (planet.address.kind() != Kind::Planet || !hasSolidSurface(planet))
                continue;
            const auto climate = planetClimate(planet);
            const auto pose = p.poseFor(planet.address, nav);
            const double radius = std::pow(10., planet.frame.logRadius),
                         units = std::pow(10., nav.logDistance);
            const auto localEye = glm::inverse(pose.orientation) * (p.eye - pose.position) * units / radius;
            const double distance = (glm::length(localEye) - 1) * radius;
            if (glm::length(localEye) > .99 && glm::length(localEye) < 8 && distance < opticalDistance &&
                climate.pressureBar * 288 / std::max(60., climate.temperatureK) > .01) {
                opticalPlanet = &planet;
                opticalDistance = distance;
            }
        }
        if (opticalPlanet) {
            const auto pose = p.poseFor(opticalPlanet->address, nav);
            const double radius = std::pow(10., opticalPlanet->frame.logRadius),
                         units = std::pow(10., nav.logDistance);
            const auto localEye = glm::inverse(pose.orientation) * (p.eye - pose.position) * units / radius;
            glm::dmat4 mapping(glm::mat3_cast(glm::inverse(pose.orientation)) * (units / radius));
            mapping[3] = glm::dvec4(localEye, 1);
            p.hyperion->prepareWeather(*opticalPlanet, mapping,
                                       p.activity ? p.activity->seconds : p.visualTime, p.frameSeconds,
                                       p.cloudVolumes, p.fixedVisualTime >= 0);
            const double seconds = p.activity ? p.activity->seconds : p.visualTime;
            const auto climate = planetClimate(*opticalPlanet);
            p.hyperion->localWeather(
                climate, sampleWeather(climate, localEye, (glm::length(localEye) - 1) * radius, seconds),
                seconds, p.weatherEffects);
        }
    }
    p.staticUploadBytes += p.hyperion->materialUploadBytes();
    p.stats.uploadBytes += p.hyperion->materialUploadBytes();
    bgfx::setViewMode(0, bgfx::ViewMode::Sequential);
    bgfx::setViewTransform(0, glm::value_ptr(v), glm::value_ptr(proj));
    bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(p.width), static_cast<uint16_t>(p.height));
    p.stellarExposure = 1;
    {
        auto sky = extractSpectralSky(p.universe, nav);
        if (p.specimen || p.exteriorHidden) sky.strength = 0;
        sky.cameraToField = sky.cameraToField * stationAim * p.head.orientation;
        for (const auto& planet : nodes)
            if (planet.address.kind() == Kind::Planet && hasSolidSurface(planet)) {
                const auto climate = planetClimate(planet);
                const auto pose = p.poseFor(planet.address, nav);
                const double radius = std::pow(10., planet.frame.logRadius - nav.logDistance);
                const auto localEye = glm::inverse(pose.orientation) * (p.eye - pose.position) / radius;
                const auto light = glm::inverse(pose.orientation) * p.lightAt(planet.address, nav);
                if (glm::length(localEye) > .99) {
                    const float transmission = daylightSkyTransmission(
                        climate.pressureBar,
                        (glm::length(localEye) - 1) * std::pow(10., planet.frame.logRadius),
                        climate.scaleHeightMetres, glm::dot(glm::normalize(localEye), light));
                    sky.strength *= transmission;
                    p.stellarExposure *= transmission;
                }
            }
        p.hyperion->sky(sky, p.visualTime, float(p.width) / p.height, float(std::tan(fov * .5)));
    }
    if (p.instruments) {
        bgfx::setViewTransform(2, glm::value_ptr(v), glm::value_ptr(proj));
        bgfx::setViewRect(2, 0, 0, uint16_t(p.width), uint16_t(p.height));
        bgfx::setViewMode(2, bgfx::ViewMode::Default);
    }
    std::optional<Node> lifeSurvey;
    double surveyPriority = 1e300;
    for (const auto& system : nodes)
        if (system.address.kind() == Kind::System)
            if (auto planets = cache.catalog(system)) {
                p.contacts = extractCelestialContacts(p.universe, nav, system, *planets,
                                                      stationAim * p.head.orientation);
                for (const auto& planet : *planets) {
                    auto contact = std::find_if(p.contacts.begin(), p.contacts.end(),
                                                [&](const auto& c) { return c.address == planet.address; });
                    if (contact == p.contacts.end())
                        continue;
                    if (!hasSolidSurface(planet)) {
                        contact->life = LifeDetection::None;
                    } else if (auto summary = p.surfaceLife.summary(planet.address)) {
                        contact->population = summary->population;
                        contact->surfacePorts = summary->ports;
                        contact->openPorts = summary->openPorts;
                        contact->life = summary->population ? LifeDetection::Detected : LifeDetection::None;
                    } else if (p.instruments && !p.instruments->shrinking) {
                        contact->life = LifeDetection::Scanning;
                        const double priority = contact->selected ? 0 : contact->distanceMetres;
                        if (priority < surveyPriority) {
                            lifeSurvey = planet;
                            surveyPriority = priority;
                        }
                    }
                }
            }
    std::sort(p.contacts.begin(), p.contacts.end(),
              [](const auto& a, const auto& b) { return a.distanceMetres < b.distanceMetres; });
    const double pixelAngle = 2 * std::tan(fov * .5) / p.height;
    for (const auto& contact : p.contacts)
        if (contact.angularRadius > pixelAngle * .125)
            p.skyOccluders.push_back(contact);
    if (!p.exteriorHidden && p.instruments && !p.instruments->shrinking && p.stellarOptics)
        p.hyperion->stellar(extractStellarImage(p.universe, nav, p.contacts, stationAim * p.head.orientation),
                            float(p.width) / p.height, float(std::tan(fov * .5)), p.visualTime);
    p.stats.celestialContacts = p.contacts.size();
    p.stats.lifeWorlds = std::count_if(p.contacts.begin(), p.contacts.end(), hasSurfaceLife);
    p.stats.lifeScanning = std::count_if(p.contacts.begin(), p.contacts.end(),
                                         [](const auto& c) { return c.life == LifeDetection::Scanning; });
    bgfx::touch(0);
    std::optional<Node> terrainPlanet;
    for (const auto& node : nodes)
        if (node.address.kind() == Kind::Planet && hasSolidSurface(node))
            terrainPlanet = node;
    if (terrainPlanet) {
        const auto planetEye = p.universe.pointRelative(
            nav.anchor, p.eye * std::pow(10., nav.logDistance - nodes.back().frame.logRadius),
            terrainPlanet->address, terrainPlanet->frame.logRadius);
        p.terrain.update(*terrainPlanet, planetEye, p.height, p.frameSeconds, cache);
    } else
        p.terrain.clear();
    p.terrainActive = terrainPlanet && p.terrain.ready();
    p.terrainSunPlanet.reset();
    p.terrainSunComplete = true;
    std::set<Address> rendered;
    // Request the current focus first. Ancestors remain in the same spatial view
    // while the camera crosses the (possibly very large) physical scale gap.
    p.renderNode(nodes.back(), nav, cache);
    rendered.insert(nav.anchor);
    for (auto it = nodes.rbegin() + 1; it != nodes.rend(); ++it) {
        p.renderNode(*it, nav, cache);
        rendered.insert(it->address);
    }
    if (p.instruments && p.instruments->warpDestination && !p.specimen && !p.exteriorHidden) {
        const auto& destination = *p.instruments->warpDestination;
        if (!rendered.contains(destination)) {
            p.renderNode(p.universe.describe(destination), nav, cache);
            rendered.insert(destination);
        }
    }
    for (const auto& node : nodes)
        if (node.address.kind() == Kind::Galaxy)
            p.stellarSky(node, nav, cache);
    for (const auto& contact : p.contacts) {
        if(p.exteriorHidden)continue;
        const auto direction = contact.localDirection;
        if (direction.z >= 0 || contact.occluded)
            continue;
        if (contact.address.kind() == Kind::Planet && contact.angularRadius >= .008 &&
            !rendered.contains(contact.address)) {
            p.renderNode(p.universe.describe(contact.address), nav, cache);
            rendered.insert(contact.address);
        }
        const auto point = p.eye + p.cameraQ * direction * 500.;
        if (auto screen = p.project(point)) {
            p.picks.push_back({contact.address, *screen, contact.distanceMetres});
            // Large resolved planets keep their generated mesh. Unresolved
            // planets and the actual sun retain a legible angular light proxy
            // even when their physical distance exceeds the local depth range.
            if (contact.angularRadius < .008 || contact.address.kind() == Kind::System) {
                const double pixelAngle = 2 * std::tan(fov * .5) / p.height;
                const bool star = contact.address.kind() == Kind::System;
                const double angular = std::clamp(contact.angularRadius * (star ? 2.5 : 1.),
                                                  pixelAngle * (star ? 2.5 : 1.2), .12);
                p.addBillboard(point, angular * 500., contact.color * (star ? 1.f : .65f), true);
                ++p.stats.celestialLights;
            }
        }
    }
    std::vector<Node> rootNodes;
    const Address anchorRoot{nav.anchor.cell, {}};
    const auto anchorRootEye = p.universe.pointRelative(
        nav.anchor, p.eye * std::pow(10., nav.logDistance - nodes.back().frame.logRadius), anchorRoot,
        rootCellLogMetres);
    const auto [root, rootEye] = canonicalRoot(anchorRoot, anchorRootEye);
    for (const auto& address : p.specimen ? std::vector<Address>{} : rootNeighborhood(root)) {
        auto node = p.universe.describe(address);
        p.galaxySky(node, nav, cache,
                    celestialCellWeight(cellDisplacement(address.cell, root.cell), rootEye));
        if (!rendered.contains(address)) {
            p.renderNode(node, nav, cache, 2);
            rendered.insert(address);
        }
        if (!p.exteriorHidden)
            if (auto boundary = cache.requestCosmicBoundary(node))
                p.drawGeometry(boundary, node, nav, 1);
        if (auto it = p.details.find(address); it != p.details.end() && it->second.blend.current)
            ++p.stats.rootCells;
        const auto centre = cellDisplacement(address.cell, root.cell);
        if (glm::length(centre - rootEye) < 1.8)
            rootNodes.push_back(std::move(node));
    }
    if (terrainPlanet)
        p.renderTerrain(*terrainPlanet, nav, cache);
    const auto probeStarted=std::chrono::steady_clock::now();
    p.renderBores(nav,cache);
    p.stats.probeCpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-probeStarted).count();
    p.prospectMarkers.vertices.clear();
    p.prospectMarkers.indices.clear();
    if(p.economy&&!p.probeRemote&&!p.specimen)
        for(const auto& b:p.economy->exploration.probes.holes)
            if(p.probeSeconds>=b.blastTime()&&(!p.economy->network||b.planet.parent()==p.economy->network->system)) {
                const auto planet=p.universe.describe(b.planet);
                const auto point=p.universe.pointRelative(b.planet,b.entry/std::pow(10.,planet.frame.logRadius),nav.anchor,nav.logDistance);
                if(glm::length(point-p.eye)*std::pow(10.,nav.logDistance)>2500)continue;
                if(auto screen=p.project(point);screen&&screen->x>p.width*.2&&screen->x<p.width*.8&&screen->y>p.height*.18&&screen->y<p.height*.7)
                    for(unsigned k=0;k<24;++k){const double a=k*6.283185307179586/24,c=(k+1)*6.283185307179586/24;
                        addLine(p.prospectMarkers,{screen->x+std::cos(a)*12,screen->y+std::sin(a)*12,0},
                                                  {screen->x+std::cos(c)*12,screen->y+std::sin(c)*12,0},{1.f,.66f,.28f});}
            }
    if (prospectPlanet) {
        const auto planet = *prospectPlanet;
        const auto radius = std::pow(10., planet.frame.logRadius);
        const auto planetEye = p.universe.pointRelative(
            nav.anchor, p.eye * std::pow(10., nav.logDistance - p.anchorFrames.back().logRadius),
            planet.address, 0);
        if (p.prospectSites) {
            p.stats.prospectSites = p.prospectSites->size();
            std::array<const ProspectSite*, maxRegionalProspects> ranked{};
            size_t count = 0;
            for (const auto& site : *p.prospectSites)
                ranked[count++] = &site;
            std::sort(ranked.begin(), ranked.begin() + count, [&](auto a, auto b) {
                return glm::length(a->metres - planetEye) < glm::length(b->metres - planetEye);
            });
            unsigned shown = 0, rocks = 0;
            for (size_t index = 0; index < count; ++index) {
                const auto& site = *ranked[index];
                const auto distance = glm::length(site.metres - planetEye);
                const bool selected =
                    nav.selected && (*nav.selected == site.surface || site.surface.ancestorOf(*nav.selected));
                const auto point = p.universe.pointRelative(planet.address, site.outcropMetres / radius,
                                                            nav.anchor, nav.logDistance);
                if (glm::dot(site.direction, planetEye - site.metres) > 0 && (selected || shown < 4))
                    if (auto screen = p.project(point);
                        screen && screen->x > p.width * .20 && screen->x < p.width * .80 &&
                        screen->y > p.height * .18 && screen->y < p.height * .70) {
                        const double x = screen->x, y = screen->y, s = selected ? 13 : 7;
                        const auto color = selected    ? glm::vec3(.55, .97, .78)
                                           : site.easy ? glm::vec3(.35, .78, .92)
                                                       : glm::vec3(.96, .62, .27);
                        for (unsigned i = 0; i < 4; ++i) {
                            const double a = i * 1.5707963267948966, b = (i + 1) * 1.5707963267948966;
                            addLine(p.prospectMarkers, {x + std::cos(a) * s, y + std::sin(a) * s, 0},
                                    {x + std::cos(b) * s, y + std::sin(b) * s, 0}, color);
                        }
                        p.picks.push_back({site.surface, *screen, distance});
                        ++shown;
                        ++p.stats.prospectMarkers;
                    }
                if (distance < 2200 && rocks < 12) {
                    for (const auto& node : site.rocks) {
                        auto frames = site.parentFrames;
                        frames.push_back(node.frame);
                        p.frameFrames.try_emplace(node.address, std::move(frames));
                        p.renderNode(node, nav, cache, 3);
                        if (auto screen = p.project(p.poseFor(node.address, nav).position))
                            p.picks.push_back({node.address, *screen, distance});
                        ++rocks;
                        ++p.stats.prospectRocks;
                    }
                }
            }
        }
    }
    if (p.economy && p.economy->network && (!p.market || !p.market->open))
        if (const auto* seam = activeSeam(p.economy->exploration, p.economy->network->system)) {
            auto beacon = [&](glm::dvec3 point, const Address& pick, glm::vec3 color, bool entry) {
                if (auto screen = p.project(point);
                    screen && screen->x > p.width * .20 && screen->x < p.width * .80 &&
                    screen->y > p.height * .18 && screen->y < p.height * .70) {
                    const double x = screen->x, y = screen->y, s = entry ? 10 : 16;
                    for (unsigned k = 0; k < 4; ++k) {
                        const double a = k * 1.5707963267948966 + (entry ? .7853981633974483 : 0);
                        const double b = a + 1.5707963267948966;
                        addLine(p.prospectMarkers, {x + std::cos(a) * s, y + std::sin(a) * s, 0},
                                {x + std::cos(b) * s, y + std::sin(b) * s, 0}, color);
                    }
                    if (!entry) {
                        addLine(p.prospectMarkers, {x - 5, y, 0}, {x + 5, y, 0}, color);
                        addLine(p.prospectMarkers, {x, y - 5, 0}, {x, y + 5, 0}, color);
                        if (pick != nav.anchor)
                            p.picks.push_back({pick, *screen, glm::length(point - p.eye)});
                    }
                }
            };
            const auto planet = p.universe.describe(seam->pool.parent());
            const auto entry = p.universe.pointRelative(planet.address,
                seam->entryMetres / std::pow(10., planet.frame.logRadius), nav.anchor, nav.logDistance);
            beacon(entry, seam->source, {.35f, .85f, 1.f}, true);
            if (seam->marked && seam->material) {
                const auto pose = p.poseFor(*seam->material, nav);
                beacon(pose.position, seam->source, {1.f, .75f, .25f}, false);
            }
        }
    Mesh locators;
    auto candidates = [&](const Node& parent) {
        if(p.exteriorHidden&&parent.address.kind()<Kind::Surface)return;
        auto parentPose = p.poseFor(parent.address, nav);
        auto pc = p.cameraSpace(parentPose.position);
        double parentPixels = LOD::pixels(parentPose.scale, glm::length(pc), p.height, 1);
        const bool catalogVisible = parentPixels >= 25 && parentPose.scale <= 40;
        const bool selectedChild = nav.selected && nav.selected->parent() == parent.address;
        if (!catalogVisible && !selectedChild)
            return;
        auto catalog = catalogVisible ? cache.catalog(parent) : nullptr;
        const auto parentFrames = p.framesFor(parent.address);
        auto present = [&](const Node& n) {
            const auto& a = n.address;
            if (p.instruments) {
                auto contact = std::find_if(p.contacts.begin(), p.contacts.end(),
                                            [&](const CelestialContact& c) { return c.address == a; });
                if (contact != p.contacts.end() && contact->occluded)
                    return;
            }
            auto childFrames = parentFrames;
            childFrames.push_back(n.frame);
            p.frameFrames.try_emplace(a, std::move(childFrames));
            auto pose = p.poseFor(a, nav);
            auto cp = p.cameraSpace(pose.position);
            auto screen = p.project(pose.position);
            if (!screen || cp.z >= 0)
                return;
            bool isSelected = nav.selected && a == *nav.selected;
            if (a == nav.anchor)
                return;
            p.picks.push_back({a, *screen, -cp.z});
            if (isSelected || n.address.kind() == Kind::Planet || n.address.kind() >= Kind::Surface) {
                double rr = -cp.z * .0025;
                p.addBillboard(pose.position, rr,
                               n.address.kind() == Kind::System ? stellarRadiance(n)
                                                                : material(n.environment.mineral).color);
            }
            if (isSelected) {
                double x = screen->x, y = screen->y;
                glm::vec3 col(.31, .66, .74);
                for (int corner = 0; corner < 4; ++corner) {
                    double sx = (corner & 1) ? 1 : -1, sy = (corner & 2) ? 1 : -1;
                    addLine(locators, {x + sx * 10, y + sy * 10, 0}, {x + sx * 17, y + sy * 10, 0}, col);
                    addLine(locators, {x + sx * 10, y + sy * 10, 0}, {x + sx * 10, y + sy * 17, 0}, col);
                }
            }
            // Regional survey markers sit on the terrain already in view.
            // Request a separate inspection mesh only when one is selected.
            if (!isSelected && a.kind() == Kind::Surface && decodeTile(a.id()).level == 7)
                return;
            if (!rendered.contains(a)) {
                p.renderNode(n, nav, cache);
                rendered.insert(a);
            }
        };
        if (catalog)
            for (const auto& n : *catalog)
                present(n);
        // Automatic flight may discover any canonical surface tile or any
        // displayed star ID, beyond the deliberately small locator catalog.
        if (nav.selected && nav.selected->parent() == parent.address &&
            (!catalog || std::none_of(catalog->begin(), catalog->end(),
                                      [&](const auto& n) { return n.address == *nav.selected; })))
            present(p.universe.describe(*nav.selected));
    };
    if (nodes.size() > 1)
        p.renderSection(nodes[nodes.size() - 2], nav, cache);
    candidates(nodes.back());
    if (nodes.size() > 1)
        candidates(nodes[nodes.size() - 2]);
    for (const auto& node : rootNodes)
        if (node.address != root)
            candidates(node);
    if(!p.exteriorHidden)
        for (const auto& a : rendered)
            if (a.kind() == Kind::Planet)
                p.drawPlanetRings(p.universe.describe(a), nav);
    p.renderActivity(nodes, nav, cache);
    p.renderLanding(nav, terrainPlanet);
    // Nearby visible worlds claim the two existing workers first. Surveying
    // the remaining system never evicts a close city's geometry to keep a
    // distant world's full layout resident.
    if (lifeSurvey && p.frame % 8 == 0)
        p.surfaceLife.survey(*lifeSurvey);
    p.surfaceLife.finishFrame();
    const auto surfaceStats = p.surfaceLife.stats();
    if (!surfaceStats.error.empty())
        throw std::runtime_error("Surface generation: " + surfaceStats.error);
    p.stats.surfaceBytes = surfaceStats.bytes;
    p.stats.surfacePending = surfaceStats.pending;
    p.stats.surfaceGpuBytes = p.surfaceMeshBytes;
    auto drawBillboards = [&](const std::vector<Instance>& instances, bool cosmic, bool stellar = false,
                              bool compact = false) {
        uint32_t count = bgfx::getAvailInstanceDataBuffer(static_cast<uint32_t>(instances.size()), 80);
        if (count) {
            bgfx::InstanceDataBuffer buffer;
            bgfx::allocInstanceDataBuffer(&buffer, count, 80);
            std::memcpy(buffer.data, instances.data(), count * sizeof(Instance));
            const auto& mesh = compact ? p.pointLightBillboardMesh : p.hyperionBillboardMesh;
            bgfx::setVertexBuffer(0, mesh.vertices);
            bgfx::setIndexBuffer(mesh.indices);
            bgfx::setInstanceDataBuffer(&buffer);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_MSAA | BGFX_STATE_DEPTH_TEST_GEQUAL |
                           BGFX_STATE_BLEND_ADD);
            {
                // Use the same focus-local reference at every magnification.
                if (cosmic) {
                    const auto q = p.universe.relative(nav.anchor, root, nav.logDistance).orientation;
                    glm::dmat4 pattern = glm::mat4_cast(q);
                    for (unsigned i = 0; i < 3; ++i)
                        pattern[i] /= 500.;
                    pattern[3] = glm::dvec4(-(q * p.eye) / 500., 1);
                    p.hyperion->bindEmissions(p.universe.describe(Address{}), glm::mat4(pattern),
                                              p.visualTime);
                } else {
                    const auto& node = nodes.back();
                    auto focusPose = p.poseFor(nav.anchor, nav);
                    auto focusModel = glm::translate(glm::mat4(1), glm::vec3(focusPose.position)) *
                                      glm::mat4_cast(glm::quat(focusPose.orientation)) *
                                      glm::scale(glm::mat4(1), glm::vec3(float(focusPose.scale)));
                    p.hyperion->bindEmissions(node, glm::inverse(focusModel), p.visualTime);
                }
            }
            bgfx::submit(cosmic && p.instruments ? 0 : p.worldView(),
                         stellar ? p.hyperion->coronaProgram() : p.hyperion->glowProgram());
        }
    };
    drawBillboards(p.pointLightBillboards, true, false, true);
    drawBillboards(p.cosmicBillboards, true);
    drawBillboards(p.billboards, false);
    drawBillboards(p.stellarBillboards, false, true);
    const bool warpView = p.instruments && !p.probeRemote && !p.specimen && !p.exteriorHidden &&
                          p.instruments->warp != WarpPhase::Off;
    const bool engaged = warpView && p.instruments->warp != WarpPhase::Drop;
    if (engaged != p.warpEngaged) {
        p.warpDestinationExit = !engaged && p.instruments && p.instruments->warpDestination.has_value();
        if (p.warpDestinationExit)
            p.warpExitBore = warpTunnelAperture(1, p.instruments->warpAngularRadius, fov);
        p.warpEngaged = engaged;
        p.warpTransition = 0;
        p.warpTransitionVisible = !p.probeRemote && !p.specimen && !p.exteriorHidden;
    }
    p.warpTransition = std::min(1., p.warpTransition + p.frameSeconds /
        (engaged ? 1.05 : p.warpDestinationExit ? 2.8 : .85));
    const double transition = p.warpTransitionVisible ? std::sin(std::numbers::pi * p.warpTransition) : 0.;
    const double warpSpeed = warpView ? p.instruments->speed : 0.;
    if (engaged)
        p.warpOpticalDestination = p.instruments->warpDestination;
    const double warpTarget = engaged
        ? std::max(p.instruments->warpDestination ? .72 : 0.,
                   smoothstep(3.7, 7.3, std::log10(std::max(1., warpSpeed)))) : 0.;
    p.warpLight = std::lerp(p.warpLight, warpTarget,
        -std::expm1(-p.frameSeconds * (!engaged && p.warpDestinationExit ? 1.1 : 4.)));
    if (p.instruments && !p.probeRemote && !p.specimen && !p.exteriorHidden && (p.warpLight > .005 || transition > .005)) {
        // Camera-facing optical trails. Their animation survives frame rebases
        // and the opaque scene depth still masks them against nearby bodies.
        Mesh streaks;
        p.warpClock = std::fmod(p.warpClock + p.frameSeconds * (.55 + p.warpLight * 3.1), 1.);
        const auto velocity = p.cameraInverse * (nav.orientation * p.instruments->localVelocity);
        const double aspect = double(p.width) / p.height;
        const double tanFov = std::tan(fov * .5);
        const double forward = -velocity.z;
        auto desiredFocus = forward > std::max(1e-12, .25 * glm::length(velocity))
            ? glm::clamp(glm::dvec2(velocity.x / (forward * tanFov * aspect),
                                    velocity.y / (forward * tanFov)), glm::dvec2(-.8), glm::dvec2(.8))
            : glm::dvec2(0);
        const auto& travel = *p.instruments;
        const auto destination = p.warpOpticalDestination ? p.cameraInverse *
            (p.universe.relative(*p.warpOpticalDestination, nav.anchor, nav.logDistance).position - p.eye)
            : glm::dvec3(0);
        if (p.warpOpticalDestination && destination.z < -1e-12) {
            const auto centre = glm::clamp(glm::dvec2(destination.x / (-destination.z * tanFov * aspect),
                                                     destination.y / (-destination.z * tanFov)),
                                           glm::dvec2(-.8), glm::dvec2(.8));
            desiredFocus = glm::mix(desiredFocus, centre, engaged ? travel.warpReveal : 1.);
        }
        if (engaged || forward > 1e-12)
            p.warpFocus = glm::mix(p.warpFocus, desiredFocus, -std::expm1(-p.frameSeconds * 9.));
        const auto focus = p.warpFocus;
        const auto world = [&](glm::dvec2 screen) {
            return p.eye + p.cameraQ * glm::dvec3(screen.x * aspect * tanFov, screen.y * tanFov, -1) * 500.;
        };
        constexpr double turn = 2. * std::numbers::pi;
        const double twist = p.visualTime * .48;
        const auto polar = [&](double radius, double angle) {
            return glm::dvec2(radius * std::cos(angle) / aspect, radius * std::sin(angle));
        };
        const auto prism = [](double hue) {
            const auto phase = glm::dvec3(hue, hue - turn / 3., hue - 2. * turn / 3.);
            return glm::vec3(glm::pow(.5 + .5 * glm::cos(phase), glm::dvec3(1.6)));
        };

        // Feathered emissive strips give the colour room to spread before the
        // existing bloom pass. Only the field uses this palette.
        Mesh ribbons;
        constexpr unsigned lanes = 5;
        const auto strip = [&](unsigned samples, auto section) {
            constexpr std::array offsets{-1., -.27, 0., .27, 1.};
            constexpr std::array strengths{0.f, .38f, 1.f, .38f, 0.f};
            const auto first = uint32_t(ribbons.vertices.size());
            for (unsigned j = 0; j <= samples; ++j) {
                const auto [centre, edge, colour] = section(double(j) / samples);
                for (unsigned lane = 0; lane < lanes; ++lane) {
                    const auto point = world(centre + edge * offsets[lane]);
                    ribbons.vertices.push_back(
                        {float(point.x), float(point.y), float(point.z), rgba(colour * strengths[lane])});
                    if (j && lane) {
                        const auto end = first + j * lanes + lane;
                        ribbons.indices.insert(ribbons.indices.end(),
                            {end - lanes - 1, end - lanes, end - 1, end - lanes, end, end - 1});
                    }
                }
            }
        };

        // A perspective bore around the vanishing point. The sky still
        // shows through the opening, and opaque scene depth masks the field.
        constexpr bgfx::ViewId warpPass = 66;
        bgfx::setViewMode(warpPass, bgfx::ViewMode::Sequential);
        bgfx::setViewRect(warpPass, 0, 0, uint16_t(p.width), uint16_t(p.height));
        bgfx::setViewTransform(warpPass, glm::value_ptr(v), glm::value_ptr(proj));
        const double outerRadius = std::hypot(aspect * (1. + std::abs(focus.x)),
                                              1. + std::abs(focus.y)) + .28;
        const double shutter = smoothstep(.04, .96, p.warpTransition);
        const double destinationBore = !engaged && p.warpDestinationExit ? p.warpExitBore :
            warpTunnelAperture(travel.warpReveal, travel.warpAngularRadius, fov);
        const double aperture = std::lerp(outerRadius, destinationBore, engaged ? shutter : 1. - shutter);
        const auto bore = [&](double radius, double angle) {
            return polar(radius * (1. + .036 * std::cos(angle * 8.)), angle);
        };
        if (p.warpLight > .015 && aperture < outerRadius - .001) {
            Mesh wall;
            constexpr unsigned sectors = 80, slices = 24, row = sectors + 1;
            wall.vertices.reserve((slices + 1) * row);
            wall.indices.reserve(slices * sectors * 6);
            const double ratio = outerRadius / aperture;
            for (unsigned slice = 0; slice <= slices; ++slice) {
                const double t = double(slice) / slices;
                const double radius = aperture * std::pow(ratio, t);
                const double depth = aperture / radius;
                const double wave = .5 + .5 * std::cos(turn * (depth * 2.2 + p.warpClock));
                const float opacity = float(p.warpLight * .82 * smoothstep(0., .16, t));
                for (unsigned j = 0; j <= sectors; ++j) {
                    const double angle = j * turn / sectors;
                    const float rib = float(std::pow(.5 + .5 * std::cos(angle * 16.), 7.));
                    const auto pigment = prism(angle * 2. + depth * 3. + twist);
                    const auto colour = glm::vec3(.013f, .020f, .050f) +
                                        pigment * float(.07 + .14 * wave + .09 * rib);
                    const auto position = world(focus + bore(radius, angle));
                    wall.vertices.push_back({float(position.x), float(position.y), float(position.z),
                                             rgba(colour, opacity)});
                    if (slice && j) {
                        const auto end = uint32_t(wall.vertices.size() - 1);
                        wall.indices.insert(wall.indices.end(),
                                           {end - row - 1, end - row, end - 1, end - row, end, end - 1});
                    }
                }
            }
            p.transient(wall, warpPass, false, false, !p.warpOpticalDestination, false, true);

            // Ribs expand in projection as they rush by. The innermost lip
            // stays around the aperture so the viewer can read its depth.
            strip(96, [&](double t) {
                const double angle = t * turn;
                const auto colour = glm::mix(prism(angle * 2. + twist), glm::vec3(.75f, .90f, 1.f), .54f);
                const double radius = aperture * 1.05;
                return std::tuple{focus + bore(radius, angle), polar(radius * .075, angle),
                                  colour * float(p.warpLight * .62)};
            });
            for (unsigned rib = 0; rib < 6; ++rib) {
                const double age = std::fmod(p.warpClock + rib / 6., 1.);
                const double radius = aperture * 1.11 / (1. - .93 * age);
                if (radius > outerRadius)
                    continue;
                const float fade = float(p.warpLight * smoothstep(0., .18, age) *
                                         (1. - smoothstep(.77, 1., age)));
                strip(80, [&](double t) {
                    const double angle = t * turn;
                    const auto colour = glm::mix(prism(angle * 2. + twist + age * 1.6),
                                                 glm::vec3(.74f, .90f, 1.f), .38f);
                    return std::tuple{focus + bore(radius, angle), polar(radius * .037, angle),
                                      colour * (fade * .46f)};
                });
            }
        }

        for (unsigned i = 0; i < 148; ++i) {
            const Random random{mix64(0x574152505354524bULL + i)};
            const double angle = random.unit(0) * turn;
            const double age = std::fmod(p.warpClock + random.unit(1), 1.);
            const double radius = (.14 + age * age * 3.15) * (.67 + .55 * random.unit(2));
            const double span = (.055 + .95 * age * age) * (p.warpLight + transition * .22);
            const double inner = aperture * (1.08 + .036 * std::cos(angle * 8.));
            if (radius <= inner)
                continue;
            const auto head = focus + polar(radius, angle);
            const auto tail = focus + polar(std::max(inner, radius - span), angle);
            const float fade = float((p.warpLight + transition * .20) * smoothstep(.04, .22, age) *
                                     (1. - smoothstep(.78, 1., age)));
            const auto colour = glm::mix(prism(angle + twist), glm::vec3(1), .30f);
            addLine(streaks, world(tail), world(glm::mix(tail, head, .72)), colour * (fade * .38f));
            addLine(streaks, world(glm::mix(tail, head, .72)), world(head), colour * fade);
            if (i % 4 == 0 && fade > .06f) {
                strip(2, [&](double t) {
                    return std::tuple{glm::mix(tail, head, t), polar(.008 + .012 * age, angle + turn * .25),
                                      colour * (fade * float(t) * .60f)};
                });
            }
        }

        // Petalled field fronts sweep the viewport in opposite directions.
        for (unsigned band = 0; band < 4; ++band) {
            const double age = std::clamp(p.warpTransition * 1.24 - band * .095, 0., 1.);
            const double radius = .09 + 2.40 * (engaged ? age : 1. - age);
            const float energy = float(transition * (1. - band * .14) * .74);
            if (energy < .005f)
                continue;
            strip(100, [&](double t) {
                const double a = t * turn;
                const double petals = 1. + .095 * std::cos(a * 6. + twist + age * 4.) +
                                      .025 * std::sin(a * 12. - twist);
                const double hue = a * 3. + band * .65 + (engaged ? 3.5 : .2) + age * 2.;
                return std::tuple{focus + polar(radius * petals, a), polar(.025 + radius * .055, a),
                                  glm::mix(prism(hue), glm::vec3(1), .18f) * energy};
            });
        }
        // The collapsing field leaves a brief central afterimage, giving
        // the last frame of the transition a readable end.
        if (!engaged && p.warpTransitionVisible && p.warpTransition > .68) {
            const double release = (p.warpTransition - .68) / .32;
            const float fade = float(std::sin(std::numbers::pi * release) * .45);
            strip(64, [&](double t) {
                const double angle = t * turn;
                const double radius = .12 + release * .33;
                return std::tuple{focus + polar(radius, angle), polar(.018, angle),
                                  glm::vec3(.72f, .86f, 1.f) * fade};
            });
        }
        p.transient(ribbons, warpPass, false, true, !p.warpOpticalDestination);
        p.transient(streaks, warpPass, true, true, !p.warpOpticalDestination);
    } else {
        p.warpFocus = {};
    }
    if (p.instruments && p.warpLight < .12 && !p.instruments->shrinking &&
        !p.instruments->landed && p.instruments->speed > 24) {
        // Hyperion drawMotion's bounded, world-grid-anchored navigation streaks.
        // These are optical cues, never additional stars or collision objects.
        Mesh trails;
        const double unitMetres = std::pow(10., nav.logDistance);
        const double driveScale = std::max(1., p.instruments->driveSpeed / 450.);
        const double spacing = 180. * driveScale / unitMetres;
        const auto grid = glm::floor(p.eye / spacing);
        const auto velocity = nav.orientation * p.instruments->localVelocity;
        const auto direction = glm::normalize(velocity);
        const auto forward = nav.orientation * glm::dvec3(0, 0, -1);
        const double strength = std::clamp(p.instruments->speed / (450. * driveScale), 0., 1.);
        for (int x = -2; x <= 2; x++)
            for (int y = -2; y <= 2; y++)
                for (int z = -2; z <= 2; z++) {
                    auto cell = grid + glm::dvec3(x, y, z);
                    double h =
                        std::sin(glm::dot(glm::mod(cell, glm::dvec3(4096)), glm::dvec3(127.1, 311.7, 74.7))) *
                        43758.5453;
                    h -= std::floor(h);
                    const auto point =
                        (cell + glm::dvec3(h, std::fmod(h * 17, 1.), std::fmod(h * 31, 1.))) * spacing;
                    const double distance = glm::length(point - p.eye);
                    if (distance < spacing / 6 || distance > spacing * 2 ||
                        glm::dot(point - p.eye, forward) < spacing / 9)
                        continue;
                    const auto length = (driveScale + p.instruments->speed * .028) / unitMetres;
                    addLine(trails, point, point - direction * length,
                            glm::vec3(.28, .78, .91) *
                                float((1 - distance / (spacing * 2)) * strength * .36));
                }
        p.transient(trails, p.worldView(), true, true);
    }
    if ((p.weapons && (!p.weapons->shots().empty() || !p.weapons->missiles().empty())) ||
        (p.npcCombat && !p.npcCombat->shots().empty())) {
        Mesh beams, weaponGlow, miningChips, miningDust;
        const auto rotation = glm::mat4_cast(glm::quat(p.cameraInverse));
        const glm::mat4 projection(p.projection);
        bgfx::setViewTransform(63, glm::value_ptr(rotation), glm::value_ptr(projection));
        bgfx::setViewRect(63, 0, 0, uint16_t(p.width), uint16_t(p.height));
        const double units = std::pow(10., nav.logDistance);
        const auto cameraRight = p.cameraQ * glm::dvec3(1, 0, 0);
        const auto cameraUp = p.cameraQ * glm::dvec3(0, 1, 0);
        const auto screenWidth = [&](glm::dvec3 point) {
            return 2 * std::tan(fov * .5) * std::max(worldNearPlane, -p.cameraSpace(point + p.eye).z) /
                   p.height;
        };
        const auto vertex = [&](glm::dvec3 point, glm::vec3 colour) {
            weaponGlow.vertices.push_back({float(point.x), float(point.y), float(point.z), rgba(colour)});
        };
        const auto ribbon = [&](glm::dvec3 from, glm::dvec3 to, double startWidth, double endWidth,
                                glm::vec3 startColour, glm::vec3 endColour) {
            auto side = glm::cross(to - from, from + to);
            const double length = glm::length(side);
            side = length > 1e-20 ? side / length : cameraRight;
            const auto first = uint32_t(weaponGlow.vertices.size());
            for (int edge = -1; edge <= 1; ++edge) {
                vertex(from + side * (startWidth * edge), edge ? glm::vec3(0) : startColour);
                vertex(to + side * (endWidth * edge), edge ? glm::vec3(0) : endColour);
            }
            weaponGlow.indices.insert(weaponGlow.indices.end(),
                                      {first, first + 1, first + 2, first + 1, first + 3, first + 2,
                                       first + 2, first + 3, first + 4, first + 3, first + 5, first + 4});
        };
        const auto flash = [&](glm::dvec3 point, double radius, glm::vec3 colour) {
            const auto centre = uint32_t(weaponGlow.vertices.size());
            vertex(point, colour);
            constexpr unsigned sides = 10;
            for (unsigned j = 0; j <= sides; ++j) {
                const double angle = j * 2 * std::numbers::pi / sides;
                vertex(point + radius * (cameraRight * std::cos(angle) + cameraUp * std::sin(angle)),
                       glm::vec3(0));
                if (j)
                    weaponGlow.indices.insert(weaponGlow.indices.end(), {centre, centre + j, centre + j + 1});
            }
        };
        auto drawShot = [&](const WeaponShot& shot) {
            const auto source = p.universe.describe(shot.reference);
            const double sourceUnits = std::pow(10., source.frame.logRadius);
            const auto a = p.universe.pointRelative(shot.reference, shot.from / sourceUnits, nav.anchor,
                                                    nav.logDistance) -
                           p.eye;
            const auto b =
                p.universe.pointRelative(shot.reference, shot.to / sourceUnits, nav.anchor, nav.logDistance) -
                p.eye;
            const bool missile = shot.level == missileLauncher;
            const bool ion = shot.level == ionDisruptor;
            const auto colour = shot.enemy ? glm::vec3(1, .22, .12) : weaponSpec(shot.level).colour;
            const float pulse = float(1 - smoothstep(0, .16, shot.age));
            const auto ray = b - a;
            const double length = glm::length(ray);
            if (length < 1e-20)
                return;
            const auto axis = ray / length;
            const double metres = shot.scale / units;
            if (shot.utility) {
                const auto right = cameraRight * (metres * .1);
                addLine(beams, a, b, colour * pulse);
                addLine(beams, a + right, b + right, glm::vec3(pulse));
                addLine(beams, a - right, b - right, colour * pulse);
            } else if (pulse > 0 && !(missile && shot.hit)) {
                const auto width = [&](glm::dvec3 point) {
                    return std::clamp((ion ? .8 : .38) * metres, screenWidth(point) * 1.4,
                                      screenWidth(point) * 7.);
                };
                ribbon(a, b, width(a), width(b), colour * pulse, colour * (pulse * .8f));
                ribbon(a, b, width(a) * .31, width(b) * .31,
                       glm::mix(colour, glm::vec3(1), .75f) * pulse,
                       glm::mix(colour, glm::vec3(1), .75f) * pulse);
                flash(a, width(a) * (missile ? 5. : 3.2), colour * pulse);
            }
            if(shot.utility&&p.stats.miningParticles==0&&glm::length(shot.from-shot.to)>.01){
                const auto target=p.universe.describe(nav.selected.value_or(nav.anchor));
                auto mineral=target.environment.mineral;
                if(p.economy)if(const auto* seam=markedSeam(p.economy->exploration,target.address))
                    mineral=seam->resource==0?Mineral::Ice:seam->resource==1?Mineral::Iron:seam->resource==3?Mineral::Graphite:mineral==Mineral::Olivine?Mineral::Olivine:Mineral::Quartz;
                const auto normal=glm::normalize(shot.from-shot.to);
                const auto down=shot.reference.kind()==Kind::Planet&&glm::length(shot.to)>1e-8?-glm::normalize(shot.to):glm::dvec3(0,-1,0);
                const auto motes=miningImpactMotes(shot.to+normal*.1,normal,down,seedOf(target.address,p.universe.seed()),mineral,p.probeSeconds);
                const auto orientation=p.poseFor(shot.reference,nav).orientation;
                for(size_t i=0;i<motes.count;++i){const auto& m=motes.items[i];
                    const auto centre=p.universe.pointRelative(shot.reference,m.position/sourceUnits,nav.anchor,nav.logDistance)-p.eye;
                    if(m.spark){p.addBillboard(centre+p.eye,m.radius/units,m.colour*float(m.opacity));continue;}
                    auto& mesh=m.fragment?miningChips:miningDust;const auto first=uint32_t(mesh.vertices.size());
                    if(m.fragment){for(const auto corner:{glm::dvec3(1,0,0),glm::dvec3(-.6,.8,0),glm::dvec3(-.4,-.5,.7),glm::dvec3(0,0,-.7)}){
                            const auto point=centre+orientation*(corner*m.stretch)*(m.radius/units);mesh.vertices.push_back({float(point.x),float(point.y),float(point.z),rgba(m.colour*.45f,m.opacity)});}
                        mesh.indices.insert(mesh.indices.end(),{first,first+1,first+2,first,first+3,first+1,first,first+2,first+3,first+1,first+3,first+2});
                    }else{mesh.vertices.push_back({float(centre.x),float(centre.y),float(centre.z),rgba(m.colour*.35f,m.opacity)});
                        for(unsigned side=0;side<=8;++side){const double angle=side*2*std::numbers::pi/8;
                            const auto point=centre+p.cameraQ*glm::dvec3(std::cos(angle),std::sin(angle),0)*(m.radius/units);
                            mesh.vertices.push_back({float(point.x),float(point.y),float(point.z),rgba(m.colour*.35f,0)});
                            if(side)mesh.indices.insert(mesh.indices.end(),{first,first+side,first+side+1});}}
                }
                p.stats.miningParticles+=motes.count;
            }
            if (shot.hit && !shot.utility) {
                const bool subsystem = shot.component != ShipSystem::Hull;
                const float age = float(std::clamp(shot.age / weaponShotLifetime(shot), 0., 1.));
                const float fade = (1 - age) * (1 - age);
                const auto spark = missile || subsystem ? glm::vec3(1, .65f, .20f) : colour;
                const auto point = b - axis * (.28 * metres);
                const double pixel = screenWidth(point);
                const double radius = std::clamp((missile ? 34. : shot.disabled ? 22. : ion ? 12. : 7.) * metres,
                                                 7 * pixel, 80 * pixel);
                flash(point, radius * (.27 + age * .85), spark * fade);
                flash(point, radius * .22, glm::mix(spark, glm::vec3(1), .8f) * (fade * fade));
                constexpr double golden = 2.399963229728653;
                const unsigned sparks = missile || shot.disabled ? 20 : 12;
                for (unsigned j = 0; j < sparks; ++j) {
                    const double angle = golden * j;
                    const auto direction = cameraRight * std::cos(angle) + cameraUp * std::sin(angle);
                    const double travel = radius * (.2 + age) * (.50 + .50 * double((j * 13) % 17) / 16.);
                    const auto tip = point + direction * travel - axis * (travel * .13);
                    addLine(beams, tip - direction * (radius * .12 * (1 - age)), tip, spark * fade);
                }
                if (ion || missile || shot.disabled) {
                    const double ring = radius * (.22 + .9 * age);
                    constexpr unsigned sides = 24;
                    for (unsigned j = 0; j < sides; ++j) {
                        const double angle = j * 2 * std::numbers::pi / sides;
                        const double next = (j + 1) * 2 * std::numbers::pi / sides;
                        addLine(beams, point + ring * (cameraRight * std::cos(angle) + cameraUp * std::sin(angle)),
                                point + ring * (cameraRight * std::cos(next) + cameraUp * std::sin(next)),
                                colour * (fade * .65f));
                    }
                }
            }
        };
        if (p.weapons) {
            for (const auto& shot : p.weapons->shots())
                drawShot(shot);
            for (const auto& missile : p.weapons->missiles()) {
                const auto source = p.universe.describe(missile.reference);
                const auto tip = p.universe.pointRelative(
                    missile.reference, missile.position / std::pow(10., source.frame.logRadius),
                    nav.anchor, nav.logDistance) - p.eye;
                const auto axis = p.poseFor(missile.reference, nav).orientation * missile.direction;
                const auto colour = missile.target ? glm::vec3(.35f, .78f, 1) : glm::vec3(1, .27f, .10f);
                const auto tail = tip - axis * (std::min(78., 8. + missile.age * 360.) / units);
                const double width = std::max(.65 / units, screenWidth(tip) * 2.1);
                ribbon(tail, tip, width * 2.8, width, glm::vec3(0), colour);
                ribbon(glm::mix(tail, tip, .55), tip, width, width * .4, glm::vec3(0), glm::vec3(1));
                flash(tip, width * 2.3, colour);
            }
        }
        if (p.npcCombat)
            for (const auto& shot : p.npcCombat->shots())
                drawShot(shot);
        p.transient(weaponGlow, 63, false, true);
        p.transient(beams, 63, true, true);
        p.transient(miningChips,63,false,false,true,true,true);
        bgfx::setViewTransform(64,glm::value_ptr(rotation),glm::value_ptr(projection));bgfx::setViewRect(64,0,0,uint16_t(p.width),uint16_t(p.height));
        p.transient(miningDust,64,false,false,true,false,true);
    }
    if (opticalDepth) {
        for (const auto& planet : nodes)
            if (opticalPlanet && planet.address == opticalPlanet->address) {
                const auto pose = p.poseFor(planet.address, nav);
                const double radiusMetres = std::pow(10., planet.frame.logRadius);
                const double radius = std::pow(10., planet.frame.logRadius - nav.logDistance);
                const auto localEye = glm::inverse(pose.orientation) * (p.eye - pose.position) / radius;
                const double eyeRadius = glm::length(localEye);
                const auto climate = planetClimate(planet);
                if (eyeRadius < 8 && eyeRadius > .99) {
                    const auto rotation = glm::mat4_cast(glm::quat(glm::inverse(pose.orientation) * p.cameraQ));
                    const auto light = glm::normalize(glm::inverse(pose.orientation) * p.lightAt(planet.address, nav));
                    const double height = std::clamp(climate.scaleHeightMetres / radiusMetres, 1e-5, .02);
                    const float strength = atmosphericOpticalStrength(
                        climate.pressureBar, climate.temperatureK, (eyeRadius - 1.) * radiusMetres,
                        radiusMetres, height * radiusMetres, glm::dot(localEye / eyeRadius, light));
                    p.hyperion->atmosphere(glm::vec3(localEye), glm::vec3(light), rotation, float(radius),
                                           float(height), strength, float(p.width) / p.height,
                                           float(std::tan(fov * .5)),
                                           hyperionPhase(planet.planetSeed ? planet.planetSeed : planet.seed),
                                           float(radiusMetres / 1000.),
                                           p.polarLights && (!p.instruments || !p.instruments->shrinking)
                                               ? extractPolarLight(planet)
                                               : PolarLight{},
                                           p.visualTime);
                    ++p.stats.atmospheres;
                }
            }
    }
    p.hyperion->finish();
    if (p.instruments && overlay && !p.probeRemote) {
        const double aspect = double(p.width) / p.height;
        if (p.cabinAspect != aspect) {
            destroy(p.cabin);
            p.cabin = p.upload(cockpitMesh(aspect));
            p.cabinAspect = aspect;
        }
        // Tiny head translation produces parallax inside the cabin only. The
        // universe keeps its precision eye; all world optics share head rotation.
        const auto cabinView = glm::mat4_cast(glm::quat(glm::inverse(p.head.orientation))) *
                               glm::translate(glm::mat4(1), -glm::vec3(p.head.translation));
        const auto cabinProjection = bgfx::getCaps()->homogeneousDepth
                                         ? glm::perspectiveRH_NO(float(fov), float(aspect), .025f, 6.f)
                                         : glm::perspectiveRH_ZO(float(fov), float(aspect), .025f, 6.f);
        bgfx::setViewRect(84, 0, 0, uint16_t(p.width), uint16_t(p.height));
        bgfx::setViewTransform(84, glm::value_ptr(cabinView), glm::value_ptr(cabinProjection));
        bgfx::setViewClear(84, BGFX_CLEAR_DEPTH, 0, 1.f, 0);
        bgfx::setVertexBuffer(0, p.cabin.vertices);
        bgfx::setIndexBuffer(p.cabin.indices);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                       BGFX_STATE_MSAA);
        if (bgfx::getAvailInstanceDataBuffer(1, 80)) {
            bgfx::InstanceDataBuffer instance;
            bgfx::allocInstanceDataBuffer(&instance, 1, 80);
            const Instance identity{glm::mat4(1), glm::vec4(1)};
            std::memcpy(instance.data, &identity, sizeof(identity));
            Geometry source;
            const ScenePresentation presentation{
                source, nav.anchor,  {.38f, .55f, .20f, 0}, hyperionPhase(p.universe.seed()),
                1.f,    {5, 0, 0, 0}};
            const auto sun = glm::inverse(nav.orientation * stationAim) * p.lightAt(nav.anchor, nav);
            p.hyperion->bind(presentation, {}, glm::vec3(sun), glm::mat4(1), 0, p.visualTime);
            bgfx::setInstanceDataBuffer(&instance);
            bgfx::submit(84, p.hyperion->geometryProgram());
            p.stats.uploadBytes += 80;
        } else
            bgfx::submit(84, p.colorProgram);
    } else
        bgfx::setViewClear(84, BGFX_CLEAR_NONE);

    if (p.instruments && overlay) {
        const auto& viewport = p.hudViewport;
        const double aspect = viewport.aspect();
        auto cockpit = *p.instruments;
        if(cockpit.phase!="CAVERN EXPLORATION") {
            cockpit.weather = p.weatherReport;
            cockpit.weatherDetail = p.weatherDetail;
            cockpit.weatherSeverity = p.weatherSeverity;
        }
        if(cockpit.environmentalHeat>.15||cockpit.electricalCharge>.15||cockpit.surfaceScanQuality<.8)
            cockpit.weatherDetail="HEAT "+std::to_string(int(cockpit.environmentalHeat*100))+"% / CHARGE "+
                std::to_string(int(cockpit.electricalCharge*100))+"% / SCAN "+std::to_string(int(cockpit.surfaceScanQuality*100))+"%";
        const auto hud = cockpitOverlay(cockpit, aspect, renderModeName(p.mode), notice, p.contacts,
                                        p.activityContacts, p.cockpitHelp, viewport.projectionScale, p.market,
                                        stationAim * p.head.orientation, p.crew,p.expeditionPlan);
        if (p.validateCockpit) {
            const auto conflicts = cockpitTextConflicts(hud);
            if (!conflicts.empty()) {
                const auto [a, b] = conflicts.front();
                throw std::runtime_error("Native HUD text overlap: " + hud.textRuns[a].value + " / " +
                                         hud.textRuns[b].value);
            }
            for (const auto& run : hud.textRuns)
                if (run.bounds.x < 0 || run.bounds.y < 0 || run.bounds.x + run.bounds.w > 900 * aspect + .01 ||
                    run.bounds.y + run.bounds.h > 900.01)
                    throw std::runtime_error("Native HUD text outside canvas: " + run.value);
        }
        p.cockpitHitRegions = hud.hitRegions;
        p.cockpitContactHits = hud.contactHits;
        maskCockpitMarkers(p.prospectMarkers, viewport, hud);
        const auto projection = bgfx::getCaps()->homogeneousDepth
                                    ? glm::orthoRH_NO(0.f, float(900 * aspect), 900.f, 0.f, -1.f, 1.f)
                                    : glm::orthoRH_ZO(0.f, float(900 * aspect), 900.f, 0.f, -1.f, 1.f);
        bgfx::setViewRect(254, uint16_t(viewport.x), uint16_t(viewport.y), uint16_t(viewport.width),
                          uint16_t(viewport.height));
        bgfx::setViewTransform(254, nullptr, glm::value_ptr(projection));
        bgfx::setViewMode(254, bgfx::ViewMode::Sequential);
        p.transient(hud.panels, 254, false, false, false);
        p.transient(hud.strokes, 254, true, false, false);
        p.transient(hud.lettering, 254, false, false, false);
    }
    auto ortho =
        glm::orthoRH_NO(0.f, static_cast<float>(p.width), static_cast<float>(p.height), 0.f, -1.f, 1.f);
    if (!bgfx::getCaps()->homogeneousDepth)
        ortho =
            glm::orthoRH_ZO(0.f, static_cast<float>(p.width), static_cast<float>(p.height), 0.f, -1.f, 1.f);
    bgfx::setViewTransform(255, nullptr, glm::value_ptr(ortho));
    bgfx::setViewRect(255, 0, 0, static_cast<uint16_t>(p.width), static_cast<uint16_t>(p.height));
    bgfx::setViewTransform(253, nullptr, glm::value_ptr(ortho));
    bgfx::setViewRect(253, 0, 0, static_cast<uint16_t>(p.width), static_cast<uint16_t>(p.height));
    if (!p.instruments)
        p.transient(locators, 255, true, false, false);
    if (overlay && !p.specimen)
        p.transient(p.prospectMarkers, p.cockpitVisible ? 253 : 255, true, false, false);
    bgfx::dbgTextClear();
    if (looking && !p.instruments)
        bgfx::dbgTextPrintf(p.width / 16, p.height / 32, 0x0f, "+");
    if (overlay && !p.instruments) {
        const auto& node = nodes.back();
        auto info = cache.stats();
        int bottom = std::max(10, p.height / 16 - 9);
        bgfx::dbgTextPrintf(3, 2, 0x0f, "I N F I N I T E");

        bgfx::dbgTextPrintf(3, 3, 0x0b, "%s", std::string(renderModeName(p.mode)).c_str());
        bgfx::dbgTextPrintf(3, 4, 0x08, "A DETERMINISTIC UNIVERSE   /   SEED %08llX",
                            static_cast<unsigned long long>(p.universe.seed() & 0xffffffff));
        bgfx::dbgTextPrintf(3, bottom, 0x0f, "%s", node.label().c_str());
        bgfx::dbgTextPrintf(3, bottom + 1, 0x07, "VIEW  10^%+.2f m    OBJECT  10^%+.2f m", nav.logDistance,
                            node.frame.logRadius);
        if (node.address.kind() >= Kind::Planet) {
            const auto& mat = material(node.environment.mineral);
            bgfx::dbgTextPrintf(3, bottom + 2, 0x0b, "%s / %s    %.0f K    %.2f GPa",
                                std::string(mat.name).c_str(), std::string(mat.formula).c_str(),
                                node.environment.temperature, node.environment.pressureGPa);
        } else
            bgfx::dbgTextPrintf(3, bottom + 2, 0x08, "%.1f Gyr    METALS %.0f%%    %zu MARKERS",
                                node.environment.ageGyr, node.environment.chemistry.metal * 100, markers);
        bgfx::dbgTextPrintf(3, bottom + 3, 0x08, "LOC %s", nav.anchor.str().c_str());
        if (nav.selected)
            bgfx::dbgTextPrintf(3, bottom + 4, 0x07, "APPROACH %s TO DESCEND    F FOCUS    BACKSPACE PARENT",
                                std::string(kindName(nav.selected->kind())).c_str());
        else
            bgfx::dbgTextPrintf(3, bottom + 4, 0x07,
                                "MATERIAL LATTICE / STATIC EDUCATIONAL MODEL    BACKSPACE  PARENT");
        bgfx::dbgTextPrintf(3, bottom + 5, 0x08,
                            "WASD FLY   Q/E DOWN/UP   C MOUSE LOOK   APPROACH TO AUTO SCALE   SCROLL SCALE");
        bgfx::dbgTextPrintf(3, bottom + 6, 0x08,
                            "SPEED x%.3g   -/+ ADJUST   SHIFT BOOST   CTRL FINE   CLICK SELECT   ESC %s",
                            nav.speedMultiplier(), looking ? "RELEASE MOUSE" : "QUIT");
        bgfx::dbgTextPrintf(3, bottom + 7, 0x08,
                            "%.0f FPS   GEN %.1f ms   %zu JOBS   CPU CACHE %.1f MiB   GPU %.1f MiB", fps,
                            info.lastMs, info.pending, info.bytes / 1048576., p.gpuBytes / 1048576.);
        if (!p.sectionLabel.empty())
            bgfx::dbgTextPrintf(3, 8, 0x08, "LOCAL MATERIAL SECTION / %s", p.sectionLabel.c_str());
        if (node.address.kind() == Kind::Galaxy)
            bgfx::dbgTextPrintf(3, 10, 0x08, "%s / GAS %.0f%%",
                                std::string(processName(node.galaxy.type)).c_str(),
                                node.galaxy.gasFraction * 100);
        else if (node.address.kind() >= Kind::Rock)
            bgfx::dbgTextPrintf(3, 10, 0x08, "%s / COOLING %.2f / STRAIN %.2f",
                                std::string(processName(node.rock.type)).c_str(),
                                node.environment.coolingRate, node.environment.strain);
        else if (node.geology)
            bgfx::dbgTextPrintf(3, 10, 0x08, "%s / ACTIVITY %.2f",
                                std::string(processName(node.geology->type)).c_str(), node.geology->activity);
        if (node.address.kind() == Kind::Lattice)
            bgfx::dbgTextPrintf(
                3, 12, 0x08, "%s",
                std::string(processName(crystalCell(node.environment.mineral, node.environment).phase))
                    .c_str());
        if (!notice.empty())
            bgfx::dbgTextPrintf(3, 6, 0x0b, "%.*s", static_cast<int>(notice.size()), notice.data());
        if (!info.error.empty())
            bgfx::dbgTextPrintf(3, 7, 0x0c, "GENERATION ERROR: %s", info.error.c_str());
    }
    cache.finishFrame();
    p.prune();
    p.stats.cpuMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    bgfx::frame();
    p.stats.submitMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() -
        p.stats.cpuMs;
    const auto* st = bgfx::getStats();
    p.stats.gpuBytes = p.gpuBytes + p.craftBytes + p.surfaceMeshBytes + p.probeRigBytes;
    p.stats.effectBytes = p.hyperion->targetBytes();
    p.stats.shadowBytes = p.hyperion->sunBytes();
    p.stats.shadowUpdates = p.hyperion->sunUpdated();
    p.stats.shadowActive = p.hyperion->sunActive();
    p.stats.materialBytes = p.hyperion->materialBytes();
    p.stats.weatherPending = p.hyperion->weatherPending();
    p.stats.weatherCacheBytes = p.hyperion->weatherStats().bytes;
    p.stats.weatherGpuBytes = p.hyperion->weatherGpuBytes();
    p.stats.drawCalls = st->numDraw;
    p.stats.gpuMs = st->gpuTimerFreq
                        ? 1000. * static_cast<double>(st->gpuTimeEnd - st->gpuTimeBegin) / st->gpuTimerFreq
                        : 0;
    return p.stats;
}
std::optional<Address> Renderer::pick(double x, double y) const {
    const auto& p = *impl_;
    if (p.cockpitVisible && (p.cockpitHelp || (p.expeditionPlan&&p.expeditionPlan->open) || (p.market && p.market->open) || (p.crew && p.crew->open) ||
                             p.instruments->phase == "MINING PROBE CAMERA"))
        return {};
    if(p.cockpitVisible){const auto point=p.hudViewport.toCanvas({x,y});
        for(const auto& hit:p.cockpitContactHits)if(hit.bounds.contains(point)){
            if(hit.life)return nextLivingWorld(p.contacts,p.lifeSelection);
            return hit.world;
        }
    }
    if (p.cockpitVisible &&
        std::any_of(p.cockpitHitRegions.begin(), p.cockpitHitRegions.end(),
                    [&](const auto& r) { return r.contains(p.hudViewport.toCanvas({x, y})); }))
        return {};
    const Pick* chosen = nullptr;
    double best = 40 * 40;
    for (auto& candidate : p.picks) {
        double distance = glm::dot(candidate.screen - glm::dvec2(x, y), candidate.screen - glm::dvec2(x, y));
        if (distance < best) {
            best = distance;
            chosen = &candidate;
        }
    }
    return chosen ? std::optional<Address>(chosen->address) : std::optional<Address>{};
}
void Renderer::screenshot(const std::string& file) {
    bgfx::requestScreenShot(BGFX_INVALID_HANDLE, file.c_str());
}
std::string Renderer::backend() const {
    return bgfx::getRendererName(bgfx::getRendererType());
}
} // namespace infinity
