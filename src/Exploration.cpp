#include "infinite/Exploration.hpp"
#include "infinite/Planetology.hpp"
#include "infinite/StationControl.hpp"
#include <iomanip>
#include <set>
#include <sstream>

namespace infinite {
namespace {
bool finite(glm::dvec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
glm::dvec3 eyeIn(const Universe& u, const SurveyObserver& o, const Address& target) {
    if (o.reference == target)
        return o.position;
    return u.pointRelative(o.reference, o.position / std::pow(10., u.describe(o.reference).frame.logRadius),
                           target, 0);
}
bool observerValid(const SurveyObserver& o) {
    return finite(o.position) && finite(o.velocity) && std::isfinite(o.hull) && o.hull > 0 &&
           std::isfinite(o.size) && o.size >= 1e-12 && o.size <= 1 && !o.field &&
           std::isfinite(glm::length(o.attitude)) && std::abs(glm::length(o.attitude) - 1) < 1e-6 &&
           std::isfinite(o.seconds) && o.seconds >= 0 && o.seconds <= 1e12;
}
} // namespace
std::optional<Address> surveySystem(const Address& a) {
    if (a.kind() < Kind::System)
        return {};
    auto out = a;
    while (out.kind() > Kind::System)
        out = out.parent();
    return out;
}
SurveyRecord surveyReading(const Universe& u, const Address& a, unsigned grade, double seconds) {
    if (a.kind() < Kind::Planet || grade < 1 || grade > 3 || !std::isfinite(seconds) || seconds < 0 ||
        seconds > 1e12)
        throw std::invalid_argument("Invalid survey reading");
    const auto n = u.describe(a);
    SurveyRecord r;
    r.address = a;
    r.grade = grade;
    r.seconds = seconds;
    r.descriptor = descriptorFingerprint(n);
    r.name = n.label();
    r.mineral = std::string(material(n.environment.mineral).name) + " / " +
                std::string(material(n.environment.mineral).formula);
    auto c = n.environment.chemistry;
    c.normalize();
    r.fractions = {c.ice, c.metal, c.silicate, c.carbon};
    r.formation = a.kind() == Kind::Planet
                      ? std::string(hasSolidSurface(n) ? "SOLID / " : "GAS ENVELOPE / ") +
                            (n.geology ? std::string(processName(n.geology->type)) : "BULK COMPOSITION")
                  : a.kind() >= Kind::Grain
                      ? std::string(processName(crystalCell(n.environment.mineral, n.environment).phase))
                  : a.kind() == Kind::Rock ? std::string(processName(n.rock.type))
                                           : "SURFACE GEOLOGY";
    r.conditions = std::to_string(int(n.environment.temperature)) + "K / " +
                   std::to_string(n.environment.pressureGPa) + " GPA";
    std::ostringstream out;
    out << std::fixed << std::setprecision(grade == 1 ? 0 : grade == 2 ? 1 : 2);
    constexpr std::array names{"ICE ", "ORE ", "SILICATE ", "CARBON "};
    for (size_t i = 0; i < 4; ++i)
        out << names[i] << r.fractions[i] * 100 << "% ";
    r.composition = out.str();
    r.prospect = prospectAddress(u, a);
    if (r.prospect)
        r.resources = prospectResources(u, *r.prospect);
    if (r.prospect && a.kind() == Kind::Surface) {
        const auto e =
            resourceExposure(n, n.geographicDirection, geologySample(*n.geology, n.geographicDirection));
        r.formation = std::string(exposureName(e.type)) + " / " + std::to_string(int(e.concentration * 100)) +
                      "% EXPOSURE GRADE";
    }
    return r;
}
std::optional<Address> prospectAddress(const Universe& u, const Address& target) {
    if (target.kind() < Kind::Surface)
        return {};
    auto surface = target;
    while (surface.kind() > Kind::Surface)
        surface = surface.parent();
    const auto p = u.describe(surface.parent());
    if (!hasSolidSurface(p))
        return {};
    const auto direction = u.describe(surface).geographicDirection;
    return p.address.child(Kind::Surface, encodeTile(tileAt(direction, 7)));
}
ResourceStock prospectResources(const Universe& u, const Address& target) {
    auto key = prospectAddress(u, target);
    if (!key)
        return {};
    const auto n = u.describe(*key);
    const auto field = geologySample(*n.geology, n.geographicDirection);
    const auto e = resourceExposure(n, n.geographicDirection, field);
    const auto mass = int64_t(
        std::clamp(material(n.environment.mineral).density * (.35 + e.concentration * .35), 100., 5000.));
    ResourceStock out{};
    int64_t assigned = 0;
    for (unsigned i = 0; i < 4; ++i) {
        out[i] = int64_t(std::floor(mass * e.fractions[i]));
        assigned += out[i];
    }
    const auto dominant =
        size_t(std::max_element(e.fractions.begin(), e.fractions.end()) - e.fractions.begin());
    out[dominant] += mass - assigned;
    return out;
}
bool surveyAccess(const Universe& u, const SurveyObserver& o, const Address& target) {
    if (!observerValid(o) || target.kind() < Kind::Planet)
        return false;
    if (target.kind() == Kind::Surface) {
        if (auto key = prospectAddress(u, target)) {
            const auto planet = u.describe(key->parent());
            const auto local = eyeIn(u, o, planet.address);
            const auto d = glm::normalize(local);
            const auto altitude =
                glm::length(local) - std::pow(10., planet.frame.logRadius) - u.terrainHeight(planet, d);
            if (std::isfinite(altitude) && altitude >= -3 && altitude <= 1000 &&
                encodeTile(tileAt(d, 7)) == key->id())
                return true;
        }
    }
    const auto n = u.describe(target);
    const auto eye = eyeIn(u, o, target);
    const double distance = glm::length(eye), radius = std::pow(10., n.frame.logRadius);
    const double reach = target.kind() >= Kind::Grain ? radius * 20 : radius * 8 + 200 * o.size;
    if (!std::isfinite(distance) || distance > reach)
        return false;
    if (distance <= radius * 1.1)
        return true;
    const auto direction =
        u.relative(o.reference, target, 0).orientation * (o.attitude * glm::dvec3(0, 0, -1));
    return glm::dot(direction, -eye / distance) > .8;
}
MiningAccess miningAccess(const Universe& u, const SurveyObserver& o, const Address& target, unsigned mount,
                          std::span<const ActivityBody> nearby) {
    MiningAccess a;
    a.status = "MINING / FULL SIZE / STOP BELOW 3 M/S / APPROACH SOLID GROUND";
    if (!observerValid(o) || o.size < .99 || mount >= maxWeaponMounts || glm::length(o.velocity) > 3)
        return a;
    const auto key = prospectAddress(u, target);
    if (!key) {
        a.status = "SELECT A SURFACE PROSPECT / F12 SURVEY FIRST";
        return a;
    }
    const auto planet = key->parent();
    const auto n = u.describe(planet);
    const auto eye = eyeIn(u, o, planet);
    const auto radial = glm::normalize(eye);
    const auto radius = std::pow(10., n.frame.logRadius);
    const double altitude = glm::length(eye) - radius - u.terrainHeight(n, radial);
    if (!std::isfinite(altitude) || altitude < -3 || altitude > 40 ||
        encodeTile(tileAt(radial, 7)) != key->id())
        return a;
    const auto rotation = u.relative(o.reference, planet, 0).orientation * o.attitude;
    const double yaw = mount == 1 ? -1.5707963267948966 : mount == 2 ? 1.5707963267948966 : 0;
    auto tangent = rotation * glm::dvec3(std::sin(yaw), 0, -std::cos(yaw));
    tangent -= radial * glm::dot(tangent, radial);
    if (glm::length(tangent) < 1e-6) {
        a.status = "MINING / LEVEL TOOL ABOVE THE SURFACE";
        return a;
    }
    const auto d = glm::normalize(eye + glm::normalize(tangent) * 40.);
    const auto endpoint = d * (radius + u.terrainHeight(n, d));
    const auto muzzle = eye + rotation * glm::dvec3(weaponMountLateralMetres(mount), 2, 0);
    a.ray.reference = planet;
    a.ray.origin = muzzle;
    a.ray.direction = glm::normalize(endpoint - muzzle);
    a.ray.right = rotation * glm::dvec3(1, 0, 0);
    a.ray.seconds = o.seconds;
    auto check = a.ray;
    check.scale = .001; // only nearby hull obstruction; actual terrain supplies endpoint
    const auto trace = traceWeapon(u, check, glm::length(endpoint - muzzle), nearby);
    if (trace.body) {
        a.status = "MINING / INTERVENING SHIP / HOLD FIRE";
        return a;
    }
    a.allowed = true;
    a.deposit = *key;
    a.endpoint = endpoint;
    a.status = "MINING ACCESS / SOLID TERRAIN";
    return a;
}
void SurveyController::reset() {
    target_.reset();
    progress_ = 0;
    submitted_ = false;
}
bool SurveyController::update(const Universe& u, const SurveyObserver& o,
                              const std::optional<Address>& target, unsigned grade, double quality, double dt,
                              bool held, bool submissionReady) {
    if (!std::isfinite(dt) || dt <= 0 || dt > 1)
        return false;
    if (!held) {
        reset();
        status_ = "HOLD F12 SCAN / F11 JOURNAL / CTRL-SPACE MINE";
        return false;
    }
    if (!target || !surveyAccess(u, o, *target)) {
        reset();
        status_ = "SCAN / APPROACH AND FACE A WORLD OR MATERIAL / DRIVE OFF";
        return false;
    }
    if (!target_ || *target_ != *target) {
        reset();
        target_ = target;
    }
    if (submitted_) {
        status_ = "SURVEY SUBMITTED / RELEASE F12";
        return false;
    }
    if (!submissionReady) {
        status_ = "SCAN WAITING / FLIGHT COMPUTER BUSY";
        return false;
    }
    const double rate =
        std::clamp(grade, 1u, 3u) * (.55 + .45 * std::clamp(std::isfinite(quality) ? quality : 0., 0., 1.5));
    progress_ = std::min(8., progress_ + dt * rate);
    status_ = "SCANNING / " + std::to_string(int(progress_ / 8 * 100)) + "% / HOLD F12";
    if (progress_ < 8)
        return false;
    submitted_ = true;
    return true;
}
bool MiningController::update(const MiningAccess& a, unsigned level, double readiness, double dt, bool held,
                              bool submissionReady) {
    if (!std::isfinite(dt) || dt <= 0 || dt > 1)
        return false;
    heat_ = std::max(0., heat_ - 18 * dt);
    if (heat_ < 30)
        overheated_ = false;
    if (!held) {
        reset();
        status_ = "CTRL-SPACE / EXTRACT SURVEYED MATERIAL";
        return false;
    }
    if (!level || level > 3) {
        reset();
        status_ = "FIT MINING HEAD AT PORT / F2 SURVEY";
        return false;
    }
    if (!a.allowed || !std::isfinite(readiness) || readiness <= 0) {
        reset();
        status_ = readiness <= 0 ? "MINING / STATION UNMANNED OR DAMAGED / F9" : a.status;
        return false;
    }
    if (!target_ || *target_ != a.deposit) {
        reset();
        target_ = a.deposit;
    }
    if (!submissionReady) {
        status_ = "MINING WAITING / FLIGHT COMPUTER BUSY";
        return false;
    }
    if (overheated_) {
        status_ = "MINING / COOLING";
        return false;
    }
    heat_ = std::min(100., heat_ + 30 * dt);
    if (heat_ >= 100) {
        overheated_ = true;
        status_ = "MINING / OVERHEATED";
        return false;
    }
    progress_ += dt * level * std::clamp(readiness, .2, 1.2);
    status_ = "EXTRACTING / " + std::to_string(int(std::min(1., progress_ / 6) * 100)) + "% / HEAT " +
              std::to_string(int(heat_)) + "%";
    if (progress_ < 6)
        return false;
    progress_ = 0;
    return true;
}
std::string ExplorationLedger::serialize() const {
    std::ostringstream out;
    out << std::setprecision(17) << "INFINITE_EXPLORATION 2 " << state_.scannerLevel << ' '
        << state_.miningLevel << ' ' << state_.miningMount << ' ' << state_.minedKg << ' '
        << state_.surveysDelivered << ' ' << state_.surveyEarnings << ' ' << state_.revision << '\n';
    out << state_.journal.size() << '\n';
    for (const auto& r : state_.journal)
        out << std::quoted(r.address.str()) << ' ' << r.descriptor << ' ' << r.grade << ' ' << r.seconds
            << ' ' << r.paidGrade << ' ' << r.revision << '\n';
    out << state_.prospects.size() << '\n';
    for (const auto& p : state_.prospects) {
        out << std::quoted(p.address.str());
        for (auto kg : p.extracted)
            out << ' ' << kg;
        for (auto kg : p.capacity)
            out << ' ' << kg;
        out << '\n';
    }
    out << state_.contracts.size() << '\n';
    for (const auto& c : state_.contracts)
        out << c.id << ' ' << std::quoted(c.issuer.str()) << ' ' << std::quoted(c.target.str()) << ' '
            << c.grade << ' ' << unsigned(c.phase) << ' ' << c.reward << ' ' << c.escrow << ' '
            << c.acceptedRevision << '\n';
    return out.str();
}
bool ExplorationLedger::restore(std::string_view text, const Universe& u) {
    try {
        ExplorationState s;
        std::istringstream in{std::string(text)};
        std::string h;
        unsigned v;
        if (!(in >> h >> v >> s.scannerLevel >> s.miningLevel >> s.miningMount >> s.minedKg >>
              s.surveysDelivered >> s.surveyEarnings >> s.revision) ||
            h != "INFINITE_EXPLORATION" || (v != 1 && v != 2) || s.scannerLevel < 1 || s.scannerLevel > 3 ||
            s.miningLevel > 3 || s.miningMount > noWeaponMount ||
            bool(s.miningLevel) != (s.miningMount < noWeaponMount) || s.minedKg > 1000000000000ULL ||
            s.surveysDelivered > 1000000000000ULL || s.surveyEarnings < 0 ||
            s.surveyEarnings > 1000000000000000LL || s.revision > 1000000000000ULL)
            return false;
        size_t count;
        std::set<Address> ids;
        if (!(in >> count) || count > maxSurveyRecords)
            return false;
        for (size_t i = 0; i < count; ++i) {
            std::string a;
            uint64_t hash, revision;
            unsigned grade, paid;
            double seconds;
            if (!(in >> std::quoted(a) >> hash >> grade >> seconds >> paid >> revision) || paid > grade ||
                !revision || revision > s.revision)
                return false;
            auto address = Address::parse(a);
            if (!ids.insert(address).second)
                return false;
            auto r = surveyReading(u, address, grade, seconds);
            if (hash != r.descriptor)
                return false;
            r.paidGrade = paid;
            r.revision = revision;
            s.journal.push_back(std::move(r));
        }
        ids.clear();
        if (!(in >> count) || count > maxProspects)
            return false;
        for (size_t i = 0; i < count; ++i) {
            Prospect p;
            std::string a;
            if (!(in >> std::quoted(a)))
                return false;
            p.address = Address::parse(a);
            auto canonical = prospectAddress(u, p.address);
            if (!canonical || *canonical != p.address || !ids.insert(p.address).second)
                return false;
            p.capacity = legacyProspectResources(u, p.address);
            for (auto& kg : p.extracted)
                if (!(in >> kg) || kg < 0 || kg > 5000)
                    return false;
            if (v >= 2)
                for (auto& kg : p.capacity)
                    if (!(in >> kg) || kg < 0 || kg > 5000)
                        return false;
            int64_t mass = 0;
            for (size_t g = 0; g < 4; ++g) {
                if (p.extracted[g] > p.capacity[g])
                    return false;
                mass += p.capacity[g];
            }
            if (mass < 100 || mass > 5000)
                return false;
            s.prospects.push_back(p);
        }
        for (auto& r : s.journal)
            if (r.prospect)
                for (const auto& p : s.prospects)
                    if (p.address == *r.prospect) {
                        r.resources = p.capacity;
                        break;
                    }
        std::set<uint64_t> jobs;
        if (!(in >> count) || count > maxSurveyContracts)
            return false;
        for (size_t i = 0; i < count; ++i) {
            SurveyContract c;
            std::string issuer, target;
            unsigned phase;
            if (!(in >> c.id >> std::quoted(issuer) >> std::quoted(target) >> c.grade >> phase >> c.reward >>
                  c.escrow >> c.acceptedRevision) ||
                !c.id || !jobs.insert(c.id).second || c.grade < 1 || c.grade > 3 || phase > 3 ||
                c.reward < 100 || c.reward > 10000000 || c.escrow < 0 || c.escrow > c.reward ||
                c.acceptedRevision > s.revision)
                return false;
            c.issuer = ActivityId::parse(issuer);
            c.target = Address::parse(target);
            c.phase = SurveyPhase(phase);
            if (c.issuer.home.kind() != Kind::Planet || c.target.kind() != Kind::Planet ||
                surveySystem(c.issuer.home) != surveySystem(c.target) ||
                ((phase == 1) ? c.escrow != c.reward : c.escrow != 0))
                return false;
            c.name = u.describe(c.target).label();
            s.contracts.push_back(c);
        }
        in >> std::ws;
        if (!in.eof())
            return false;
        state_ = std::move(s);
        return true;
    } catch (...) {
        return false;
    }
}
size_t ExplorationLedger::residentBytes() const {
    size_t n = sizeof(*this) + state_.journal.capacity() * sizeof(SurveyRecord) +
               state_.prospects.capacity() * sizeof(Prospect) +
               state_.contracts.capacity() * sizeof(SurveyContract);
    for (const auto& r : state_.journal)
        n += r.address.path.capacity() * sizeof(Segment) + r.name.capacity() + r.formation.capacity() +
             r.mineral.capacity() + r.conditions.capacity() + r.composition.capacity() +
             (r.prospect ? r.prospect->path.capacity() * sizeof(Segment) : 0);
    for (const auto& p : state_.prospects)
        n += p.address.path.capacity() * sizeof(Segment);
    for (const auto& c : state_.contracts)
        n += (c.issuer.home.path.capacity() + c.target.path.capacity()) * sizeof(Segment) + c.name.capacity();
    return n;
}
} // namespace infinite
