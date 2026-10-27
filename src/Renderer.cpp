#include "infinite/Renderer.hpp"
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
};
static void destroy(GPUMesh& m) {
    if (bgfx::isValid(m.vertices))
        bgfx::destroy(m.vertices);
    if (bgfx::isValid(m.indices))
        bgfx::destroy(m.indices);
    m = {};
}
struct GPUGeometry {
    GPUMesh solid, lines;
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
    bgfx::VertexLayout layout, instanceLayout;
    bgfx::ProgramHandle colorProgram = BGFX_INVALID_HANDLE, instanceProgram = BGFX_INVALID_HANDLE;
    bgfx::SwapChain swapChain;
    std::array<GPUMesh, 3> atomSpheres;
    GPUMesh billboardMesh;
    bgfx::ViewId atomView = 2;
    std::map<const Geometry*, GPUGeometry> gpu;
    std::vector<Pick> picks;
    std::vector<Instance> billboards;
    std::vector<LocalFrame> anchorFrames;
    std::map<Address, std::vector<LocalFrame>> frameFrames;
    std::map<Address, Pose> framePoses;
    int width = 1280, height = 800;
    uint64_t frame = 0;
    size_t gpuBytes = 0;
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
    double frameSeconds = 0;
    std::string sectionLabel;
    explicit Impl(SDL_Window* w, const Universe& u) : universe(u), window(w) {}
    ~Impl() {
        if (initialized) {
            for (auto& [key, g] : gpu) {
                destroy(g.solid);
                destroy(g.lines);
                if (bgfx::isValid(g.instances))
                    bgfx::destroy(g.instances);
            }
            for (auto& mesh : atomSpheres)
                destroy(mesh);
            destroy(billboardMesh);
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
        result.vertices = bgfx::createVertexBuffer(
            bgfx::copy(m.vertices.data(), static_cast<uint32_t>(m.vertices.size() * sizeof(Vertex))), layout);
        result.indices = bgfx::createIndexBuffer(
            bgfx::copy(m.indexData(), static_cast<uint32_t>(m.indexCount() * m.indexWidth())),
            m.wideIndices() ? BGFX_BUFFER_INDEX32 : 0);
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
    void addBillboard(glm::dvec3 p, double radius, glm::vec3 color) {
        ++stats.particles;
        auto right = cameraQ * glm::dvec3(radius, 0, 0), up = cameraQ * glm::dvec3(0, radius, 0);
        glm::mat4 transform(1);
        transform[0] = glm::vec4(glm::vec3(right), 0);
        transform[1] = glm::vec4(glm::vec3(up), 0);
        transform[3] = glm::vec4(glm::vec3(p), 1);
        billboards.push_back({transform, glm::vec4(color, 1)});
    }
    bool prepareGPU(const std::shared_ptr<const Geometry>& geometry) {
        if (!geometry)
            return false;
        auto it = gpu.find(geometry.get());
        if (it != gpu.end() && it->second.source.expired()) {
            gpuBytes -= it->second.bytes;
            destroy(it->second.solid);
            destroy(it->second.lines);
            if (bgfx::isValid(it->second.instances))
                bgfx::destroy(it->second.instances);
            gpu.erase(it);
            it = gpu.end();
        }
        if (it != gpu.end())
            return true;
        size_t bytes = geometry->solid.bytes() + geometry->lines.bytes() +
                       geometry->atomInstances.size() * sizeof(Instance);
        if (stats.uploadBytes && stats.uploadBytes + bytes > 2 * 1024 * 1024) {
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
        }
        item.source = geometry;
        item.bytes = bytes;
        gpuBytes += bytes;
        gpu.emplace(geometry.get(), std::move(item));
        return true;
    }
    void drawMesh(const GPUMesh& m, const glm::mat4& model, float opacity = 1, bool lines = false) {
        if (!bgfx::isValid(m.vertices) || !bgfx::isValid(m.indices) ||
            bgfx::getAvailInstanceDataBuffer(1, 80) < 1)
            return;
        bgfx::InstanceDataBuffer idb;
        bgfx::allocInstanceDataBuffer(&idb, 1, 80);
        Instance item{model, {1, 1, 1, opacity}};
        std::memcpy(idb.data, &item, sizeof(item));
        bgfx::setVertexBuffer(0, m.vertices);
        bgfx::setIndexBuffer(m.indices);
        bgfx::setInstanceDataBuffer(&idb);
        uint64_t state =
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;
        if (lines)
            state |= BGFX_STATE_PT_LINES;
        if (opacity > .99f)
            state |= BGFX_STATE_WRITE_Z;
        else
            state |= BGFX_STATE_BLEND_ALPHA;
        bgfx::setState(state);
        bgfx::submit(0, instanceProgram);
    }
    void drawLines(const GPUMesh& m, const glm::mat4& model) {
        if (!bgfx::isValid(m.vertices))
            return;
        bgfx::setTransform(glm::value_ptr(model));
        bgfx::setVertexBuffer(0, m.vertices);
        bgfx::setIndexBuffer(m.indices);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_PT_LINES | BGFX_STATE_DEPTH_TEST_LEQUAL |
                       BGFX_STATE_MSAA);
        bgfx::submit(0, colorProgram);
    }
    void transient(const Mesh& mesh, bgfx::ViewId viewId, bool lines, bool additive) {
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
            state |= BGFX_STATE_PT_LINES;
        else
            state |= BGFX_STATE_DEPTH_TEST_LEQUAL;
        if (additive)
            state |= BGFX_STATE_BLEND_ADD;
        bgfx::setState(state);
        bgfx::submit(viewId, colorProgram);
    }
    void drawGeometry(const std::shared_ptr<const Geometry>& geometry, const Navigation& nav, float opacity) {
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
        stats.vertices += geometry->solid.vertices.size();
        stats.triangles += geometry->solid.indexCount() / 3;
        auto model = glm::translate(glm::mat4(1), glm::vec3(pose.position)) *
                     glm::mat4_cast(glm::quat(pose.orientation)) *
                     glm::scale(glm::mat4(1), glm::vec3(static_cast<float>(r)));
        drawMesh(item.solid, model, opacity);
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
            auto p = cameraSpace(position);
            if (p.z >= 0)
                continue;
            double size = std::clamp(static_cast<double>(particle.radius) * r, -p.z * .0005, -p.z * .035);
            if (LOD::visible(p, size, static_cast<double>(width) / height, 1))
                addBillboard(position, size, particle.color * opacity);
        }
        if (!geometry->lattice.atoms.empty() && bgfx::isValid(item.instances) && atomView < 64) {
            double diameter =
                LOD::pixels(r * geometry->lattice.atoms.front().radius,
                            std::max(.05, glm::length(cameraSpace(pose.position)) - r), height, 1);
            unsigned level = diameter > 18 ? 2 : diameter > 6 ? 1 : 0;
            unsigned rings = level == 2   ? 12
                             : level == 1 ? 8
                                          : 4,
                     sectors = level == 2   ? 20
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
            uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;
            if (opacity > .99f)
                state |= BGFX_STATE_WRITE_Z;
            else
                state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_FACTOR, BGFX_STATE_BLEND_INV_FACTOR);
            bgfx::setState(state, rgba(glm::vec3(opacity), opacity));
            bgfx::submit(viewId, instanceProgram);
            stats.atoms += count;
            stats.atomTriangles += count * rings * sectors * 2;
        }
    }
    void drawBlend(DetailState& state, const Navigation& nav, float opacity) {
        if (state.blend.active())
            ++stats.lodTransitions;
        drawGeometry(state.blend.previous, nav, opacity * (1 - state.blend.weight()));
        drawGeometry(state.blend.current, nav, opacity * state.blend.weight());
    }
    void renderNode(const Node& node, const Navigation& nav, RegionCache& cache) {
        auto pose = poseFor(node.address, nav);
        auto cp = cameraSpace(pose.position);
        double r = pose.scale;
        double pixels = LOD::pixels(r, glm::length(cp), height, 1.0);
        auto kind = node.address.kind();
        auto& state = details[node.address];
        bool stellarProxy =
            kind == Kind::System && (node.address.ancestorOf(nav.anchor) || state.blend.current);
        if (stellarProxy && cp.z < 0) {
            double physical = std::pow(10.0, std::log10(node.stellarRadiusMetres) - nav.logDistance) * 2.5;
            double radius = std::clamp(physical, -cp.z * .001, -cp.z * .035);
            float intensity = static_cast<float>(.35 + .65 * std::min(node.massSolar / 3, 1.0));
            if (LOD::visible(cp, radius, static_cast<double>(width) / height, 1))
                addBillboard(pose.position, radius, stellarColor(node.temperatureK) * intensity);
        } else if (node.address == nav.anchor && pixels < 1.5 && cp.z < 0 && kind != Kind::Cosmic)
            addBillboard(pose.position, -cp.z * .0015, material(node.environment.mineral).color);
        if (r < 1e-8 || r > 1e4 || pixels < .7 ||
            !LOD::visible(cp, r, static_cast<double>(width) / height, 1))
            return;
        float opacity = node.address.ancestorOf(nav.anchor) && node.address != nav.anchor
                            ? static_cast<float>(1 - smoothstep(3, 12, r))
                            : 1.f;
        if (opacity < .005)
            return;
        if (kind == Kind::Mountain && node.address != nav.anchor && pixels < 160)
            return;
        if (kind == Kind::Surface && node.address != nav.anchor && pixels < 100)
            return;
        state.level = screenDetail(kind, pixels, state.initialized ? state.level : 0);
        state.initialized = true;
        state.used = frame;
        unsigned viewKey =
            kind == Kind::Planet ? terrainView(glm::inverse(pose.orientation) * (eye - pose.position)) : 0;
        auto candidate = cache.request(node, state.level, node.address == nav.anchor ? 0 : 4, viewKey);
        state.blend.update(prepareGPU(candidate) ? candidate : nullptr, frameSeconds);
        if (state.blend.current) {
            ++stats.visibleObjects;
            drawBlend(state, nav, opacity);
        }
    }
    void renderSection(const Node& parent, const Navigation& nav, RegionCache& cache) {
        if (parent.address.kind() < Kind::Planet || parent.address.kind() > Kind::Grain)
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
            drawBlend(state, nav, opacity);
            ++stats.sections;
            sectionLabel = parent.label();
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
            if (bgfx::isValid(victim->second.instances))
                bgfx::destroy(victim->second.instances);
            gpu.erase(victim);
        }
    }
};
Renderer::Renderer(SDL_Window* window, const Universe& universe)
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
    init.reset = BGFX_RESET_VSYNC;
    init.limits.maxTransientVbSize = 8 * 1024 * 1024;
    init.limits.maxTransientIbSize = 3 * 1024 * 1024;
    if (!init.swapChain.nwh)
        throw std::runtime_error("SDL did not provide a native window handle");
    if (!bgfx::init(init))
        throw std::runtime_error("bgfx could not initialize a graphics device");
    p.initialized = true;
    p.swapChain = init.swapChain;
    p.layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    p.colorProgram = bgfx::createProgram(
        bgfx::createEmbeddedShader(embedded, bgfx::getRendererType(), "vs_debugdraw_lines"),
        bgfx::createEmbeddedShader(embedded, bgfx::getRendererType(), "fs_debugdraw_lines"), true);
    p.instanceProgram =
        bgfx::createProgram(p.loadShader("vs_instancing"), p.loadShader("fs_instancing"), true);
    if (!bgfx::isValid(p.colorProgram) || !bgfx::isValid(p.instanceProgram))
        throw std::runtime_error("bgfx shader program creation failed");
    for (unsigned i = 0; i < 3; ++i) {
        Mesh sphere;
        appendSphere(sphere, {}, 1, {1, 1, 1}, i == 2 ? 12 : i == 1 ? 8 : 4, i == 2 ? 20 : i == 1 ? 12 : 6);
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
    p.instanceLayout.begin();
    for (unsigned i = 0; i < 5; ++i)
        p.instanceLayout.add(static_cast<bgfx::Attrib::Enum>(bgfx::Attrib::TexCoord0 + i), 4,
                             bgfx::AttribType::Float);
    p.instanceLayout.end();
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, UINT32_C(0x03060cff), 1.0f, uint8_t(0));
    bgfx::setViewMode(0, bgfx::ViewMode::Default);
    bgfx::setViewMode(1, bgfx::ViewMode::Sequential);
    bgfx::setDebug(BGFX_DEBUG_TEXT);
    p.billboards.reserve(40000);
}
Renderer::~Renderer() = default;
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
    bgfx::reset(BGFX_RESET_VSYNC, &p.swapChain);
}
FrameStats Renderer::render(const Navigation& nav, RegionCache& cache, bool overlay, double fps,
                            size_t markers, std::string_view notice) {
    auto start = std::chrono::steady_clock::now();
    auto& p = *impl_;
    ++p.frame;
    p.frameSeconds = std::clamp(std::chrono::duration<double>(start - p.previousTime).count(), 0.0, .1);
    p.previousTime = start;
    p.sectionLabel.clear();
    p.stats = {};
    p.picks.clear();
    p.billboards.clear();
    p.frameFrames.clear();
    p.framePoses.clear();
    p.atomView = 2;
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
    p.projection = bgfx::getCaps()->homogeneousDepth
                       ? glm::perspectiveRH_NO(1.0, static_cast<double>(p.width) / p.height, .00005, 10000.0)
                       : glm::perspectiveRH_ZO(1.0, static_cast<double>(p.width) / p.height, .00005, 10000.0);
    glm::mat4 v(p.view), proj(p.projection);
    bgfx::setViewTransform(0, glm::value_ptr(v), glm::value_ptr(proj));
    bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(p.width), static_cast<uint16_t>(p.height));
    bgfx::touch(0);
    std::set<Address> rendered;
    // Request the current focus first. Ancestors remain in the same spatial view
    // while the camera crosses the (possibly very large) physical scale gap.
    p.renderNode(nodes.back(), nav, cache);
    rendered.insert(nav.anchor);
    for (auto it = nodes.rbegin() + 1; it != nodes.rend(); ++it) {
        p.renderNode(*it, nav, cache);
        rendered.insert(it->address);
    }
    Mesh locators;
    auto candidates = [&](const Node& parent) {
        auto parentPose = p.poseFor(parent.address, nav);
        auto pc = p.cameraSpace(parentPose.position);
        double parentPixels = LOD::pixels(parentPose.scale, glm::length(pc), p.height, 1);
        if (parentPixels < 25 || parentPose.scale > 40)
            return;
        auto catalog = cache.catalog(parent);
        if (!catalog)
            return;
        const auto parentFrames = p.framesFor(parent.address);
        for (const auto& n : *catalog) {
            const auto& a = n.address;
            auto childFrames = parentFrames;
            childFrames.push_back(n.frame);
            p.frameFrames.try_emplace(a, std::move(childFrames));
            auto pose = p.poseFor(a, nav);
            auto cp = p.cameraSpace(pose.position);
            auto screen = p.project(pose.position);
            if (!screen || cp.z >= 0)
                continue;
            bool isSelected = nav.selected && a == *nav.selected;
            if (a == nav.anchor)
                continue;
            p.picks.push_back({a, *screen, -cp.z});
            if (isSelected || n.address.kind() == Kind::Planet || n.address.kind() == Kind::Rock) {
                double rr = -cp.z * .0025;
                p.addBillboard(pose.position, rr,
                               n.address.kind() == Kind::System ? stellarColor(n.temperatureK)
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
        }
    };
    if (nodes.size() > 1)
        p.renderSection(nodes[nodes.size() - 2], nav, cache);
    candidates(nodes.back());
    if (nodes.size() > 1)
        candidates(nodes[nodes.size() - 2]);
    if (!p.billboards.empty()) {
        uint32_t count = bgfx::getAvailInstanceDataBuffer(static_cast<uint32_t>(p.billboards.size()), 80);
        p.stats.particles = count;
        if (count) {
            bgfx::InstanceDataBuffer buffer;
            bgfx::allocInstanceDataBuffer(&buffer, count, 80);
            std::memcpy(buffer.data, p.billboards.data(), count * sizeof(Instance));
            bgfx::setVertexBuffer(0, p.billboardMesh.vertices);
            bgfx::setIndexBuffer(p.billboardMesh.indices);
            bgfx::setInstanceDataBuffer(&buffer);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_MSAA | BGFX_STATE_DEPTH_TEST_LEQUAL |
                           BGFX_STATE_BLEND_ADD);
            bgfx::submit(0, p.instanceProgram);
        }
    }
    auto ortho =
        glm::orthoRH_NO(0.f, static_cast<float>(p.width), static_cast<float>(p.height), 0.f, -1.f, 1.f);
    if (!bgfx::getCaps()->homogeneousDepth)
        ortho =
            glm::orthoRH_ZO(0.f, static_cast<float>(p.width), static_cast<float>(p.height), 0.f, -1.f, 1.f);
    bgfx::setViewTransform(255, nullptr, glm::value_ptr(ortho));
    bgfx::setViewRect(255, 0, 0, static_cast<uint16_t>(p.width), static_cast<uint16_t>(p.height));
    p.transient(locators, 255, true, false);
    bgfx::dbgTextClear();
    if (overlay) {
        const auto& node = nodes.back();
        auto info = cache.stats();
        int bottom = std::max(10, p.height / 16 - 9);
        bgfx::dbgTextPrintf(3, 2, 0x0f, "I N F I N I T E");
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
            bgfx::dbgTextPrintf(3, bottom + 4, 0x07, "F  FOCUS %s    BACKSPACE  PARENT",
                                std::string(kindName(nav.selected->kind())).c_str());
        else
            bgfx::dbgTextPrintf(3, bottom + 4, 0x07,
                                "MATERIAL LATTICE / STATIC EDUCATIONAL MODEL    BACKSPACE  PARENT");
        bgfx::dbgTextPrintf(3, bottom + 5, 0x08,
                            "WASD FLY   RIGHT-DRAG LOOK   SCROLL SCALE   CLICK SELECT   M MARK   TAB HIDE");
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
    p.stats.gpuBytes = p.gpuBytes;
    p.stats.drawCalls = st->numDraw;
    p.stats.gpuMs = st->gpuTimerFreq
                        ? 1000. * static_cast<double>(st->gpuTimeEnd - st->gpuTimeBegin) / st->gpuTimerFreq
                        : 0;
    return p.stats;
}
std::optional<Address> Renderer::pick(double x, double y) const {
    const auto& p = *impl_;
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
