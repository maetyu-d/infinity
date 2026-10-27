#include "infinite/World.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif
using namespace infinite;
int main() {
    Universe u;
    GeometryGenerator generator(u);
    auto address = Address{}.child(Kind::Galaxy, 0);
    size_t total = 0;
    std::cout << "kind,detail,median_ms,p95_ms,geometry_bytes,vertices,atoms\n";
    for (;;) {
        auto node = u.describe(address);
        for (unsigned lod : {0u, 2u, 4u}) {
            std::vector<double> times;
            std::shared_ptr<Geometry> g;
            for (int i = 0; i < 3; ++i) {
                g = generator.generate(node, lod);
                times.push_back(g->generationMs);
            }
            std::sort(times.begin(), times.end());
            total = std::max(total, g->bytes());
            std::cout << kindName(address.kind()) << ',' << lod << ',' << std::fixed << std::setprecision(3)
                      << times[1] << ',' << times.back() << ',' << g->bytes() << ','
                      << g->solid.vertices.size() << ',' << g->lattice.atoms.size() << '\n';
        }
        auto children = u.children(node, 1);
        if (children.empty())
            break;
        address = children.front();
    }
    std::cerr << "largest_geometry_bytes=" << total << '\n';
#if defined(__APPLE__) || defined(__linux__)
    struct rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
#if defined(__APPLE__)
    std::cerr << "peak_rss_bytes=" << usage.ru_maxrss << '\n';
#else
    std::cerr << "peak_rss_bytes=" << usage.ru_maxrss * 1024 << '\n';
#endif
#endif
}
