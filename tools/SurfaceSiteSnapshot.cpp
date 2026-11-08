#include "infinity/SurfaceLife.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace infinity;
int main(int argc, char** argv) {
    if (argc != 3)
        return 2;
    Universe u;
    std::ostringstream records;
    records << std::setprecision(17);
    unsigned count = 0;
    for (unsigned star : {0, 1, 9, 137}) {
        const auto system = u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, star));
        for (unsigned planet = 0; planet < u.establishedPlanetCount(system); planet++) {
            const auto p = u.describe(system.address.child(Kind::Planet, planet));
            const auto world = generateSurfaceLife(u, p);
            auto retained = p;
            if (p.geology && p.geology->landform) {
                // A new landform may change only empty wilderness entries.
                // Keep the golden's original geology hash and exact site lines.
                if (!world->settlements.empty()) {
                    std::cerr << "Major relief altered a settled world\n";
                    return 1;
                }
                auto base = std::make_shared<PlanetProcess>(*p.geology);
                base->landform.reset();
                retained.geology = std::move(base);
            }
            records << "WORLD " << p.address.str() << ' ' << world->seed << ' '
                    << descriptorFingerprint(retained) << '\n';
            for (const auto& s : world->settlements) {
                records << "SITE " << p.address.str() << ' ' << s.slot << ' ' << s.seed << ' '
                        << unsigned(s.kind) << ' ' << s.sealed << ' ' << s.landingPoint.x << ' '
                        << s.landingPoint.y << ' ' << s.landingPoint.z << ' ' << s.orientation.w << ' '
                        << s.orientation.x << ' ' << s.orientation.y << ' ' << s.orientation.z << ' '
                        << s.radius << ' ' << s.name << '\n';
                ++count;
            }
        }
    }
    const std::string mode = argv[1];
    if (mode == "--write") {
        std::ofstream(argv[2]) << records.str();
    } else if (mode == "--check") {
        std::ifstream file(argv[2]);
        std::ostringstream old;
        old << file.rdbuf();
        if (old.str() != records.str()) {
            std::cerr << "Surface site identities, seeds or landing positions changed\n";
            return 1;
        }
    } else
        return 2;
    std::cout << "PASS: " << count << " stable surface site anchors, seeds, names and orientations\n";
}
