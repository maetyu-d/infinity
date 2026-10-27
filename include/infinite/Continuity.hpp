#pragma once
#include "Geometry.hpp"
#include <map>
#include <mutex>
#include <oneapi/tbb/task_group.h>

namespace infinite {
struct ObjectProof {
    uint64_t seed = 0, descriptor = 0;
    std::array<uint64_t, 5> geometry{};
    auto operator<=>(const ObjectProof&) const = default;
    uint64_t digest() const;
};
ObjectProof proveObject(const Universe&, const Address&, unsigned viewKey = 0);
// Used by the native GPU tour. Full recipe regeneration runs in workers, while
// the captured fingerprint is from the geometry actually submitted by the viewer.
class RoundTripVerifier {
  public:
    explicit RoundTripVerifier(const Universe& universe) : universe_(universe) {}
    ~RoundTripVerifier();
    void observe(const Node&, const std::shared_ptr<const Geometry>& submitted);
    void finish();
    size_t objects() const {
        return proofs_.size();
    }
    size_t revisits() const {
        return revisits_;
    }
    uint64_t digest() const;

  private:
    const Universe& universe_;
    oneapi::tbb::task_group tasks_;
    std::mutex mutex_;
    std::map<Address, ObjectProof> proofs_;
    size_t revisits_ = 0;
    std::string failure_;
};
} // namespace infinite
