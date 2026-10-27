#pragma once
#include "Universe.hpp"
#include <atomic>
#include <functional>

namespace infinite {
struct Vertex {
    float x, y, z;
    uint32_t color;
};
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<uint16_t> indices16;
    void compact() {
        vertices.shrink_to_fit();
        if (vertices.size() <= 65536 && !indices.empty()) {
            indices16.assign(indices.begin(), indices.end());
            std::vector<uint32_t>().swap(indices);
        } else
            indices.shrink_to_fit();
    }
    bool wideIndices() const {
        return indices16.empty();
    }
    size_t indexCount() const {
        return wideIndices() ? indices.size() : indices16.size();
    }
    size_t indexWidth() const {
        return wideIndices() ? 4 : 2;
    }
    const void* indexData() const {
        return wideIndices() ? static_cast<const void*>(indices.data()) : indices16.data();
    }
    uint32_t index(size_t i) const {
        return wideIndices() ? indices[i] : indices16[i];
    }
    size_t bytes() const {
        return vertices.size() * sizeof(Vertex) + indexCount() * indexWidth();
    }
};
struct Particle {
    glm::dvec3 position;
    glm::vec3 color;
    float radius;
    uint64_t objectId = UINT64_MAX; // Exact System ID for addressed galaxy samples.
};
struct Geometry {
    Address address;
    uint64_t seed = 0, descriptor = 0;
    uint32_t recipe = generationRecipeVersion;
    unsigned detail = 0;
    unsigned viewKey = 0;
    Address referenceFrame;
    double logUnits = 0;
    bool section = false;
    double inspectionRadius = 0;
    glm::dvec2 coveredHalfExtent{};
    Mesh solid, lines;
    std::vector<Particle> particles;
    Lattice lattice;
    struct Instance {
        glm::mat4 transform;
        glm::vec4 color;
    };
    std::vector<Instance> atomInstances;
    double generationMs = 0;
    size_t bytes() const {
        return solid.bytes() + lines.bytes() + particles.size() * sizeof(Particle) +
               lattice.atoms.size() * sizeof(Atom) + lattice.bonds.size() * sizeof(Bond) +
               lattice.vacantSites.size() * sizeof(uint64_t);
    }
    size_t residentBytes() const {
        return sizeof(Geometry) + solid.vertices.capacity() * sizeof(Vertex) + solid.indices.capacity() * 4 +
               solid.indices16.capacity() * 2 + lines.vertices.capacity() * sizeof(Vertex) +
               lines.indices.capacity() * 4 + lines.indices16.capacity() * 2 +
               particles.capacity() * sizeof(Particle) + lattice.atoms.capacity() * sizeof(Atom) +
               lattice.bonds.capacity() * sizeof(Bond) + lattice.vacantSites.capacity() * 8 +
               atomInstances.capacity() * sizeof(Instance);
    }
};
uint32_t rgba(glm::vec3, float alpha = 1);
glm::vec3 elementColor(uint8_t);
void addLine(Mesh&, glm::dvec3, glm::dvec3, glm::vec3);
void appendSphere(Mesh&, glm::dvec3 center, double radius, glm::vec3 color, unsigned rings = 12,
                  unsigned sectors = 20, bool emissive = false);
struct TerrainLeaf {
    Tile tile;
};
std::vector<TerrainLeaf> terrainLeaves(glm::dvec3 viewDirection, unsigned maxLevel);
class GeometryGenerator {
  public:
    explicit GeometryGenerator(const Universe& universe) : universe_(universe) {}
    std::shared_ptr<Geometry> generate(const Node&, unsigned lod, const std::atomic<bool>* cancel = nullptr,
                                       unsigned viewKey = 0) const;
    std::shared_ptr<Geometry> section(const Node& parent, const Address& anchor, int halfDecade,
                                      const std::atomic<bool>* cancel = nullptr) const;

  private:
    const Universe& universe_;
};
uint64_t geometryFingerprint(const Geometry&);
unsigned geometryDetail(Kind, unsigned requested);
int sectionBucket(double renderLog);
unsigned screenDetail(Kind, double projectedDiameter, unsigned previous);
unsigned terrainView(glm::dvec3 localEye);
std::function<double(glm::dvec3)> densityField(const Universe&, const Node&, double outsideBand = 0);
class GeometryBlend {
  public:
    void update(std::shared_ptr<const Geometry> candidate, double seconds);
    std::shared_ptr<const Geometry> current, previous;
    float weight() const {
        return blend_;
    }
    bool active() const {
        return static_cast<bool>(previous);
    }

  private:
    float blend_ = 1;
};
} // namespace infinite
