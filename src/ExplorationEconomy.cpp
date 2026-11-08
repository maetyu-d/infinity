#include "infinity/Economy.hpp"
#include "infinity/OrbitalDebris.hpp"
#include <algorithm>
#include <numeric>

namespace infinity {
namespace {
constexpr std::array resources{Good::Ice, Good::Ore, Good::Silicate, Good::Carbon};
Stock hardware(unsigned level, bool mining) {
    Stock out{};
    if (!level)
        return out;
    out[size_t(Good::Electronics)] = mining ? 2 * level : 3 * (level - 1);
    out[size_t(Good::Silicon)] = mining ? level : 2 * (level - 1);
    out[size_t(Good::Alloy)] = mining ? 6 * level : 0;
    out[size_t(Good::Tools)] = mining ? 2 * level : level - 1;
    return out;
}
bool activePort(const EconomySnapshot& s, size_t i) {
    return i < s.sites.size() && s.sites[i].outageUntil <= s.tick && s.sites[i].health >= 500 &&
           s.sites[i].condition >= 300;
}
} // namespace
std::array<unsigned, maxWeaponMounts> ExplorationState::stationLoadout(const EconomyPlayer& p) const {
    auto levels = p.weaponLoadout();
    if (miningLevel && miningMount < maxWeaponMounts)
        levels[miningMount] = miningLevel;
    for (unsigned k = 0; k < maxWeaponMounts; ++k)
        if (probes.launchers[k]) levels[k] = 1;
    return levels;
}
WeaponQuote economicExplorationQuote(const EconomySnapshot& s, size_t i, bool mining, unsigned mount,
                                     bool sell) {
    WeaponQuote q;
    if (!s.network || !activePort(s, i) || mount >= maxWeaponMounts) {
        q.status = "PORT EQUIPMENT CREW UNAVAILABLE";
        return q;
    }
    const auto& x = s.exploration;
    if (mining && x.probes.launchers[mount]) {
        q.status = "PROBE LAUNCHER FITTED / REMOVE IT IN SURVEY SERVICES FIRST";
        return q;
    }
    const unsigned current = mining ? x.miningLevel : x.scannerLevel;
    if (sell && (!mining || !current || x.miningMount != mount)) {
        q.status = "NO MINING HEAD AT THIS MOUNT";
        return q;
    }
    if (!sell && current >= 3) {
        q.status = "EQUIPMENT AT MAXIMUM GRADE III";
        return q;
    }
    if (mining && !sell && (s.player.weaponAt(mount) || (current && x.miningMount != mount))) {
        q.status = "EMPTY THIS MOUNT FIRST / SELL LASER OR EXISTING MINING HEAD";
        return q;
    }
    const unsigned next = sell ? current : current + 1;
    q.materials = hardware(next, mining);
    if (!sell) {
        const auto installed = hardware(current, mining);
        for (size_t g = 0; g < goodCount; ++g)
            q.materials[g] -= installed[g];
        q.labourCents = (mining ? 20000 : 35000) * next;
    }
    bool stock = true;
    int64_t kg = 0;
    for (size_t g = 0; g < goodCount; ++g)
        if (q.materials[g]) {
            const auto price = s.quote(i, Good(g), q.materials[g]);
            q.partsCents += price.buyCents;
            q.sellCents += price.sellCents;
            stock &= price.available >= q.materials[g];
            kg += q.materials[g];
        }
    const auto& site = s.sites[i];
    const bool room = std::accumulate(site.stock.begin(), site.stock.end(), int64_t(0)) + kg <=
                      economicCapacity(s.network->sites[i], site);
    q.available = sell ? (room && site.cash >= q.sellCents) : (stock && s.player.credits >= q.totalCents());
    q.status = q.available
                   ? (sell ? "RETURN HARDWARE / PORT-FUNDED BUYBACK" : "READY / PORT PARTS AND INSTALLATION")
               : sell   ? "PORT NEEDS SPACE OR BUYBACK FUNDS"
               : !stock ? "PORT NEEDS UNRESERVED EQUIPMENT PARTS"
                        : "INSUFFICIENT CREDITS";
    return q;
}
void ExplorationLedger::offer(const Universe& u, Economy& e) {
    if (e.network_->sites.empty() || state_.contracts.size() >= maxSurveyContracts)
        return;
    const auto system = e.network_->system;
    const auto parent = u.describe(system);
    const auto count = u.planetCount(parent);
    if (!count)
        return;
    for (unsigned k = 0; k < 4 && state_.contracts.size() < maxSurveyContracts; ++k) {
        const auto& issuer = e.network_->sites[std::min<size_t>(k, e.network_->sites.size() - 1)];
        SurveyContract j;
        j.issuer = issuer.id;
        j.grade = k + 1;
        j.id = mix64(seedOf(system, u.seed()) ^ 0x5355525645593533ULL ^ mix64(k));
        if (!j.id)
            j.id = 1;
        if (std::any_of(state_.contracts.begin(), state_.contracts.end(),
                        [&](const auto& x) { return x.id == j.id; }))
            continue;
        j.target = system.child(Kind::Planet, (mix64(j.id) % count + k) % count);
        if(k==3) {
            bool solid=false;
            for(uint64_t offset=0;offset<count;++offset) {
                const auto a=system.child(Kind::Planet,(j.target.id()+offset)%count);
                if(hasSolidSurface(u.describe(a))){j.target=a;solid=true;break;}
            }
            if(!solid)continue;
            j.cavern=true;j.grade=2;
        }
        j.name = u.describe(j.target).label();
        if(j.cavern)j.name="CAVERN ASSAY / "+j.name;
        j.reward = 30000 * (k + 1);
        state_.contracts.push_back(j);
    }
    offerFollowup(u,e);
    offerSurfaceSupply(u,e);
    constexpr std::array goods{Good::Ice, Good::Ore, Good::Silicate, Good::Carbon};
    if (state_.contracts.size() >= maxSurveyContracts || std::any_of(state_.contracts.begin(), state_.contracts.end(),
            [&](const auto& j) { return j.target.kind() == Kind::Surface && surveySystem(j.target) == system &&
                                       j.phase <= SurveyPhase::Accepted; }))
        return;
    for (size_t i = 0; i < e.network_->sites.size(); ++i) {
        const auto& site = e.network_->sites[i];
        const auto& stock = e.sites_[i];
        if (site.orbital || site.localWorks.empty() || stock.cash < 52000)
            continue;
        for (unsigned resource = 0; resource < goods.size(); ++resource) {
            const auto g = size_t(goods[resource]);
            if (site.reserves[g] <= 0 || stock.extracted[g] <= 0 ||
                site.reserves[g] - stock.extracted[g] > site.reserves[g] / 10)
                continue;
            const auto key = mix64(site.seed ^ mix64(resource) ^ 0x5245534552564531ULL);
            const auto id = key ? key : 1;
            if (std::any_of(state_.contracts.begin(), state_.contracts.end(), [&](const auto& j) { return j.id == id; }))
                continue;
            const auto planet = u.describe(site.id.home);
            if (!hasSolidSurface(planet))
                continue;
            const auto origin = glm::normalize(site.metres);
            const auto tangent = tangentFrame(origin);
            const auto old = encodeTile(tileAt(origin, 7));
            const double radius = std::pow(10., planet.frame.logRadius);
            const Random random{key};
            std::optional<Address> best;
            int64_t bestKg = 20;
            for (unsigned sample = 0; sample < 12; ++sample) {
                const double angle = (sample / 6 ? .43 : 0.) + (sample % 6 + random.unit(0)) * 1.0471975512;
                const double reach = sample < 6 ? .021 : .037;
                const auto tile = tileAt(glm::normalize(origin + tangent * glm::dvec3(std::cos(angle) * reach, 0,
                                                                                   std::sin(angle) * reach)), 7);
                if (encodeTile(tile) == old)
                    continue;
                const auto direction = tileDirection(tile);
                const double height = u.terrainHeight(planet, direction);
                if ((planet.geology->liquidWater > .1 && height / radius < -.0008) ||
                    glm::dot(direction, u.terrainNormal(planet, direction)) < .985)
                    continue;
                const auto target = planet.address.child(Kind::Surface, encodeTile(tile));
                auto kg = prospectResources(u, target)[resource];
                for (const auto& saved : state_.prospects)
                    if (saved.address == target)
                        kg = saved.capacity[resource] - saved.extracted[resource];
                if (kg > bestKg) {
                    best = target;
                    bestKg = kg;
                }
            }
            if (best) {
                SurveyContract job;
                job.id = id;
                job.issuer = site.id;
                job.target = *best;
                job.reward = 52000;
                job.name = "RESERVE SURVEY / " + u.describe(*best).label();
                state_.contracts.push_back(std::move(job));
                return;
            }
        }
    }
}
std::string ExplorationLedger::executeOrder(const Universe& u, Economy& e, EconomyPlayer& p,
                                       const EconomicCommand& c) {
    if (!c.sequence || c.sequence <= p.lastCommand)
        return "ORDER ALREADY PROCESSED";
    p.lastCommand = c.sequence;
    if (c.action == EconomicAction::ExtractOrbitalDebris) {
        if (!c.observer || !isOrbitalDebris(c.port) || c.port.home.parent() != e.network_->system ||
            !std::isfinite(c.workSeconds) || c.workSeconds < 6 || !state_.miningLevel ||
            c.weaponMount != state_.miningMount || !c.combatPresent)
            return "DEBRIS MINING REQUIRES A FITTED HEAD AND COMPLETED CUT";
        if (!debrisMiningAccess(u, *c.observer, c.port).allowed)
            return "DEBRIS CONTACT LOST / NO CARGO TRANSFER";
        const auto home = u.describe(c.port.home);
        const auto body = sampleActivity(home, c.port.slot, c.observer->seconds);
        const auto deposit = debrisDeposit(home, body);
        auto found = std::find_if(state_.debris.begin(), state_.debris.end(),
                                  [&](const auto& r) { return r.id == c.port; });
        if (found == state_.debris.end()) {
            if (state_.debris.size() >= maxProspects)
                return "DEBRIS REGISTER FULL / DEPLETION RECORDS RETAINED";
            state_.debris.push_back({c.port, {}});
            found = std::prev(state_.debris.end());
        }
        std::array<size_t, 4> order{0, 1, 2, 3};
        std::stable_sort(order.begin(), order.end(),
                         [&](size_t a, size_t b) { return deposit.capacity[a] > deposit.capacity[b]; });
        int64_t budget = 10 * state_.miningLevel, taken = 0;
        bool remaining = false;
        for (auto i : order) {
            const auto good = resources[i];
            const auto g = size_t(good);
            const auto kg = std::max<int64_t>(0, std::min({deposit.capacity[i] - found->extracted[i],
                budget, p.capacityKg - p.massKg(), (p.capacityLitres - p.volumeLitres()) / goodInfo(good).litresPerKg}));
            found->extracted[i] += kg;
            remaining |= found->extracted[i] < deposit.capacity[i];
            p.cargo[g] += kg;
            budget -= kg;
            taken += kg;
            if (!e.sites_.empty()) {
                e.sites_[0].imported[g] += kg;
                e.sites_[0].exported[g] += kg;
            }
        }
        state_.minedKg += uint64_t(taken);
        return taken ? "DEBRIS CUT / " + std::to_string(taken) + " KG TO HOLD / F2 SELL"
                     : remaining ? "HOLD FULL / DEBRIS RESERVE RETAINED" : "FRAGMENT DEPLETED";
    }
    const auto targetSystem = c.surveyTarget ? surveySystem(*c.surveyTarget) : std::nullopt;
    if (c.action == EconomicAction::ControlProbe || c.action == EconomicAction::ScanCavern ||
        c.action == EconomicAction::MarkCavern || c.action == EconomicAction::ExtractCavern)
        return executeCavern(u,e,p,c);
    if (c.action == EconomicAction::LaunchProbe)
        return executeProbe(u, e, p, c);
    if (c.action == EconomicAction::BeginSeam || c.action == EconomicAction::MarkSeam) {
        if (!c.surveyTarget || !c.observer || targetSystem != std::optional<Address>(e.network_->system))
            return "SEAM TARGET OUTSIDE ACTIVE SYSTEM";
        if (c.action == EconomicAction::BeginSeam) {
            const auto entry = seamEntryPoint(u, *c.observer, *c.surveyTarget);
            const auto pool = prospectAddress(u, *c.surveyTarget);
            if (!entry || !pool)
                return "BEGIN AT FULL SIZE / STOP IN THE OUTCROP CLEARING / BELOW 100 M";
            if (std::none_of(state_.journal.begin(), state_.journal.end(), [&](const auto& r) {
                    return r.prospect == pool;
                }))
                return "SURVEY THE SURFACE FIRST / HOLD F12";
            auto found = std::find_if(state_.seams.begin(), state_.seams.end(),
                                      [&](const auto& s) { return s.source == *c.surveyTarget; });
            if (found == state_.seams.end()) {
                if (state_.seams.size() >= maxSeamExpeditions)
                    return "SEAM REGISTER FULL / EXISTING MARKS RETAINED";
                SeamExpedition s;
                s.source = *c.surveyTarget;
                s.pool = *pool;
                s.entryMetres = *entry;
                s.beganRevision = state_.revision;
                state_.seams.push_back(s);
            } else {
                found->entryMetres = *entry;
                std::rotate(found, std::next(found), state_.seams.end());
            }
            return "SEAM EXPEDITION / ENTRY BEACON SAVED / Z SHRINK / J APPROACH / F12 INSPECT";
        }
        const auto* active = activeSeam(state_, e.network_->system);
        if (!active)
            return "BEGIN A SEAM EXPEDITION AT THE SURFACE FIRST";
        auto& seam = state_.seams[size_t(active - state_.seams.data())];
        if (!seam.material || *seam.material != *c.surveyTarget ||
            seam.scannedRevision <= seam.beganRevision ||
            !seamSurveyAccess(u, *c.observer, seam, *c.surveyTarget))
            return "MARK REQUIRES A FRESH CLOSE SURVEY / SHRINK / STOP / HOLD F12";
        seam.marked = true;
        return "MATERIAL MARKED / " + std::string(resourceName(seam.resource)) +
               " / SHIFT-J RETURN TO ENTRY / SHIFT-Z FULL SIZE / CTRL-SPACE MINE";
    }
    if (c.action == EconomicAction::ScanSurvey || c.action == EconomicAction::ExtractProspect) {
        if (!c.surveyTarget || !c.observer || targetSystem != std::optional<Address>(e.network_->system) ||
            !std::isfinite(c.workSeconds))
            return "EXPLORATION TARGET OUTSIDE ACTIVE SYSTEM";
        if (c.action == EconomicAction::ScanSurvey) {
            if (c.workSeconds < 8 || !surveyAccess(u, *c.observer, *c.surveyTarget))
                return "SCAN REQUIRES COMPLETED OBSERVATION / APPROACH AND FACE TARGET";
            auto found = std::find_if(state_.journal.begin(), state_.journal.end(),
                                      [&](const auto& r) { return r.address == *c.surveyTarget; });
            if (found == state_.journal.end() && state_.journal.size() >= maxSurveyRecords)
                return "SURVEY JOURNAL FULL / RECORDS PRESERVED";
            if (state_.revision >= 1000000000000ULL)
                return "SURVEY REVISION LIMIT";
            const auto precision =
                std::min(3u, state_.scannerLevel + unsigned(c.serviceUnits >= 110 && c.serviceUnits <= 150));
            if (found != state_.journal.end() && precision < found->grade)
                return "LOWER SCAN PRECISION / HIGHER-GRADE RECORD RETAINED / REST CREW OR UPGRADE SCANNER";
            auto record = surveyReading(u, *c.surveyTarget, precision, c.observer->seconds);
            if (found != state_.journal.end()) {
                record.grade = std::max(record.grade, found->grade);
                record = surveyReading(u, *c.surveyTarget, record.grade, c.observer->seconds);
                record.paidGrade = found->paidGrade;
            }
            if (record.prospect)
                for (const auto& pool : state_.prospects)
                    if (pool.address == *record.prospect) {
                        record.resources = pool.capacity;
                        break;
                    }
            record.revision = ++state_.revision;
            const bool first = found == state_.journal.end();
            if (first)
                state_.journal.push_back(record);
            else
                *found = record;
            if (const auto* active = activeSeam(state_, e.network_->system)) {
                auto& seam = state_.seams[size_t(active - state_.seams.data())];
                if (seamSurveyAccess(u, *c.observer, seam, record.address)) {
                    {
                        seam.marked = false;
                        seam.material = record.address;
                        seam.scannedRevision = record.revision;
                        auto stock = prospectResources(u, seam.pool);
                        for (const auto& pool : state_.prospects)
                            if (pool.address == seam.pool)
                                for (unsigned k = 0; k < 4; ++k)
                                    stock[k] = pool.capacity[k] - pool.extracted[k];
                        constexpr std::array value{1.5, 4., 1., 2.};
                        double best = -1;
                        for (unsigned k = 0; k < 4; ++k) {
                            const double score = stock[k] > 0 ? record.fractions[k] * value[k] : -1.;
                            if (score > best) {
                                best = score;
                                seam.resource = k;
                            }
                        }
                        if (best <= 0) {
                            seam.material.reset();
                            seam.scannedRevision = 0;
                            return "SEAM SURVEY RECORDED / NO RECOVERABLE MATERIAL REMAINS";
                        }
                    }
                    return "CLOSE SEAM SURVEY / " + std::string(resourceName(seam.resource)) +
                           " / CTRL-F12 MARK MATERIAL";
                }
                if (seam.source.ancestorOf(record.address))
                    return "SEAM INSPECTION / FLY TO THE ROCK FACE / BRAKE / SHRINK FURTHER IF NEEDED";
            }
            return std::string(first ? "DISCOVERY RECORDED / " : "SURVEY UPDATED / ") + record.name +
                   " / F11 JOURNAL";
        }
        if (!state_.miningLevel || c.weaponMount != state_.miningMount || c.workSeconds < 6)
            return "FIT AND OPERATE MINING HEAD FIRST";
        const auto* marked = markedSeam(state_, *c.surveyTarget);
        auto access = miningAccess(u, *c.observer, *c.surveyTarget, state_.miningMount, {}, marked);
        if (!access.allowed || !c.combatPresent)
            return "MINING ACCESS LOST / NO CARGO TRANSFER";
        bool known = false;
        for (const auto& r : state_.journal) {
            auto key = prospectAddress(u, r.address);
            if (key && *key == access.deposit) {
                known = true;
                break;
            }
        }
        if (!known)
            return "UNSURVEYED PROSPECT / HOLD F12 FIRST";
        auto found = std::find_if(state_.prospects.begin(), state_.prospects.end(),
                                  [&](const auto& a) { return a.address == access.deposit; });
        if (found == state_.prospects.end()) {
            if (state_.prospects.size() >= maxProspects)
                return "PROSPECT REGISTER FULL / DEPLETION RECORDS RETAINED";
            state_.prospects.push_back({access.deposit, {}, prospectResources(u, access.deposit)});
            found = std::prev(state_.prospects.end());
        }
        const auto total = found->capacity;
        int64_t taken = 0, budget = (marked ? 20 : 10) * state_.miningLevel;
        for (size_t step = 0; step < resources.size(); ++step) {
            const size_t slot = marked ? marked->resource : step;
            const auto good = resources[slot];
            const auto g = size_t(good);
            const auto kg = std::max<int64_t>(
                0, std::min({total[slot] - found->extracted[slot], budget, p.capacityKg - p.massKg(),
                             (p.capacityLitres - p.volumeLitres()) / goodInfo(good).litresPerKg}));
            found->extracted[slot] += kg;
            p.cargo[g] += kg;
            taken += kg;
            budget -= kg;
            // Physical extraction enters the player economy and is exported immediately.
            if (!e.sites_.empty()) {
                e.sites_[0].imported[g] += kg;
                e.sites_[0].exported[g] += kg;
            }
            if (marked)
                break;
        }
        state_.minedKg += uint64_t(taken);
        if (marked)
            state_.seams[size_t(marked - state_.seams.data())].minedKg += uint64_t(taken);
        if (!taken) {
            if (marked && found->extracted[marked->resource] >= total[marked->resource])
                return "MARKED MATERIAL EXHAUSTED / OTHER DISTRICT RESOURCES RETAINED";
            bool exhausted = true;
            for (size_t g = 0; g < 4; ++g)
                exhausted &= found->extracted[g] >= total[g];
            return exhausted ? "PROSPECT DEPLETED / JOURNAL RETAINS SITE"
                             : "HOLD FULL / CARGO AND PROSPECT RETAINED";
        }
        return (marked ? "SEAM EXTRACTED " : "EXTRACTED ") + std::to_string(taken) +
               " KG / REAL MATERIAL IN CARGO / F2 SELL";
    }
    const auto site = e.siteIndex(c.port);
    if (!site)
        return "RETURN TO A LOCAL EXPLORATION PORT";
    if (c.action != EconomicAction::AcceptSurvey && c.action != EconomicAction::CancelSurvey) {
        if (!c.observer || c.observer->field || !std::isfinite(c.observer->size) || c.observer->size < .99 ||
            c.observer->hull <= 0 || !std::isfinite(glm::length(c.observer->velocity)) ||
            glm::length(c.observer->velocity) > 3)
            return "EXPLORATION SERVICES REQUIRE A PHYSICAL PORT LINK";
        const auto& d = e.network_->sites[*site];
        const auto& o = *c.observer;
        const auto eye =
            o.reference == d.id.home
                ? o.position
                : u.pointRelative(o.reference,
                                  o.position / std::pow(10., u.describe(o.reference).frame.logRadius),
                                  d.id.home, 0);
        const auto range = glm::length(eye - d.metres);
        if (!std::isfinite(range) || !(d.orbital ? range < d.radius * 5 + 250 : o.landed && range < 250))
            return "APPROACH AND STOP AT THE ISSUING PORT / LAND AT SURFACE PORTS";
    }
    if (c.action == EconomicAction::UpgradeScanner || c.action == EconomicAction::FitMiningHead ||
        c.action == EconomicAction::SellMiningHead) {
        EconomySnapshot s;
        s.network = e.network_;
        s.sites = e.sites_;
        s.tick = e.tick_;
        s.player = p;
        s.exploration = state_;
        const bool mining = c.action != EconomicAction::UpgradeScanner,
                   sell = c.action == EconomicAction::SellMiningHead;
        const auto q = economicExplorationQuote(s, *site, mining, c.weaponMount, sell);
        if (!q.available)
            return q.status;
        if (sell ? c.limitCents > q.sellCents : c.limitCents < q.totalCents())
            return "EQUIPMENT QUOTE CHANGED / REVIEW PRICE";
        auto& port = e.sites_[*site];
        for (size_t g = 0; g < goodCount; ++g) {
            port.stock[g] += (sell ? 1 : -1) * q.materials[g];
            (sell ? port.imported[g] : port.exported[g]) += q.materials[g];
        }
        if (sell) {
            port.cash -= q.sellCents;
            p.credits += q.sellCents;
            e.externalMoneyDelta_ -= q.sellCents;
            state_.miningLevel = 0;
            state_.miningMount = noWeaponMount;
            return "MINING HEAD SOLD / HARDWARE RETURNED TO PORT";
        }
        p.credits -= q.totalCents();
        e.externalMoneyDelta_ += q.totalCents();
        port.cash += q.partsCents;
        (e.network_->sites[*site].orbital ? e.carrierCash_ : port.householdCash) += q.labourCents;
        if (mining) {
            ++state_.miningLevel;
            state_.miningMount = c.weaponMount;
            return "MINING HEAD FITTED / " + std::string(weaponMountName(c.weaponMount)) +
                   " / CTRL-SPACE / F9 STAFF EXTRA MOUNT";
        }
        ++state_.scannerLevel;
        return "SCANNER UPGRADED / FASTER OBSERVATIONS / GREATER PRECISION";
    }
    if (c.action == EconomicAction::FitProbeLauncher || c.action == EconomicAction::ReloadProbe ||
        c.action == EconomicAction::SellProbeLauncher)
        return executeProbe(u, e, p, c);
    if (c.action == EconomicAction::SellSurvey) {
        if (!c.surveyTarget)
            return "SELECT A JOURNAL ENTRY";
        auto r = std::find_if(state_.journal.begin(), state_.journal.end(),
                              [&](const auto& x) { return x.address == *c.surveyTarget; });
        if (r == state_.journal.end() || r->paidGrade >= r->grade)
            return "SURVEY DATA ALREADY SOLD / IMPROVE GRADE FOR MORE";
        const int64_t price = (r->grade - r->paidGrade) * (r->address.kind() >= Kind::Grain ? 10000 : 5000);
        auto& port = e.sites_[*site];
        if (port.cash < price || port.outageUntil > e.tick_ || port.health < 500 || port.condition < 300)
            return "SURVEY OFFICE NEEDS FUNDS OR REPAIRS / DATA RETAINED";
        if (p.credits > 1000000000000000LL - price)
            return "BALANCE LIMIT / DATA RETAINED";
        port.cash -= price;
        p.credits += price;
        e.externalMoneyDelta_ -= price;
        state_.surveyEarnings += price;
        r->paidGrade = r->grade;
        return "FIRST-SURVEY DATA SOLD / " + std::to_string(price / 100) + " CR / SAME GRADE PAID ONCE";
    }
    auto job = std::find_if(state_.contracts.begin(), state_.contracts.end(),
                            [&](const auto& j) { return j.id == c.contract && j.issuer == c.port; });
    if (job == state_.contracts.end())
        return "SURVEY CONTRACT NOT FOUND AT THIS PORT";
    auto& port = e.sites_[*site];
    if (c.action == EconomicAction::AcceptSurvey) {
        if (job->phase != SurveyPhase::Open)
            return "SURVEY OFFER CLOSED";
        if (port.cash < job->reward)
            return "ISSUER CANNOT RESERVE SURVEY REWARD";
        port.cash -= job->reward;
        e.externalMoneyDelta_ -= job->reward;
        job->escrow = job->reward;
        job->phase = SurveyPhase::Accepted;
        job->acceptedRevision = state_.revision;
        if(job->supplyKg)job->supplyMinedBaseline=surfaceSupplyMined(state_,*job);
        if(job->cavern&&c.observer&&std::isfinite(c.observer->hull)&&c.observer->hull>0&&c.observer->hull<=100){
            job->accounting.recorded=true;job->accounting.departureHull=job->accounting.returnHull=c.observer->hull;}
        return "SURVEY ACCEPTED / REWARD IN ESCROW / F4 TARGET / HOLD F12";
    }
    if (c.action == EconomicAction::CancelSurvey) {
        if (job->phase != SurveyPhase::Accepted)
            return "NO ACCEPTED SURVEY TO CANCEL";
        port.cash += job->escrow;
        e.externalMoneyDelta_ += job->escrow;
        job->escrow = 0;
        job->phase = SurveyPhase::Cancelled;
        return "SURVEY CANCELLED / RESERVED FUNDS RETURNED TO ISSUER";
    }
    if (c.action != EconomicAction::ClaimSurvey || job->phase != SurveyPhase::Accepted)
        return "SURVEY CONTRACT ALREADY CLOSED";
    if(job->cavern) {
        if(!std::isfinite(c.observer->hull)||c.observer->hull>100)return "ASSAY DELIVERY REQUIRES A VALID SHIP HULL";
        constexpr std::array goods{Good::Ice,Good::Ore,Good::Silicate,Good::Carbon};
        const auto sample=cavernAssaySample(state_,*job,p);
        if(!sample)return job->supplyKg?"SUPPLY EXPEDITION / FRESH ASSAY AND EXTRACTION IN THE MARKED REGION / KEEP REQUIRED CARGO ABOARD":
            "CAVERN ASSAY / NEW CLOSE SURVEY / MARK AND EXTRACT 10 KG / RETURN WITH SAMPLE";
        const auto g=size_t(goods[sample->resource]);
        const int64_t kg=job->supplyKg?job->supplyKg:10;
        if(std::accumulate(port.stock.begin(),port.stock.end(),int64_t(0))+kg>economicCapacity(e.network_->sites[*site],port))
            return "ASSAY PORT HOLD FULL / SAMPLE AND ESCROW RETAINED";
        if(p.credits>1000000000000000LL-job->escrow)return "BALANCE LIMIT / SAMPLE AND ESCROW RETAINED";
        const auto before=economicVitals(e.network_->sites[*site],port,e.tick_);
        p.cargo[g]-=kg;port.stock[g]+=kg;port.imported[g]+=kg;
        p.credits+=job->escrow;state_.surveyEarnings+=job->escrow;job->accounting.paid=job->escrow;
        job->accounting.returnHull=c.observer->hull;
        job->escrow=0;job->phase=SurveyPhase::Complete;++state_.surveysDelivered;
        if(job->supplyKg){Stock delivered{};delivered[g]=kg;
            e.recordOutcome(c,*site,OutcomeKind::Supply,delivered,job->reward,before);
            e.event(e.network_->sites[*site].name+" / LOCAL MINING SUPPLY RECEIVED / "+std::to_string(kg)+" KG "+std::string(resourceName(sample->resource)));
            return "SURFACE SUPPLY COMPLETE / "+std::to_string(kg)+" KG "+std::string(resourceName(sample->resource))+" DELIVERED / "+
                std::to_string(job->reward/100)+" CR / F2 OUTCOMES TRACKS THE PLANT";
        }
        const auto receipt="CAVERN ASSAY COMPLETE / 10 KG SAMPLE HANDED OVER / "+std::to_string(job->reward/100)+" CR";
        offerFollowup(u,e);return receipt;
    }
    auto report = std::find_if(state_.journal.begin(), state_.journal.end(), [&](const auto& r) {
        return r.address == job->target && r.grade >= job->grade && r.revision > job->acceptedRevision;
    });
    if (report == state_.journal.end())
        return "NEW SURVEY REQUIRED / VISIT TARGET AFTER ACCEPTING / CHECK GRADE";
    if (p.credits > 1000000000000000LL - job->escrow)
        return "BALANCE LIMIT / ESCROW RETAINED";
    p.credits += job->escrow;
    state_.surveyEarnings += job->escrow;
    job->escrow = 0;
    job->phase = SurveyPhase::Complete;
    ++state_.surveysDelivered;
    return "SURVEY CONTRACT COMPLETE / " + std::to_string(job->reward / 100) + " CR / JOURNAL RETAINED";
}
} // namespace infinity
