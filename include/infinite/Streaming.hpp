#pragma once
#include "World.hpp"
#include <unordered_map>

namespace infinite {
inline constexpr unsigned terrainCells = 8;
std::pair<Address, glm::dvec3> canonicalRoot(const Address&, glm::dvec3 rootPoint);
std::vector<Address> rootNeighborhood(const Address&, glm::dvec3 localEye = {}, bool ahead = false);
double streamedCosmicDensity(const Universe&, const Address& root, glm::dvec3 rootPoint);
std::shared_ptr<Geometry> cosmicBoundaryGeometry(const Universe&, const Node&, const std::atomic<bool>*);
std::vector<Tile> terrainFrontier(glm::dvec3 planetEye, double altitude, double pixels, size_t budget = 192);
Tile terrainNeighbor(Tile, unsigned edge);
unsigned terrainStitches(Tile, const std::vector<Tile>&);
uint64_t terrainVertexKey(Tile, unsigned x, unsigned y);
glm::dvec3 terrainVertexDirection(uint64_t);
std::shared_ptr<Geometry> terrainGeometry(const Universe&, const Node&, Tile, unsigned stitch,
                                          const std::atomic<bool>* cancel = nullptr);
struct TerrainPatch {
    Tile tile;
    unsigned stitch;
    std::shared_ptr<const Geometry> geometry;
};
class TerrainStreamer {
  public:
    explicit TerrainStreamer(const Universe& universe) : universe_(universe) {}
    void update(const Node& planet, glm::dvec3 planetEye, double height, double dt, RegionCache&);
    void clear();
    const std::vector<TerrainPatch>& patches() const {
        return patches_;
    }
    bool ready() const {
        return patches_.size() >= 6;
    }
    glm::dvec3 position(const TerrainPatch&, unsigned vertex) const;
    bool morphing() const {
        return morph_ < 1;
    }

  private:
    const Universe& universe_;
    std::optional<Address> planet_;
    std::vector<TerrainPatch> patches_;
    std::unordered_map<uint64_t, glm::dvec3> starts_;
    double morph_ = 1;
    glm::dvec3 oldPosition(uint64_t key) const;
};
} // namespace infinite
