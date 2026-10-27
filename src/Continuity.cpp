#include "infinite/Continuity.hpp"
#include <stdexcept>

namespace infinite {
uint64_t ObjectProof::digest() const {
    uint64_t hash = mix64(seed ^ descriptor);
    for (auto value : geometry)
        hash = mix64(hash ^ value);
    return hash;
}
ObjectProof proveObject(const Universe& universe, const Address& address, unsigned viewKey) {
    // A fresh descriptor cache makes the audit independent of render-side
    // memoization as well as mesh-cache contents.
    Universe independent(universe.seed());
    auto node = independent.describe(address);
    ObjectProof proof{node.seed, descriptorFingerprint(node), {}};
    std::map<unsigned, uint64_t> levels;
    for (unsigned level = 0; level < proof.geometry.size(); ++level) {
        unsigned canonical = geometryDetail(address.kind(), level);
        auto it = levels.find(canonical);
        if (it == levels.end()) {
            auto geometry = GeometryGenerator(independent).generate(node, canonical, nullptr, viewKey);
            if (!geometry || geometry->address != address || geometry->seed != node.seed ||
                geometry->descriptor != proof.descriptor || geometry->recipe != generationRecipeVersion ||
                geometry->detail != canonical || geometry->referenceFrame != address || geometry->section)
                throw std::runtime_error("Geometry identity disagrees with its object descriptor");
            it = levels.emplace(canonical, geometryFingerprint(*geometry)).first;
        }
        proof.geometry[level] = it->second;
    }
    return proof;
}
RoundTripVerifier::~RoundTripVerifier() {
    tasks_.wait();
}
void RoundTripVerifier::observe(const Node& node, const std::shared_ptr<const Geometry>& submitted) {
    if (!submitted || submitted->section || submitted->address != node.address ||
        submitted->seed != node.seed || submitted->descriptor != descriptorFingerprint(node) ||
        submitted->detail > 4)
        throw std::runtime_error("Viewer submitted geometry with the wrong procedural identity");
    auto hash = geometryFingerprint(*submitted);
    auto address = node.address;
    auto detail = submitted->detail;
    auto viewKey = submitted->viewKey;
    tasks_.run([this, address, detail, hash, viewKey]() {
        try {
            auto proof = proveObject(universe_, address, viewKey);
            std::lock_guard lock(mutex_);
            if (proof.geometry[detail] != hash)
                failure_ = "Submitted viewer mesh differs from independent regeneration";
            auto [it, inserted] = proofs_.emplace(address, proof);
            if (!inserted) {
                ++revisits_;
                if (it->second != proof)
                    failure_ = "Round-trip descriptor, material, sites or LOD geometry changed";
            }
        } catch (const std::exception& e) {
            std::lock_guard lock(mutex_);
            failure_ = e.what();
        }
    });
}
void RoundTripVerifier::finish() {
    tasks_.wait();
    if (!failure_.empty())
        throw std::runtime_error(failure_);
    if (proofs_.size() != 8 || revisits_ != 7)
        throw std::runtime_error("Incomplete galaxy-to-lattice proof");
    std::optional<Address> previous;
    for (const auto& [address, proof] : proofs_) {
        if (previous && address.parent() != *previous)
            throw std::runtime_error("Tour changed hierarchical branches");
        previous = address;
    }
}
uint64_t RoundTripVerifier::digest() const {
    uint64_t hash = 0;
    for (const auto& [address, proof] : proofs_)
        hash = mix64(hash ^ proof.digest());
    return hash;
}
} // namespace infinite
