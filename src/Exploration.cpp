#include "infinity/SaveTag.hpp"
#include "infinity/Exploration.hpp"
#include "infinity/MajorLandforms.hpp"
#include "infinity/OrbitalDebris.hpp"
#include "infinity/Planetology.hpp"
#include "infinity/StationControl.hpp"
#include "infinity/SurfaceProvince.hpp"
#include <iomanip>
#include <set>
#include <sstream>

namespace infinity {
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
        const auto field = geologySample(*n.geology, n.geographicDirection);
        const auto province = surfaceProvince(*n.geology, n.geographicDirection, field).dominant();
        const auto e = resourceExposure(n, n.geographicDirection, field);
        r.formation = std::string(provinceName(province)) + " / " + std::string(exposureName(e.type)) +
                      " / " + std::to_string(int(e.concentration * 100)) + "% EXPOSURE GRADE";
        if (n.geology->landform && field.majorCoverage > .1)
            r.formation += " / " + std::string(majorLandformName(n.geology->landform->type));
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
    if(surveyExposure(u,o,target).scanQuality<=.16)return false;
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
SurfaceExposure surveyExposure(const Universe& u,const SurveyObserver& o,std::optional<Address> target) {
    auto planet=o.reference.kind()>=Kind::Planet?std::optional<Address>(o.reference):target;
    if(!planet||planet->kind()<Kind::Planet||!observerValid(o))return {};
    while(planet->kind()>Kind::Planet)*planet=planet->parent();
    const auto n=u.describe(*planet);if(!hasSolidSurface(n))return {};
    const auto eye=eyeIn(u,o,*planet);const double distance=glm::length(eye),radius=std::pow(10.,n.frame.logRadius);
    if(!std::isfinite(distance)||distance<radius*.9||distance>radius+30000)return {};
    const auto direction=glm::normalize(eye);
    const double altitude=distance-radius-u.terrainHeight(n,direction);
    return surfaceExposure(n,sampleWeather(planetClimate(n),direction,std::max(0.,altitude),o.seconds),altitude,direction);
}
std::string_view resourceName(unsigned i) {
    constexpr std::array names{"ICE", "ORE", "SILICATE", "CARBON"};
    return i < names.size() ? names[i] : "UNKNOWN";
}
const SeamExpedition* activeSeam(const ExplorationState& s, const Address& system) {
    for (auto i = s.seams.rbegin(); i != s.seams.rend(); ++i)
        if (surveySystem(i->source) == std::optional<Address>(system))
            return &*i;
    return nullptr;
}
const SeamExpedition* markedSeam(const ExplorationState& s, const Address& target) {
    for (auto i = s.seams.rbegin(); i != s.seams.rend(); ++i)
        if (i->marked && i->source.ancestorOf(target))
            return &*i;
    return nullptr;
}
void selectSeamSource(Navigation& nav, const Address& source) {
    if (nav.anchor == source)
        nav.selected.reset();
    else
        nav.select(source);
}
bool seamSurveyAccess(const Universe& u, const SurveyObserver& o, const SeamExpedition& s,
                      const Address& target) {
    if (s.source.kind() != Kind::Rock || o.size > .001 || !s.source.ancestorOf(target) ||
        !surveyAccess(u, o, target))
        return false;
    const double radius = std::pow(10., u.describe(s.source).frame.logRadius);
    const double sampleRadius = std::pow(10., u.describe(target).frame.logRadius);
    // A close inspection is earned in physical space, never by changing render magnification.
    return o.size * 12 <= std::min(radius * .03, sampleRadius * .3) &&
           glm::length(eyeIn(u, o, s.source)) <= radius * 1.5 &&
           glm::length(o.velocity) <= 5 * o.size;
}
std::optional<glm::dvec3> seamEntryPoint(const Universe& u, const SurveyObserver& o, const Address& source) {
    if (!observerValid(o) || o.size < .99 || source.kind() != Kind::Rock ||
        glm::length(o.velocity) > 3)
        return {};
    const auto pool = prospectAddress(u, source);
    if (!pool)
        return {};
    const auto p = u.describe(pool->parent());
    const auto eye = eyeIn(u, o, p.address);
    const auto direction = glm::normalize(eye);
    const double radius = std::pow(10., p.frame.logRadius);
    const double terrain = u.terrainHeight(p, direction);
    const double altitude = glm::length(eye) - radius - terrain;
    if (!std::isfinite(altitude) || altitude < -3 || altitude > 100 ||
        encodeTile(tileAt(direction, 7)) != pool->id())
        return {};
    const auto point = direction * (radius + terrain + std::max(16., altitude));
    const auto rock = u.describe(source);
    const double rockRadius = std::pow(10., rock.frame.logRadius);
    const auto pose = u.relative(source, p.address, 0);
    const auto local = glm::inverse(pose.orientation) * (point - pose.position) / rockRadius;
    if (u.rockSdf(rock, local) * rockRadius < 12 || glm::length(point - pose.position) > 200 + rockRadius * 8)
        return {};
    return point;
}
MiningAccess miningAccess(const Universe& u, const SurveyObserver& o, const Address& target, unsigned mount,
                          std::span<const ActivityBody> nearby, const SeamExpedition* seam) {
    MiningAccess a;
    a.status = "MINING / FULL SIZE / STOP BELOW 3 M/S / APPROACH SOLID GROUND";
    if (!observerValid(o) || o.size < .99 || mount >= maxWeaponMounts || glm::length(o.velocity) > 3)
        return a;
    const auto key = prospectAddress(u, target);
    if (!key) {
        a.status = "SELECT A SURFACE PROSPECT / F12 SURVEY FIRST";
        return a;
    }
    if(surveyExposure(u,o,target).electrical>.8){
        a.status="CUTTER SAFETY HOLD / ELECTRICAL STORM / SHELTER OR USE A BORE";return a;
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
    auto endpoint = d * (radius + u.terrainHeight(n, d));
    if (seam) {
        if (!seam->marked || !seam->material || !seam->source.ancestorOf(target) || seam->pool != *key) {
            a.status = "SELECT THE MARKED SEAM / RETURN TO FULL SIZE";
            return a;
        }
        endpoint = u.relative(*seam->material, planet, 0).position;
        const double sourceRadius = std::pow(10., u.describe(seam->source).frame.logRadius);
        if (glm::length(endpoint - eye) > 140 + sourceRadius * 2) {
            a.status = "MARKED SEAM / APPROACH THE ORIGINAL OUTCROP";
            return a;
        }
    }
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
MiningAccess debrisMiningAccess(const Universe& u, const SurveyObserver& observer, const ActivityId& id) {
    MiningAccess access;
    access.status = "LAND AND SECURE THE GEAR TO MINE";
    if (!observerValid(observer) || !observer.landed || observer.size < .99 || !isOrbitalDebris(id))
        return access;
    const auto planet = u.describe(id.home);
    const auto body = sampleActivity(planet, id.slot, observer.seconds);
    if (!landableDebris(body))
        return access;
    const auto eye = eyeIn(u, observer, id.home);
    const auto rotation = u.relative(observer.reference, id.home, 0).orientation;
    const int side = glm::dot(eye - body.position, body.orientation * glm::dvec3(0, 1, 0)) < 0 ? -1 : 1;
    const auto pad = debrisPad(body, side);
    if (glm::length(eye - pad.eye) > .36 || glm::length(rotation * observer.velocity - pad.velocity) > .8 ||
        glm::dot(rotation * (observer.attitude * glm::dvec3(0, 1, 0)), pad.up) < .996)
        return access;
    access.allowed = true;
    access.deposit = id.home;
    access.endpoint = pad.surface + pad.forward * 10.;
    access.ray = {id.home, eye, glm::normalize(access.endpoint - eye),
                  glm::cross(pad.forward, pad.up), 1., observer.seconds};
    access.status = "DEBRIS / CUTTING LOCAL MINERAL";
    return access;
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
    if(target&&surveyExposure(u,o,*target).scanQuality<=.16){
        reset();status_="SCAN SAFETY HOLD / ELECTRICAL STORM / CLIMB OR SHELTER";return false;
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
    const double signal=surveyExposure(u,o,*target).scanQuality;
    progress_ = std::min(8., progress_ + dt * rate * signal);
    status_ = "SCANNING / " + std::to_string(int(progress_ / 8 * 100)) + "% / HOLD F12"+
        (signal<.8?" / WEATHER SIGNAL "+std::to_string(int(signal*100))+"%":"");
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
    const bool supply=std::any_of(state_.contracts.begin(),state_.contracts.end(),[](const auto& j){return j.supplyKg>0;});
    out << std::setprecision(17) << (supply?"INFINITY_EXPLORATION 9 ":state_.debris.empty() ? "INFINITY_EXPLORATION 7 " :
                                                            "INFINITY_EXPLORATION 8 ") << state_.scannerLevel << ' '
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
    for (const auto& c : state_.contracts) {
        out << c.id << ' ' << std::quoted(c.issuer.str()) << ' ' << std::quoted(c.target.str()) << ' '
            << c.grade << ' ' << unsigned(c.phase) << ' ' << c.reward << ' ' << c.escrow << ' '
            << c.acceptedRevision << ' ' << c.cavern << ' ' << c.previous << ' ' << c.accounting.recorded << ' '
            << c.accounting.equipment << ' ' << c.accounting.ammunition << ' ' << c.accounting.repairs << ' ' << c.accounting.paid << ' '
            << c.accounting.lastCharge << ' ' << c.accounting.probesUsed << ' ' << c.accounting.extractedKg << ' '
            << c.accounting.departureHull << ' ' << c.accounting.returnHull;
        if(supply)out<<' '<<c.supplyResource<<' '<<c.supplyKg<<' '<<c.supplyMinedBaseline<<' '
            <<std::quoted(c.supplySite?c.supplySite->str():std::string());
        out<<'\n';
    }
    out << state_.seams.size() << '\n';
    for (const auto& s : state_.seams)
        out << std::quoted(s.source.str()) << ' ' << std::quoted(s.pool.str()) << ' '
            << s.entryMetres.x << ' ' << s.entryMetres.y << ' ' << s.entryMetres.z << ' '
            << s.beganRevision << ' ' << s.scannedRevision << ' '
            << std::quoted(s.material ? s.material->str() : std::string()) << ' '
            << s.resource << ' ' << s.marked << ' ' << s.minedKg << '\n';
    out << std::quoted(serializeProbes(state_.probes)) << '\n';
    if (supply || !state_.debris.empty()) {
        out << state_.debris.size() << '\n';
        for (const auto& rock : state_.debris) {
            out << std::quoted(rock.id.str());
            for (auto kg : rock.extracted)
                out << ' ' << kg;
            out << '\n';
        }
    }
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
            !matchesSaveTag(h, "INFINITY_EXPLORATION") || v < 1 || v > 9 || s.scannerLevel < 1 || s.scannerLevel > 3 ||
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
            if(v>=5) { unsigned cavern; if(!(in>>cavern)||cavern>1)return false;c.cavern=cavern; }
            if(v>=6){unsigned recorded;auto& a=c.accounting;
                if(!(in>>c.previous>>recorded>>a.equipment>>a.ammunition>>a.repairs>>a.paid>>a.lastCharge>>a.probesUsed>>a.extractedKg>>a.departureHull>>a.returnHull)||recorded>1||c.previous==c.id||
                    a.equipment< -1000000000000LL||a.equipment>1000000000000LL||a.ammunition<0||a.ammunition>1000000000000LL||a.repairs<0||a.repairs>1000000000000LL||
                    a.paid!=(phase==unsigned(SurveyPhase::Complete)&&c.cavern?c.reward:0)||a.probesUsed>maxBoreholes||a.extractedKg>s.minedKg||
                    !std::isfinite(a.departureHull)||!std::isfinite(a.returnHull)||a.departureHull<0||a.departureHull>100||a.returnHull<0||a.returnHull>100)return false;
                a.recorded=recorded;
            }else if(c.cavern&&phase==unsigned(SurveyPhase::Complete))c.accounting.paid=c.reward;
            if(v>=9){std::string site;
                if(!(in>>c.supplyResource>>c.supplyKg>>c.supplyMinedBaseline>>std::quoted(site))||c.supplyResource>3||
                    c.supplyKg<0||c.supplyKg>60||c.supplyMinedBaseline>s.minedKg)return false;
                if(!site.empty())c.supplySite=Address::parse(site);
                if(bool(c.supplyKg)!=bool(c.supplySite)||(c.supplyKg&&(c.supplyKg<20||!c.cavern||c.previous))||
                    (!c.supplyKg&&(c.supplyResource||c.supplyMinedBaseline)))return false;
            }
            c.issuer = ActivityId::parse(issuer);
            c.target = Address::parse(target);
            c.phase = SurveyPhase(phase);
            if(c.supplySite&&(c.supplySite->kind()!=Kind::Surface||c.supplySite->parent()!=c.target||
                c.issuer.home!=c.target||prospectAddress(u,*c.supplySite)!=c.supplySite))return false;
            const bool reserveSurvey = v >= 7 && !c.cavern && c.target.kind() == Kind::Surface &&
                c.target.parent() == c.issuer.home && prospectAddress(u, c.target) == c.target;
            if (c.issuer.home.kind() != Kind::Planet || (c.target.kind() != Kind::Planet && !reserveSurvey) ||
                surveySystem(c.issuer.home) != surveySystem(c.target) ||
                ((phase == 1) ? c.escrow != c.reward : c.escrow != 0))
                return false;
            c.name = u.describe(c.target).label();
            if (reserveSurvey)
                c.name = "RESERVE SURVEY / " + c.name;
            if(c.cavern) {
                if(!hasSolidSurface(u.describe(c.target)))return false;
                c.name="CAVERN ASSAY / "+c.name;
            }
            if(c.supplyKg)c.name="LOCAL SUPPLY / "+std::to_string(c.supplyKg)+" KG "+std::string(resourceName(c.supplyResource))+" / "+u.describe(c.issuer.home).label();
            s.contracts.push_back(c);
        }
        if(v>=6){std::set<uint64_t> parents;
            for(const auto& c:s.contracts)if(c.previous){
                const auto p=std::find_if(s.contracts.begin(),s.contracts.end(),[&](const auto& j){return j.id==c.previous;});
                if(!parents.insert(c.previous).second||p==s.contracts.end()||!p->cavern||!c.cavern||p->phase!=SurveyPhase::Complete||p->issuer!=c.issuer)return false;
                std::set<uint64_t> chain;const SurveyContract* current=&c;
                while(current->previous){if(!chain.insert(current->id).second)return false;
                    auto next=std::find_if(s.contracts.begin(),s.contracts.end(),[&](const auto& j){return j.id==current->previous;});
                    if(next==s.contracts.end())return false;current=&*next;}
            }
        }
        if (v >= 3) {
            ids.clear();
            if (!(in >> count) || count > maxSeamExpeditions)
                return false;
            for (size_t i = 0; i < count; ++i) {
                SeamExpedition seam;
                std::string source, pool, sample;
                unsigned marked;
                if (!(in >> std::quoted(source) >> std::quoted(pool) >> seam.entryMetres.x >>
                      seam.entryMetres.y >> seam.entryMetres.z >> seam.beganRevision >>
                      seam.scannedRevision >> std::quoted(sample) >> seam.resource >> marked >> seam.minedKg) ||
                    !finite(seam.entryMetres) || seam.resource >= 4 || marked > 1 ||
                    seam.beganRevision > s.revision || seam.scannedRevision > s.revision ||
                    seam.minedKg > s.minedKg)
                    return false;
                seam.source = Address::parse(source);
                seam.pool = Address::parse(pool);
                seam.marked = marked;
                if (seam.source.kind() != Kind::Rock || !ids.insert(seam.source).second ||
                    prospectAddress(u, seam.source) != std::optional<Address>(seam.pool))
                    return false;
                SurveyObserver entry;
                entry.reference = seam.pool.parent();
                entry.position = seam.entryMetres;
                const auto pose = u.relative(seam.source, entry.reference, 0);
                const auto facing = glm::normalize(pose.position - entry.position);
                entry.attitude = glm::quatLookAtRH(facing, std::abs(facing.y) < .9
                                                              ? glm::dvec3(0, 1, 0)
                                                              : glm::dvec3(1, 0, 0));
                if (!seamEntryPoint(u, entry, seam.source))
                    return false;
                if (!sample.empty()) {
                    seam.material = Address::parse(sample);
                    if (!seam.source.ancestorOf(*seam.material) ||
                        seam.scannedRevision <= seam.beganRevision ||
                        std::none_of(s.journal.begin(), s.journal.end(), [&](const auto& r) {
                            return r.address == *seam.material && r.revision >= seam.scannedRevision;
                        }))
                        return false;
                } else if (seam.marked || seam.scannedRevision)
                    return false;
                s.seams.push_back(std::move(seam));
            }
        }
        if (v >= 4) {
            std::string probes;
            if (!(in >> std::quoted(probes)) || !restoreProbes(s.probes, probes, u)) return false;
            if (s.miningMount < maxWeaponMounts && s.probes.launchers[s.miningMount]) return false;
            for(const auto& b:s.probes.holes)for(const auto& f:b.finds)
                if(f.revision>s.revision||f.minedRevision>s.revision||f.minedKg>s.minedKg)return false;
        }
        if (v >= 8) {
            if (!(in >> count) || count > maxProspects)
                return false;
            std::set<ActivityId> seen;
            uint64_t removed = 0;
            for (size_t i = 0; i < count; ++i) {
                DebrisReserve entry;
                std::string id;
                if (!(in >> std::quoted(id)))
                    return false;
                entry.id = ActivityId::parse(id);
                if (!isOrbitalDebris(entry.id) || !seen.insert(entry.id).second)
                    return false;
                const auto world = u.describe(entry.id.home);
                const auto body = sampleActivity(world, entry.id.slot, 0);
                if (!landableDebris(body))
                    return false;
                const auto capacity = debrisDeposit(world, body).capacity;
                for (size_t k = 0; k < capacity.size(); ++k) {
                    if (!(in >> entry.extracted[k]) || entry.extracted[k] < 0 ||
                        entry.extracted[k] > capacity[k])
                        return false;
                    removed += uint64_t(entry.extracted[k]);
                }
                s.debris.push_back(entry);
            }
            if (removed > s.minedKg)
                return false;
        }
        for(const auto& c:s.contracts)if(c.supplyKg&&c.supplyMinedBaseline>surfaceSupplyMined(s,c))return false;
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
    size_t n = sizeof(*this) + state_.probes.residentBytes() - sizeof(MiningProbeState) +
               state_.journal.capacity() * sizeof(SurveyRecord) +
               state_.prospects.capacity() * sizeof(Prospect) +
               state_.debris.capacity() * sizeof(DebrisReserve) +
               state_.seams.capacity() * sizeof(SeamExpedition) +
               state_.contracts.capacity() * sizeof(SurveyContract);
    for (const auto& r : state_.journal)
        n += r.address.path.capacity() * sizeof(Segment) + r.name.capacity() + r.formation.capacity() +
             r.mineral.capacity() + r.conditions.capacity() + r.composition.capacity() +
             (r.prospect ? r.prospect->path.capacity() * sizeof(Segment) : 0);
    for (const auto& p : state_.prospects)
        n += p.address.path.capacity() * sizeof(Segment);
    for (const auto& s : state_.seams)
        n += (s.source.path.capacity() + s.pool.path.capacity() +
              (s.material ? s.material->path.capacity() : 0)) * sizeof(Segment);
    for (const auto& c : state_.contracts)
        n += (c.issuer.home.path.capacity() + c.target.path.capacity()) * sizeof(Segment) + c.name.capacity();
    return n;
}
} // namespace infinity
