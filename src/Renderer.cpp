#include "infinite/Renderer.hpp"
#include "HyperionRenderer.hpp"
#include "infinite/Cockpit.hpp"
#include "infinite/Economy.hpp"
#include "infinite/StellarEvolution.hpp"
#include "infinite/Streaming.hpp"
#include "infinite/SurfaceShading.hpp"
#include "infinite/WorldDepth.hpp"
#include <InstanceShaders.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <algorithm>
#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <chrono>
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
#include <unordered_set>
#include <vs_debugdraw_lines.bin.h>

namespace infinite {
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
    bgfx::ProgramHandle colorProgram = BGFX_INVALID_HANDLE, instanceProgram = BGFX_INVALID_HANDLE;
    bgfx::SwapChain swapChain;
    uint32_t resetFlags = BGFX_RESET_VSYNC | BGFX_RESET_MAXANISOTROPY;
    std::array<GPUMesh, 4> atomSpheres;
    GPUMesh billboardMesh, hyperionBillboardMesh;
    GPUMesh cabin;
    double cabinAspect = 0;
    std::optional<FlightInstruments> instruments;
    bool cockpitHelp = false, cockpitVisible = false;
    std::vector<CockpitRect> cockpitHitRegions;
    CockpitViewport hudViewport;
    bool polarLights = true;
    bool cloudVolumes = true;
    bool stellarOptics = true;
    bool landingShade = true;
    bool weatherEffects = true;
    bool planetRings = true;
    float stellarExposure = 1;
    std::shared_ptr<const Geometry> skyGeometry;
    std::vector<CelestialContact> contacts;
    std::optional<Address> lifeSelection;
    std::vector<CelestialContact> skyOccluders;
    const ActivityState* activity = nullptr;
    std::shared_ptr<const EconomySnapshot> economy;
    const MarketView* market = nullptr;
    std::vector<ActivityContact> activityContacts;
    std::vector<std::pair<ActivityId, glm::dvec2>> activityPicks;
    std::array<std::array<GPUMesh, 3>, 6> craftMeshes;
    size_t craftBytes = 0;
    SurfaceLifeCache surfaceLife;
    std::array<std::array<GPUMesh, 3>, size_t(SurfaceMesh::Count)> surfaceMeshes;
    size_t surfaceMeshBytes = 0;
    std::string weatherReport;
    std::string weatherDetail;
    Mesh weatherRain, weatherFlakes;
    double weatherSeverity = 0, weatherPriority = 1e100;
    std::unique_ptr<HyperionRenderer> hyperion;
    RenderMode mode = RenderMode::Original;
    double visualTime = 0, fixedVisualTime = -1;
    bgfx::ViewId atomView = 2;
    std::map<const Geometry*, GPUGeometry> gpu;
    std::vector<Pick> picks;
    std::vector<Instance> billboards;
    std::vector<Instance> cosmicBillboards;
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
                if (bgfx::isValid(g.normals))
                    bgfx::destroy(g.normals);
                if (bgfx::isValid(g.instances))
                    bgfx::destroy(g.instances);
            }
            for (auto& mesh : atomSpheres)
                destroy(mesh);
            destroy(billboardMesh);
            destroy(cabin);
            destroy(hyperionBillboardMesh);
            for (auto& role : craftMeshes)
                for (auto& mesh : role)
                    destroy(mesh);
            for (auto& role : surfaceMeshes)
                for (auto& mesh : role)
                    destroy(mesh);
            hyperion.reset();
            if (bgfx::isValid(colorProgram))
                bgfx::destroy(colorProgram);
            if (bgfx::isValid(instanceProgram))
                bgfx::destroy(instanceProgram);
            bgfx::shutdown();
        }
    }
    GPUMesh upload(const Mesh& m) {
        GPUMesh result;
        if (m.vertices.empty() || m.indexCount() == 0)
            return result;
        stats.uploadBytes += m.bytes();
        staticUploadBytes += m.bytes();
        result.vertices = bgfx::createVertexBuffer(
            bgfx::copy(m.vertices.data(), static_cast<uint32_t>(m.vertices.size() * sizeof(Vertex))), layout);
        result.indices = bgfx::createIndexBuffer(
            bgfx::copy(m.indexData(), static_cast<uint32_t>(m.indexCount() * m.indexWidth())),
            m.wideIndices() ? BGFX_BUFFER_INDEX32 : 0);
        result.count = m.indexCount();
        return result;
    }
    bgfx::ShaderHandle loadShader(const std::string& name) {
        bool vertex = name == "vs_instancing";
#define INSTANCE_SHADER(BACKEND)                                                                             \
    return vertex ? bgfx::createShader(bgfx::copy(vs_instance_##BACKEND, sizeof(vs_instance_##BACKEND)))     \
                  : bgfx::createShader(bgfx::copy(fs_instance_##BACKEND, sizeof(fs_instance_##BACKEND)))
        switch (bgfx::getRendererType()) {
        case bgfx::RendererType::Metal:
            INSTANCE_SHADER(metal);
        case bgfx::RendererType::Direct3D11:
            INSTANCE_SHADER(dxbc);
        case bgfx::RendererType::Vulkan:
            INSTANCE_SHADER(spirv);
        case bgfx::RendererType::OpenGLES:
            INSTANCE_SHADER(essl);
        case bgfx::RendererType::OpenGL:
            INSTANCE_SHADER(glsl);
        default:
            throw std::runtime_error("Unsupported renderer for instancing shaders");
        }
#undef INSTANCE_SHADER
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
        if (background && mode != RenderMode::Original)
            color *= stellarExposure;
        auto right = cameraQ * glm::dvec3(radius, 0, 0), up = cameraQ * glm::dvec3(0, radius, 0);
        glm::mat4 transform(1);
        transform[0] = glm::vec4(glm::vec3(right), 0);
        transform[1] = glm::vec4(glm::vec3(up), 0);
        transform[3] = glm::vec4(glm::vec3(p), 1);
        (mode != RenderMode::Original && opticalMaterial >= 3 ? stellarBillboards
         : cosmic                                             ? cosmicBillboards
                                                              : billboards)
            .push_back(
                {transform, glm::vec4(color, mode == RenderMode::Original ? 1.f : 1.f + opticalMaterial)});
    }
    bool prepareGPU(const std::shared_ptr<const Geometry>& geometry) {
        if (!geometry)
            return false;
        auto it = gpu.find(geometry.get());
        if (it != gpu.end() && it->second.source.expired()) {
            gpuBytes -= it->second.bytes;
            destroy(it->second.solid);
            destroy(it->second.lines);
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
        item.solid = upload(geometry->solid);
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
        if (mode == RenderMode::Original || bgfx::isValid(item.normals) || !count || count > 65536 ||
            geometry->address.kind() >= Kind::Grain)
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
                  bgfx::VertexBufferHandle normals = BGFX_INVALID_HANDLE, bool stellar = false) {
        if (!bgfx::isValid(m.vertices) || !bgfx::isValid(m.indices) ||
            bgfx::getAvailInstanceDataBuffer(1, 80) < 1)
            return;
        bgfx::InstanceDataBuffer idb;
        bgfx::allocInstanceDataBuffer(&idb, 1, 80);
        Instance item{model, {1, 1, 1, opacity}};
        std::memcpy(idb.data, &item, sizeof(item));
        bgfx::setVertexBuffer(0, m.vertices);
        bgfx::setIndexBuffer(m.indices);
        const bool smooth = mode != RenderMode::Original && bgfx::isValid(normals);
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
        bgfx::submit(worldView(), mode == RenderMode::Original ? instanceProgram
                                                               : hyperion->geometryProgram(smooth, stellar));
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
            if (mode != RenderMode::Original && node.address.kind() >= Kind::Planet) {
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
        if (instruments || (mode != RenderMode::Original && material.kind() >= Kind::Planet &&
                            material.kind() <= Kind::Mountain)) {
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
        if (mode != RenderMode::Original && bgfx::isValid(item.solid.vertices))
            bindPresentation(*geometry, node, nav, model, pose, r, bgfx::isValid(item.normals) ? .25f : 0.f);
        drawMesh(item.solid, model, opacity, false, item.normals, node.address.kind() == Kind::System);
        if (mode != RenderMode::Original && bgfx::isValid(item.lines.vertices))
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
                                 glm::dot(particle.position, particle.position) < 1e-24 &&
                                 mode != RenderMode::Original;
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
            unsigned level = atomPresentationLevel(diameter, mode == RenderMode::Original);
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
            if (mode != RenderMode::Original)
                bindPresentation(*geometry, node, nav, model, pose, r, 1);
            bgfx::submit(viewId,
                         mode == RenderMode::Original ? instanceProgram : hyperion->geometryProgram());
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
    void renderActivity(const std::vector<Node>& nodes, const Navigation& nav, RegionCache& cache) {
        activityContacts.clear();
        activityPicks.clear();
        stats.activityBytes = craftBytes;
        renderSurfaceEnvironment(nodes, nav, cache);
        if (!activity || !activity->enabled || nav.anchor.kind() >= Kind::Rock)
            return;
        std::array<std::array<std::vector<Instance>, 3>, 6> batches;
        // Like the terrain pass, subtract the double-precision eye before
        // uploading floats. Derivative normals then remain stable near a hull.
        const auto rotationView = glm::mat4_cast(glm::quat(cameraInverse));
        const glm::mat4 actorProjection(projection);
        bgfx::setViewTransform(62, glm::value_ptr(rotationView), glm::value_ptr(actorProjection));
        bgfx::setViewRect(62, 0, 0, uint16_t(width), uint16_t(height));
        Mesh exhaust;
        const double units = std::pow(10., nav.logDistance);
        for (const auto& system : nodes) {
            if (system.address.kind() != Kind::System)
                continue;
            const auto planets = cache.catalog(system);
            if (!planets)
                continue;
            for (const auto& planet : *planets) {
                if (isMoon(planet.address))
                    continue;
                const auto pose = poseFor(planet.address, nav);
                const auto planetEye = universe.pointRelative(
                    nav.anchor, eye * std::pow(10., nav.logDistance - anchorFrames.back().logRadius),
                    planet.address, 0);
                const bool pinned = activity->selected && activity->selected->home == planet.address;
                const double localRange = glm::length(planetEye);
                const bool local = localRange < std::max(2e7, std::pow(10., planet.frame.logRadius) * 6);
                auto bodies = local || pinned
                                  ? sampleActivity(planet, activity->seconds)
                                  : std::vector<ActivityBody>{sampleActivity(planet, 0, activity->seconds),
                                                              sampleActivity(planet, 1, activity->seconds)};
                if (economy && economy->network && economy->network->system == system.address && !local &&
                    !pinned)
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
                for (const auto& body : bodies) {
                    ++stats.activityBodies;
                    const auto deltaSI = pose.orientation * (body.position - planetEye);
                    const double distance = glm::length(deltaSI);
                    if (distance <= 1e-12)
                        continue;
                    const auto direction = cameraInverse * (deltaSI / distance);
                    const bool selected = activity->selected && *activity->selected == body.id;
                    const bool occulted = celestialOccluded(direction, distance, skyOccluders);
                    if (body.kind != ActivityKind::Debris) {
                        const auto velocity = cameraInverse * (pose.orientation * body.velocity);
                        activityContacts.push_back({body, direction, distance,
                                                    instruments
                                                        ? glm::length(velocity - instruments->localVelocity)
                                                        : glm::length(velocity),
                                                    selected, occulted});
                    }
                    if (direction.z >= 0 || occulted ||
                        (distance > 2e6 && body.kind != ActivityKind::Harbour && !selected))
                        continue;
                    const double angular = body.radius / distance;
                    if (!LOD::visible(direction * 500., std::max(.5, angular * 500.), double(width) / height,
                                      fieldOfView))
                        continue;
                    const auto point = eye + deltaSI / units;
                    const auto shell = eye + nav.orientation * direction * 500.;
                    if (body.kind != ActivityKind::Debris)
                        if (auto screen = project(shell))
                            activityPicks.emplace_back(body.id, *screen);
                    const double pixels = angular * height / std::tan(fieldOfView * .5);
                    const auto role = size_t(body.kind);
                    const unsigned detail = pixels > 48 ? 2 : pixels > 8 ? 1 : 0;
                    if (pixels > 1.6 && -cameraSpace(point).z > .00005 && distance / units < 8000) {
                        auto model = glm::translate(glm::dmat4(1), deltaSI / units) *
                                     glm::mat4_cast(pose.orientation * body.orientation) *
                                     glm::scale(glm::dmat4(1), glm::dvec3(body.radius / units));
                        batches[role][detail].push_back({glm::mat4(model), glm::vec4(body.tint, 1)});
                    } else {
                        const double footprint =
                            std::max(angular, 2 * std::tan(fieldOfView * .5) / height * .85);
                        addBillboard(shell, 500 * footprint,
                                     body.tint * (body.kind == ActivityKind::Debris ? .25f : .75f), true);
                    }
                    ++stats.activityVisible;
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
                        const auto source = activityMesh(ActivityKind(role), detail);
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
                    if (mode != RenderMode::Original) {
                        Geometry source;
                        const ScenePresentation presentation{source,
                                                             system.address,
                                                             {.43, .65, .035, .01},
                                                             float(role) / 6,
                                                             mode == RenderMode::Hybrid ? .36f : 1.f};
                        hyperion->bind(presentation, {}, glm::vec3(lightAt(system.address, nav)),
                                       glm::mat4(1), 0, activity->seconds);
                    }
                    bgfx::submit(62, mode == RenderMode::Original ? instanceProgram
                                                                  : hyperion->geometryProgram());
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
        transient(exhaust, worldView(), true, true);
        stats.activityBytes = craftBytes;
    }
    void renderNode(const Node& node, const Navigation& nav, RegionCache& cache, unsigned maximum = 4) {
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
        const bool streamedTerrain =
            terrainActive && streamedTerrainCovers(node.address, nav.anchor, mode == RenderMode::Original);
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
    void renderTerrain(const Node& planet, const Navigation& nav) {
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
        if (landingShade && mode != RenderMode::Original && instruments && !instruments->shrinking &&
            nav.anchor.kind() < Kind::Rock && instruments->altitude >= 0 &&
            instruments->altitude < instruments->hullMetres / 12. * 38.) {
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
        if (sunShadows && mode != RenderMode::Original && instruments && !instruments->shrinking &&
            nav.anchor.kind() < Kind::Rock && std::pow(10., nav.logDistance) > .01) {
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
                if (terrain.morphing())
                    key = mix64(key ^ frame);
                updateSun = hyperion->prepareSun(sun, key);
                terrainSunPlanet = planet.address;
            }
        }
        for (const auto& patch : terrain.patches()) {
            const auto& g = *patch.geometry;
            std::array<glm::dvec3, 81> points;
            glm::dvec3 centre{};
            for (unsigned i = 0; i < points.size(); ++i) {
                points[i] = transform.linear * (terrain.position(patch, i) - transform.pivot) +
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
            const bool smoothTerrain = mode != RenderMode::Original && g.terrainNormals.size() == 81;
            const uint32_t count = uint32_t(g.solid.indexCount());
            if ((smoothTerrain && bgfx::getAvailTransientVertexBuffer(81, normalLayout) < 81) ||
                bgfx::getAvailTransientVertexBuffer(81, layout) < 81 ||
                bgfx::getAvailTransientIndexBuffer(count, false) < count ||
                bgfx::getAvailInstanceDataBuffer(1, 80) < 1) {
                ++stats.uploadDeferred;
                if (caster)
                    terrainSunComplete = false;
                continue;
            }
            bgfx::TransientVertexBuffer vertices, normals;
            bgfx::TransientIndexBuffer indices;
            bgfx::InstanceDataBuffer instances;
            bgfx::allocTransientVertexBuffer(&vertices, 81, layout);
            bgfx::allocTransientIndexBuffer(&indices, count, false);
            bgfx::allocInstanceDataBuffer(&instances, 1, 80);
            auto data = reinterpret_cast<Vertex*>(vertices.data);
            for (unsigned i = 0; i < points.size(); ++i)
                data[i] = {float(points[i].x), float(points[i].y), float(points[i].z),
                           smoothTerrain ? g.terrainAlbedo[i] : g.solid.vertices[i].color};
            if (smoothTerrain) {
                bgfx::allocTransientVertexBuffer(&normals, 81, normalLayout);
                auto output = reinterpret_cast<ShadingNormal*>(normals.data);
                for (unsigned i = 0; i < 81; ++i)
                    output[i] = packNormal(glm::vec3(
                        glm::normalize(glm::dmat3(transform.linear) * glm::dvec3(g.terrainNormals[i]))));
            }
            std::memcpy(indices.data, g.solid.indexData(), count * 2);
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
                81 * sizeof(Vertex) + count * 2 + (smoothTerrain ? 81 * sizeof(ShadingNormal) : 0);
            if (!visible)
                continue;
            auto presentation = extractPresentation(planet, g, mode);
            const auto light = lightAt(planet.address, nav);
            glm::vec4 cavity{};
            if (nav.anchor.kind() >= Kind::Rock)
                cavity = glm::vec4(glm::vec3(-eye), float(std::max(1.3, glm::length(eye) * 1.1)));
            glm::mat4 micro(0), meso(0);
            float relief = 0, mesoRelief = 0;
            if (mode != RenderMode::Original) {
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
            stats.vertices += 81;
            stats.triangles += count / 3;
        }
        if (updateSun)
            hyperion->completeSun(terrainSunComplete);
        if (terrain.morphing())
            ++stats.lodTransitions;
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
    p.instanceProgram =
        bgfx::createProgram(p.loadShader("vs_instancing"), p.loadShader("fs_instancing"), true);
    if (!bgfx::isValid(p.colorProgram) || !bgfx::isValid(p.instanceProgram))
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
    p.billboardMesh = p.upload(quad);
    for (auto& vertex : quad.vertices)
        vertex.color = rgba({1, 1, 1});
    p.hyperionBillboardMesh = p.upload(quad);
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
    impl_->mode = mode;
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
    else
        impl_->instruments.reset();
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
void Renderer::setActivity(const ActivityState* state) {
    impl_->activity = state;
}
std::optional<ActivityId> Renderer::pickActivity(double x, double y) const {
    const auto& p = *impl_;
    if (p.cockpitVisible && p.cockpitHelp)
        return {};
    if (!p.activity || !p.activity->enabled)
        return {};
    if (p.cockpitVisible && (!p.instruments->shrinking || p.instruments->hullMetres >= .001)) {
        const auto point = p.hudViewport.toCanvas({x, y});
        const auto rows = cockpitActivityRows(p.activityContacts);
        if (auto row = CockpitLayout(p.hudViewport.aspect()).trafficRow(point, rows.size()))
            return p.activityContacts[rows[*row]].body.id;
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
    p.sectionLabel.clear();
    p.stats = {};
    p.surfaceLife.beginFrame();
    p.weatherReport.clear();
    p.weatherDetail.clear();
    p.weatherSeverity = 0;
    p.weatherPriority = 1e100;
    p.staticUploadBytes = 0;
    p.picks.clear();
    p.billboards.clear();
    p.cosmicBillboards.clear();
    p.stellarBillboards.clear();
    p.frameFrames.clear();
    p.framePoses.clear();
    p.cockpitVisible = p.instruments.has_value() && overlay;
    p.hudViewport = p.cockpitViewport();
    p.cockpitHitRegions.clear();
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
    cache.beginFrame();
    p.cameraQ = nav.orientation;
    p.cameraInverse = glm::inverse(p.cameraQ);
    p.eye = nav.cameraPosition();
    p.view = glm::mat4_cast(glm::inverse(nav.orientation)) * glm::translate(glm::dmat4(1), -p.eye);
    const double fov = p.instruments ? 70. * std::numbers::pi / 180. : 1.;
    p.fieldOfView = fov;
    p.projection =
        worldProjection(fov, static_cast<double>(p.width) / p.height, bgfx::getCaps()->homogeneousDepth);
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
    if (p.mode != RenderMode::Original && nav.anchor.kind() < Kind::Rock &&
        (!p.instruments || !p.instruments->shrinking)) {
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
    bgfx::setViewMode(0,
                      p.mode == RenderMode::Original ? bgfx::ViewMode::Default : bgfx::ViewMode::Sequential);
    bgfx::setViewTransform(0, glm::value_ptr(v), glm::value_ptr(proj));
    bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(p.width), static_cast<uint16_t>(p.height));
    p.stellarExposure = 1;
    if (p.mode != RenderMode::Original) {
        auto sky = extractSpectralSky(p.universe, nav);
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
                    p.stellarExposure *=
                        p.mode == RenderMode::Hybrid ? glm::mix(1.f, transmission, .36f) : transmission;
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
                p.contacts = extractCelestialContacts(p.universe, nav, system, *planets);
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
    if (p.mode != RenderMode::Original && p.instruments && !p.instruments->shrinking && p.stellarOptics)
        p.hyperion->stellar(extractStellarImage(p.universe, nav, p.contacts), float(p.width) / p.height,
                            float(std::tan(fov * .5)), p.visualTime);
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
    for (const auto& node : nodes)
        if (node.address.kind() == Kind::Galaxy)
            p.stellarSky(node, nav, cache);
    for (const auto& contact : p.contacts) {
        const auto direction = contact.localDirection;
        if (direction.z >= 0 || contact.occluded)
            continue;
        if (contact.address.kind() == Kind::Planet && contact.angularRadius >= .008 &&
            !rendered.contains(contact.address)) {
            p.renderNode(p.universe.describe(contact.address), nav, cache);
            rendered.insert(contact.address);
        }
        const auto point = p.eye + nav.orientation * direction * 500.;
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
    for (const auto& address : rootNeighborhood(root)) {
        auto node = p.universe.describe(address);
        p.galaxySky(node, nav, cache,
                    celestialCellWeight(cellDisplacement(address.cell, root.cell), rootEye));
        if (!rendered.contains(address)) {
            p.renderNode(node, nav, cache, 2);
            rendered.insert(address);
        }
        if (auto boundary = cache.requestCosmicBoundary(node))
            p.drawGeometry(boundary, node, nav, 1);
        if (auto it = p.details.find(address); it != p.details.end() && it->second.blend.current)
            ++p.stats.rootCells;
        const auto centre = cellDisplacement(address.cell, root.cell);
        if (glm::length(centre - rootEye) < 1.8)
            rootNodes.push_back(std::move(node));
    }
    if (terrainPlanet)
        p.renderTerrain(*terrainPlanet, nav);
    Mesh locators;
    auto candidates = [&](const Node& parent) {
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
    for (const auto& a : rendered)
        if (a.kind() == Kind::Planet)
            p.drawPlanetRings(p.universe.describe(a), nav);
    p.renderActivity(nodes, nav, cache);
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
    auto drawBillboards = [&](const std::vector<Instance>& instances, bool cosmic, bool stellar = false) {
        uint32_t count = bgfx::getAvailInstanceDataBuffer(static_cast<uint32_t>(instances.size()), 80);
        if (count) {
            bgfx::InstanceDataBuffer buffer;
            bgfx::allocInstanceDataBuffer(&buffer, count, 80);
            std::memcpy(buffer.data, instances.data(), count * sizeof(Instance));
            const auto& mesh = p.mode == RenderMode::Original ? p.billboardMesh : p.hyperionBillboardMesh;
            bgfx::setVertexBuffer(0, mesh.vertices);
            bgfx::setIndexBuffer(mesh.indices);
            bgfx::setInstanceDataBuffer(&buffer);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_MSAA | BGFX_STATE_DEPTH_TEST_GEQUAL |
                           BGFX_STATE_BLEND_ADD);
            if (p.mode != RenderMode::Original) {
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
            bgfx::submit(cosmic && p.instruments ? 0 : p.worldView(), p.mode == RenderMode::Original
                                                                          ? p.instanceProgram
                                                                      : stellar ? p.hyperion->coronaProgram()
                                                                                : p.hyperion->glowProgram());
        }
    };
    drawBillboards(p.cosmicBillboards, true);
    drawBillboards(p.billboards, false);
    drawBillboards(p.stellarBillboards, false, true);
    if (p.instruments && p.mode != RenderMode::Original && !p.instruments->shrinking &&
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
    if (opticalDepth && p.mode != RenderMode::Original) {
        for (const auto& planet : nodes)
            if (opticalPlanet && planet.address == opticalPlanet->address) {
                const auto pose = p.poseFor(planet.address, nav);
                const double radius = std::pow(10., planet.frame.logRadius - nav.logDistance);
                const auto localEye = glm::inverse(pose.orientation) * (p.eye - pose.position) / radius;
                // The optical pass consumes the same retained atmosphere as weather.
                const auto climate = planetClimate(planet);
                const float strength = float(
                    std::clamp(climate.pressureBar * 288. / std::max(60., climate.temperatureK), 0., 6.));
                if (glm::length(localEye) < 8 && glm::length(localEye) > .99 && strength > .01f) {
                    auto rotation =
                        glm::mat4_cast(glm::quat(glm::inverse(pose.orientation) * nav.orientation));
                    auto light = glm::inverse(pose.orientation) * p.lightAt(planet.address, nav);
                    const float height = float(std::clamp(
                        climate.scaleHeightMetres / std::pow(10., planet.frame.logRadius), 1e-5, .02));
                    p.hyperion->atmosphere(glm::vec3(localEye), glm::vec3(light), rotation, float(radius),
                                           height, strength, float(p.width) / p.height,
                                           float(std::tan(fov * .5)),
                                           hyperionPhase(planet.planetSeed ? planet.planetSeed : planet.seed),
                                           float(std::pow(10., planet.frame.logRadius) / 1000.),
                                           p.polarLights && (!p.instruments || !p.instruments->shrinking)
                                               ? extractPolarLight(planet)
                                               : PolarLight{},
                                           p.visualTime);
                    ++p.stats.atmospheres;
                }
            }
    }
    p.hyperion->finish();
    if (p.instruments && overlay) {
        const double aspect = double(p.width) / p.height;
        if (p.cabinAspect != aspect) {
            destroy(p.cabin);
            p.cabin = p.upload(cockpitMesh(aspect));
            p.cabinAspect = aspect;
        }
        const glm::mat4 cabinView(1);
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
        if (p.mode != RenderMode::Original && bgfx::getAvailInstanceDataBuffer(1, 80)) {
            bgfx::InstanceDataBuffer instance;
            bgfx::allocInstanceDataBuffer(&instance, 1, 80);
            const Instance identity{glm::mat4(1), glm::vec4(1)};
            std::memcpy(instance.data, &identity, sizeof(identity));
            Geometry source;
            const ScenePresentation presentation{source,
                                                 nav.anchor,
                                                 {.38f, .55f, .20f, 0},
                                                 hyperionPhase(p.universe.seed()),
                                                 p.mode == RenderMode::Hybrid ? .35f : 1.f,
                                                 {5, 0, 0, 0}};
            const auto sun = p.cameraInverse * p.lightAt(nav.anchor, nav);
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
        cockpit.weather = p.weatherReport;
        cockpit.weatherDetail = p.weatherDetail;
        cockpit.weatherSeverity = p.weatherSeverity;
        const auto hud =
            cockpitOverlay(cockpit, aspect, renderModeName(p.mode), notice, p.contacts, p.activityContacts,
                           p.cockpitHelp, viewport.projectionScale, p.market);
        p.cockpitHitRegions = hud.hitRegions;
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
    if (!p.instruments)
        p.transient(locators, 255, true, false, false);
    bgfx::dbgTextClear();
    if (looking && !p.instruments)
        bgfx::dbgTextPrintf(p.width / 16, p.height / 32, 0x0f, "+");
    if (overlay && !p.instruments) {
        const auto& node = nodes.back();
        auto info = cache.stats();
        int bottom = std::max(10, p.height / 16 - 9);
        bgfx::dbgTextPrintf(3, 2, 0x0f, "I N F I N I T E");
        if (p.mode != RenderMode::Original)
            bgfx::dbgTextPrintf(3, 3, 0x0b, "%s / H RENDER MODE",
                                std::string(renderModeName(p.mode)).c_str());
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
    p.stats.gpuBytes = p.gpuBytes + p.craftBytes + p.surfaceMeshBytes;
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
    if (p.cockpitVisible && p.cockpitHelp)
        return {};
    if (p.cockpitVisible && (!p.instruments->shrinking || p.instruments->hullMetres >= .001)) {
        const auto point = p.hudViewport.toCanvas({x, y});
        const CockpitLayout layout(p.hudViewport.aspect());
        if (!p.contacts.empty() && layout.life.contains(point)) {
            return nextLivingWorld(p.contacts, p.lifeSelection);
        }
        if (auto row = layout.celestialRow(point, p.contacts.size()))
            return p.contacts[cockpitCelestialRows(p.contacts)[*row]].address;
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
} // namespace infinite
