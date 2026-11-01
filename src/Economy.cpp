#include "infinite/Economy.hpp"
#include <algorithm>
#include <charconv>
#include <iomanip>
#include <numeric>
#include <sstream>
namespace infinite {
namespace {
Stock stock(std::initializer_list<std::pair<Good, int64_t>> list) {
    Stock s{};
    for (auto [g, n] : list)
        s[size_t(g)] = n;
    return s;
}
const std::array<GoodInfo, goodCount> goods{
    {{"ICE", 20, 2},           {"WATER", 35, 1},          {"OXYGEN", 95, 8},        {"METAL ORE", 40, 1},
     {"SILICATE", 30, 1},      {"CARBON", 55, 2},         {"ALLOY", 240, 1},        {"SILICON", 420, 1},
     {"POLYMERS", 180, 2},     {"BIOMASS", 90, 3},        {"FOOD", 280, 2},         {"MEDICINE", 1800, 3},
     {"ELECTRONICS", 2300, 2}, {"MACHINERY", 1600, 2},    {"PROPELLANT", 220, 5},   {"FILTERS", 900, 8},
     {"TOOLS", 700, 2},        {"HABITAT PARTS", 650, 3}, {"INSTRUMENTS", 4200, 3}, {"RECYCLATE", 15, 3}}};
const std::array<ProductionRecipe, 18> recipes{
    {{"Ice extraction", {}, stock({{Good::Ice, 30}}), 0, 2, 2},
     {"Metal extraction", {}, stock({{Good::Ore, 20}, {Good::Silicate, 10}, {Good::Carbon, 3}}), 0, 3, 3},
     {"Ice processing", stock({{Good::Ice, 10}}), stock({{Good::Water, 10}}), 0, 2, 2},
     {"Electrolysis", stock({{Good::Water, 9}}), stock({{Good::Oxygen, 8}, {Good::Propellant, 1}}), 0, 2, 4},
     {"Carbon recovery", {}, stock({{Good::Carbon, 8}}), 0, 2, 2},
     {"Smelting", stock({{Good::Ore, 10}, {Good::Carbon, 2}}),
      stock({{Good::Alloy, 6}, {Good::Recyclate, 6}}), 0, 3, 4},
     {"Silicon refining", stock({{Good::Silicate, 10}, {Good::Carbon, 2}}),
      stock({{Good::Silicon, 4}, {Good::Recyclate, 8}}), 0, 3, 4},
     {"Polymer synthesis", stock({{Good::Carbon, 6}, {Good::Propellant, 2}}), stock({{Good::Polymers, 8}}), 0,
      2, 3},
     {"Greenhouse", stock({{Good::Water, 8}, {Good::Carbon, 2}}), stock({{Good::Biomass, 10}}), 0, 2, 2},
     {"Food processing", stock({{Good::Biomass, 8}, {Good::Water, 2}}), stock({{Good::Food, 10}}), 0, 2, 2},
     {"Medical supplies", stock({{Good::Biomass, 4}, {Good::Polymers, 1}, {Good::Water, 1}}),
      stock({{Good::Medicine, 6}}), 0, 3, 3},
     {"Electronics", stock({{Good::Silicon, 4}, {Good::Alloy, 2}, {Good::Polymers, 2}}),
      stock({{Good::Electronics, 8}}), 0, 3, 4},
     {"Machinery", stock({{Good::Alloy, 7}, {Good::Electronics, 1}, {Good::Polymers, 2}}),
      stock({{Good::Machinery, 10}}), 0, 3, 3},
     {"Filters", stock({{Good::Carbon, 2}, {Good::Polymers, 2}, {Good::Alloy, 1}}),
      stock({{Good::Filters, 5}}), 0, 2, 2},
     {"Tools", stock({{Good::Alloy, 5}, {Good::Polymers, 1}}), stock({{Good::Tools, 6}}), 0, 2, 2},
     {"Habitat fabrication", stock({{Good::Alloy, 12}, {Good::Polymers, 4}, {Good::Electronics, 2}}),
      stock({{Good::HabitatParts, 18}}), 0, 3, 4},
     {"Scientific instruments", stock({{Good::Silicon, 3}, {Good::Electronics, 3}, {Good::Alloy, 2}}),
      stock({{Good::Instruments, 8}}), 0, 4, 4},
     {"Recycling", stock({{Good::Recyclate, 10}}),
      stock({{Good::Alloy, 3}, {Good::Silicate, 3}, {Good::Carbon, 1}}), 3, 2, 2}}};
int64_t total(const Stock& s) {
    return std::accumulate(s.begin(), s.end(), int64_t(0));
}
std::string flightRange(double metres) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1);
    if (metres >= 1e9)
        out << metres / 1e9 << " MKM";
    else
        out << metres / 1000 << " KM";
    return out.str();
}
bool cargoOnboard(FreightPhase p) {
    return p == FreightPhase::Transit || p == FreightPhase::Holding;
}
int64_t price(const EconomicSite& d, const EconomicSiteState& s, Good good, int64_t available, bool buy) {
    auto g = size_t(good);
    const int64_t target = std::max<int64_t>(1, d.target[g]);
    const int64_t ratio =
        std::clamp<int64_t>(target * 100 / std::max<int64_t>(1, available + target / 10), 40, 300);
    const int64_t spot = std::max<int64_t>(1, goods[g].baseCents * ratio / 100);
    (void)s;
    return buy ? (spot * 112 + 99) / 100 + 5 : std::max<int64_t>(1, spot * 88 / 100 - 5);
}
EconomyQuote quoted(const EconomicSite& d, const EconomicSiteState& s, Good good, int64_t kg) {
    EconomyQuote q;
    if (size_t(good) >= goodCount || kg < 1 || kg > maxShipPayloadKg)
        return q;
    auto g = size_t(good);
    q.available = s.stock[g] - s.reserved[g];
    auto integrate = [&](bool buy) {
        auto marginal = [&](int64_t i) {
            return price(d, s, good, buy ? std::max<int64_t>(0, q.available - i) : q.available + i + 1, buy);
        };
        // Marginal prices are monotone. Equal endpoints bound a constant-price
        // interval, so large orders retain exact cent totals without a loop
        // over every kilogram on the cockpit thread.
        auto sum = [&](auto&& self, int64_t first, int64_t last, int64_t a, int64_t b) -> int64_t {
            if (a == b)
                return (last - first + 1) * a;
            const auto mid = (first + last) / 2;
            return self(self, first, mid, a, marginal(mid)) + self(self, mid + 1, last, marginal(mid + 1), b);
        };
        return sum(sum, 0, kg - 1, marginal(0), marginal(kg - 1));
    };
    q.buyCents = integrate(true);
    q.sellCents = integrate(false);
    return q;
}
bool readStock(std::istream& in, Stock& s) {
    for (auto& n : s)
        if (!(in >> n) || n < 0 || n > 1000000000000LL)
            return false;
    return true;
}
void writeStock(std::ostream& out, const Stock& s) {
    for (auto n : s)
        out << ' ' << n;
}
} // namespace
const GoodInfo& goodInfo(Good g) {
    return goods.at(size_t(g));
}
std::span<const ProductionRecipe> productionRecipes() {
    return recipes;
}
int64_t EconomyPlayer::massKg() const {
    return total(cargo);
}
int64_t EconomyPlayer::volumeLitres() const {
    int64_t n = 0;
    for (size_t g = 0; g < goodCount; g++)
        n += cargo[g] * goods[g].litresPerKg;
    return n;
}
std::string EconomyPlayer::serialize() const {
    std::ostringstream o;
    o << "INFINITE_WALLET 4 " << credits << ' ' << lastCommand;
    writeStock(o, cargo);
    o << ' ' << deliveries << ' ' << lateDeliveries << ' ' << returnedHauls << ' ' << freightEarnings << ' '
      << bool(haul);
    if (haul) {
        const auto& h = *haul;
        o << ' ' << std::quoted(h.system.str()) << ' ' << h.id << ' ' << h.deadline << ' '
          << std::quoted(h.source.home.str()) << ' ' << h.source.slot << ' '
          << std::quoted(h.destination.home.str()) << ' ' << h.destination.slot << ' ' << unsigned(h.good)
          << ' ' << h.kg << ' ' << h.onboard;
    }
    o << ' ' << cargoLevel << ' ' << repairPoints << ' ' << weaponLevel;
    return o.str();
}
bool EconomyPlayer::restore(std::string_view text) {
    EconomyPlayer p;
    std::istringstream in{std::string(text)};
    std::string h;
    unsigned v;
    if (!(in >> h >> v >> p.credits >> p.lastCommand) || h != "INFINITE_WALLET" || (v < 1 || v > 4) ||
        p.credits < 0 || p.credits > 1000000000000000LL || !readStock(in, p.cargo))
        return false;
    if (v >= 2) {
        unsigned pending;
        if (!(in >> p.deliveries >> p.lateDeliveries >> p.returnedHauls >> p.freightEarnings >> pending) ||
            p.deliveries > 1000000000000ULL || p.lateDeliveries > p.deliveries ||
            p.returnedHauls > 1000000000000ULL || p.freightEarnings < 0 ||
            p.freightEarnings > 1000000000000000LL || pending > 1)
            return false;
        if (pending) {
            PlayerHaul h;
            std::string system, source, destination;
            unsigned good, onboard;
            if (!(in >> std::quoted(system) >> h.id >> h.deadline >> std::quoted(source) >> h.source.slot >>
                  std::quoted(destination) >> h.destination.slot >> good >> h.kg >> onboard) ||
                !h.id || h.id > 1000000000000ULL || h.deadline > 1000000000000ULL || good >= goodCount ||
                h.kg < 10 || h.kg > maxShipPayloadKg || onboard > 1)
                return false;
            try {
                h.system = Address::parse(system);
                h.source.home = Address::parse(source);
                h.destination.home = Address::parse(destination);
            } catch (...) {
                return false;
            }
            h.good = Good(good);
            h.onboard = onboard;
            if (h.system.kind() != Kind::System || h.source.home.kind() != Kind::Planet ||
                h.destination.home.kind() != Kind::Planet || !h.system.ancestorOf(h.source.home) ||
                !h.system.ancestorOf(h.destination.home) || h.source == h.destination ||
                (h.onboard && p.cargo[good] < h.kg))
                return false;
            p.haul = h;
        }
    }
    if (v >= 3 && (!(in >> p.cargoLevel >> p.repairPoints) || p.cargoLevel > maxCargoLevel ||
                   p.repairPoints > 1000000000000ULL))
        return false;
    if (v >= 4 && (!(in >> p.weaponLevel) || p.weaponLevel > maxWeaponLevel))
        return false;
    p.capacityKg = holdCapacityKg(p.cargoLevel);
    p.capacityLitres = holdCapacityLitres(p.cargoLevel);
    in >> std::ws;
    if (!in.eof() || p.massKg() > p.capacityKg || p.volumeLitres() > p.capacityLitres)
        return false;
    *this = p;
    return true;
}
Economy::Economy(std::shared_ptr<const EconomicNetwork> n) : network_(std::move(n)) {
    if (!network_ || network_->sites.size() > 4096 || network_->vessels.size() > 4096)
        throw std::invalid_argument("Invalid economic network");
    for (const auto& d : network_->sites) {
        EconomicSiteState s;
        s.stock = d.initial;
        s.cash = 2000000 + int64_t(d.population) * 150;
        s.householdCash = int64_t(d.population) * 500;
        s.operatingBaseline = s.cash;
        sites_.push_back(std::move(s));
    }
    for (auto id : network_->vessels) {
        EconomicFreight f;
        f.id = id;
        auto i = siteIndex({id.home, 0});
        f.location = unsigned(i.value_or(0));
        f.source = f.destination = f.location;
        freight_.push_back(std::move(f));
    }
    endowmentMoney_ = moneyTotal();
    event("CANONICAL ENDOWMENT / materials, households, firms and public treasury recorded");
}
std::optional<size_t> Economy::siteIndex(const ActivityId& id) const {
    for (size_t i = 0; i < network_->sites.size(); i++)
        if (network_->sites[i].id == id)
            return i;
    return {};
}
void Economy::event(std::string s) {
    if (events_.size() >= 32)
        events_.erase(events_.begin());
    events_.push_back({tick_, std::move(s)});
}
int64_t Economy::unitPrice(size_t i, Good g, int64_t available, bool buy) const {
    return price(network_->sites.at(i), sites_.at(i), g, available, buy);
}
EconomyQuote Economy::quote(size_t i, Good g, int64_t kg) const {
    return quoted(network_->sites.at(i), sites_.at(i), g, kg);
}
void Economy::advanceTo(uint64_t target, unsigned budget) {
    for (unsigned n = 0; tick_ < target && n < budget; n++)
        step();
}
void Economy::outage(const ActivityId& id, uint64_t until) {
    if (auto i = siteIndex(id)) {
        sites_[*i].outageUntil = std::max(tick_, until);
        event(network_->sites[*i].name + " / production outage");
    }
}
void Economy::production(size_t i) {
    const auto& d = network_->sites[i];
    auto& s = sites_[i];
    auto consume = [&](Good good, int64_t wanted, bool paid) {
        auto g = size_t(good);
        int64_t n = std::min(wanted, s.stock[g] - s.reserved[g]);
        auto unit = unitPrice(i, good, s.stock[g] - s.reserved[g], true);
        if (paid)
            n = std::min(n, s.householdCash / unit);
        if (paid) {
            s.householdCash -= n * unit;
            s.cash += n * unit;
        }
        s.stock[g] -= n;
        s.consumed[g] += n;
        return n;
    };
    bool fed = true;
    if (d.population) {
        auto need = std::max<int64_t>(1, int64_t(d.population) / 500);
        fed = consume(Good::Food, need, true) == need;
        fed &= consume(Good::Water, std::max<int64_t>(1, int64_t(d.population) / 150), true) > 0;
        if (d.sealed)
            fed &= consume(Good::Oxygen, std::max<int64_t>(1, int64_t(d.population) / 250), true) > 0;
        s.health = unsigned(std::clamp(int(s.health) + (fed ? 2 : -8), 200, 1000));
        if (tick_ % 12 == 0)
            consume(Good::Medicine, std::max<int64_t>(1, int64_t(d.population) / 5000), true);
    }
    if (tick_ % 12 == 0) {
        const auto machine = size_t(Good::Machinery), filter = size_t(Good::Filters);
        bool repaired = s.stock[machine] > s.reserved[machine] && s.stock[filter] > s.reserved[filter];
        if (repaired) {
            consume(Good::Machinery, 1, false);
            consume(Good::Filters, 1, false);
        }
        s.condition = unsigned(std::clamp(int(s.condition) + (repaired ? 15 : -5), 250, 1000));
        // Public infrastructure purchases and taxes circulate finite money.
        for (Good good : {Good::HabitatParts, Good::Instruments}) {
            auto g = size_t(good);
            if (s.stock[g] <= s.reserved[g] || treasury_ < goods[g].baseCents)
                continue;
            s.stock[g]--;
            s.consumed[g]++;
            treasury_ -= goods[g].baseCents;
            s.cash += goods[g].baseCents;
        }
        const auto tax = s.cash / 1000;
        s.cash -= tax;
        treasury_ += tax;
    }
    s.bottleneck = !fed                    ? "HOUSEHOLDS / ESSENTIAL SUPPLIES"
                   : s.outageUntil > tick_ ? "PLANT OFFLINE"
                                           : "OPERATING";
    if (d.orbital || s.outageUntil > tick_)
        return;
    int64_t workers = int64_t(d.workers) * s.health / 1000 *
                      (4 + s.modules[size_t(FacilityModule::Automation)]) / 4,
            power = economicPower(d, s, tick_);
    if (economicConstructionActive(d, s, tick_)) {
        workers = std::max<int64_t>(0, workers - 2);
        power = std::max<int64_t>(0, power - 1);
    }
    if (recoveryCrew(i))
        workers = std::max<int64_t>(0, workers - 2);
    for (auto r : d.recipes) {
        const auto& recipe = recipes.at(r);
        bool stocked = true;
        for (size_t g = 0; g < goodCount; g++)
            if (recipe.outputs[g] && s.stock[g] - s.reserved[g] < d.target[g] * 3)
                stocked = false;
        if (stocked)
            continue;
        int64_t batches = std::min(
            {int64_t(1 + d.population / 500) * (4 + s.modules[size_t(FacilityModule::Automation)]) / 4,
             workers / recipe.workers, power / recipe.power, s.cash / (recipe.workers * 50)});
        if (batches > 0)
            batches = std::max<int64_t>(1, batches * s.condition / 1000);
        if (batches <= 0) {
            if (s.bottleneck == "OPERATING")
                s.bottleneck = workers < recipe.workers ? "LABOUR / " + std::string(recipe.name)
                               : power < recipe.power   ? "POWER / " + std::string(recipe.name)
                                                        : "PAYROLL / " + std::string(recipe.name);
            continue;
        }
        bool extracting = total(recipe.inputs) == 0;
        std::optional<Good> shortage;
        bool depleted = false;
        for (size_t g = 0; g < goodCount; g++) {
            if (recipe.inputs[g]) {
                auto available = (s.stock[g] - s.reserved[g]) / recipe.inputs[g];
                batches = std::min(batches, available);
                if (!available && !shortage)
                    shortage = Good(g);
            }
            if (extracting && recipe.outputs[g]) {
                auto available = (d.reserves[g] - s.extracted[g]) / recipe.outputs[g];
                batches = std::min(batches, available);
                if (!available && !shortage) {
                    shortage = Good(g);
                    depleted = true;
                }
            }
        }
        int64_t delta = total(recipe.outputs) - total(recipe.inputs);
        if (delta > 0)
            batches = std::min(batches, (economicCapacity(d, s) - total(s.stock)) / delta);
        if (batches <= 0) {
            if (s.bottleneck == "OPERATING")
                s.bottleneck = std::string(recipe.name) + " / " +
                               (shortage ? std::string(depleted ? "DEPLETED " : "MISSING ") +
                                               std::string(goods[size_t(*shortage)].name)
                                         : "WAREHOUSE FULL");
            continue;
        }
        for (size_t g = 0; g < goodCount; g++) {
            s.stock[g] -= recipe.inputs[g] * batches;
            s.consumed[g] += recipe.inputs[g] * batches;
            s.stock[g] += recipe.outputs[g] * batches;
            if (extracting)
                s.extracted[g] += recipe.outputs[g] * batches;
            else
                s.produced[g] += recipe.outputs[g] * batches;
        }
        auto wage = batches * recipe.workers * 50;
        s.cash -= wage;
        s.householdCash += wage;
        workers -= batches * recipe.workers;
        power -= batches * recipe.power;
    }
}
uint64_t Economy::travelTicks(size_t a, size_t b) const {
    const double distance = glm::length(network_->sites[a].systemMetres - network_->sites[b].systemMetres);
    return uint64_t(std::clamp(2. + distance / 2e9, 2., 120.));
}
void Economy::logistics() {
    if (sites_.empty())
        return;
    for (auto& f : freight_) {
        if (f.disabledAt >= 0)
            continue;
        auto& from = sites_[f.source];
        auto& to = sites_[f.destination];
        const auto g = size_t(f.good);
        if (f.phase == FreightPhase::Reposition && tick_ >= f.endTick) {
            f.location = f.source;
            f.phase = FreightPhase::Loading;
            f.startTick = tick_;
            f.endTick = tick_ + 1;
        }
        if (f.phase == FreightPhase::Loading && tick_ >= f.endTick) {
            const int64_t fuel = 1 + int64_t(travelTicks(f.source, f.destination) / 12) + f.kg / 1000;
            auto prop = size_t(Good::Propellant);
            if (from.stock[prop] - from.reserved[prop] < fuel) {
                f.endTick = tick_ + 1;
                continue;
            }
            from.stock[prop] -= fuel;
            from.consumed[prop] += fuel;
            from.stock[g] -= f.kg;
            from.reserved[g] -= f.kg;
            from.cash += f.payment;
            carrierCash_ += f.fee;
            f.payment = f.fee = 0;
            f.phase = FreightPhase::Transit;
            f.startTick = tick_;
            f.endTick = tick_ + travelTicks(f.source, f.destination);
        }
        if ((f.phase == FreightPhase::Transit && tick_ >= f.endTick) || f.phase == FreightPhase::Holding) {
            if (economicCapacity(network_->sites[f.destination], to) - total(to.stock) < f.kg) {
                f.phase = FreightPhase::Holding;
                continue;
            }
            to.stock[g] += f.kg;
            f.location = f.destination;
            f.trips++;
            event(network_->sites[f.destination].name + " / RECEIVED " + std::to_string(f.kg) + " KG " +
                  std::string(goods[g].name));
            f.kg = 0;
            f.phase = FreightPhase::Idle;
        }
    }
    if (tick_ % 3)
        return;
    // Bounded dispatch: eight cheapest surplus sources per good, four new jobs per batch.
    std::array<std::vector<size_t>, goodCount> suppliers;
    std::vector<Stock> inbound(sites_.size());
    for (const auto& f : freight_)
        if (f.phase != FreightPhase::Idle && f.disabledAt < 0)
            inbound[f.destination][size_t(f.good)] += f.kg;
    for (const auto& h : hauls_)
        if (tick_ <= h.deadline &&
            (h.phase == HaulPhase::Open || h.phase == HaulPhase::Reserved || h.phase == HaulPhase::InTransit))
            inbound[h.destination][size_t(h.good)] += h.kg;
    for (size_t g = 0; g < goodCount; g++) {
        for (size_t a = 0; a < sites_.size(); a++)
            if (sites_[a].stock[g] - sites_[a].reserved[g] - network_->sites[a].target[g] >= 10)
                suppliers[g].push_back(a);
        std::stable_sort(suppliers[g].begin(), suppliers[g].end(), [&](size_t a, size_t b) {
            return unitPrice(a, Good(g), sites_[a].stock[g] - sites_[a].reserved[g], true) <
                   unitPrice(b, Good(g), sites_[b].stock[g] - sites_[b].reserved[g], true);
        });
        if (suppliers[g].size() > 8)
            suppliers[g].resize(8);
    }
    unsigned dispatched = 0;
    for (size_t k = 0; k < freight_.size() && dispatched < 4; k++) {
        auto& f = freight_[(k + (tick_ / 3) * 4) % freight_.size()];
        if (f.phase != FreightPhase::Idle || f.disabledAt >= 0)
            continue;
        double best = 0;
        size_t source = 0, destination = 0, selected = 0;
        int64_t amount = 0, cost = 0, fee = 0;
        for (size_t b = 0; b < sites_.size(); b++)
            for (size_t g = 0; g < goodCount; g++) {
                auto need = network_->sites[b].target[g] - sites_[b].stock[g] - inbound[b][g];
                if (need < 10)
                    continue;
                for (size_t a : suppliers[g]) {
                    if (a == b)
                        continue;
                    auto surplus = sites_[a].stock[g] - sites_[a].reserved[g] - network_->sites[a].target[g];
                    auto ask = unitPrice(a, Good(g), sites_[a].stock[g] - sites_[a].reserved[g], true);
                    auto shipping = 30 + int64_t(travelTicks(a, b)) * 2;
                    int64_t n =
                        std::min({surplus, need, int64_t(1000),
                                  economicCapacity(network_->sites[b], sites_[b]) - total(sites_[b].stock),
                                  sites_[b].cash / (ask + shipping)});
                    if (n < 10)
                        continue;
                    const double score =
                        double(need) * shipping / (1 + travelTicks(f.location, a) + travelTicks(a, b));
                    if (score > best) {
                        best = score;
                        source = a;
                        destination = b;
                        selected = g;
                        amount = n;
                        cost = n * ask;
                        fee = n * shipping;
                    }
                }
            }
        if (!amount)
            continue;
        dispatched++;
        f.source = unsigned(source);
        f.destination = unsigned(destination);
        f.good = Good(selected);
        f.kg = amount;
        f.payment = cost;
        f.fee = fee;
        sites_[destination].cash -= cost + fee;
        sites_[source].reserved[selected] += amount;
        inbound[destination][selected] += amount;
        f.startTick = tick_;
        f.endTick = tick_ + travelTicks(f.location, source);
        f.phase = f.location == source ? FreightPhase::Loading : FreightPhase::Reposition;
        if (f.phase == FreightPhase::Loading)
            f.endTick = tick_ + 1;
    }
}
void Economy::offers() {
    for (auto& c : contracts_)
        if ((c.phase == ContractPhase::Open || c.phase == ContractPhase::Accepted) && tick_ > c.deadline) {
            sites_[c.site].cash += c.escrow;
            c.escrow = 0;
            c.phase = ContractPhase::Expired;
            event("CONTRACT EXPIRED / escrow returned");
        }
    std::erase_if(contracts_, [&](const auto& c) {
        return (c.phase == ContractPhase::Complete || c.phase == ContractPhase::Expired) &&
               tick_ > c.deadline + 12;
    });
    if (tick_ % 12)
        return;
    for (size_t i = 0; i < sites_.size(); i++) {
        if (std::any_of(recoveries_.begin(), recoveries_.end(),
                        [&](const auto& j) { return j.site == i && j.phase <= RecoveryPhase::Working; }))
            continue;
        if (std::any_of(contracts_.begin(), contracts_.end(), [&](const auto& c) {
                return c.site == i && (c.phase == ContractPhase::Open || c.phase == ContractPhase::Accepted);
            }))
            continue;
        size_t selected = 0;
        int64_t need = 0;
        for (size_t g = 0; g < goodCount; g++) {
            auto missing = network_->sites[i].target[g] - sites_[i].stock[g];
            if (missing > need) {
                need = missing;
                selected = g;
            }
        }
        if (need < 10)
            continue;
        int64_t reward =
            unitPrice(i, Good(selected), sites_[i].stock[selected] - sites_[i].reserved[selected], true) *
            13 / 10;
        auto n = std::min({need, int64_t(200), sites_[i].cash / reward});
        if (n < 10)
            continue;
        EconomicContract c;
        c.id = nextContract_++;
        c.deadline = tick_ + 144;
        c.site = unsigned(i);
        c.good = Good(selected);
        c.requested = n;
        c.unitCents = reward;
        c.escrow = n * reward;
        sites_[i].cash -= c.escrow;
        contracts_.push_back(c);
        event(network_->sites[i].name + " / FUNDED " + std::string(goods[selected].name) +
              " SUPPLY CONTRACT");
    }
}
void Economy::step() {
    tick_++;
    for (size_t i = 0; i < sites_.size(); i++)
        production(i);
    std::array<unsigned, 4096> crewUsed{};
    recoveryWork(std::span(crewUsed).first(sites_.size()));
    logistics();
    if (tick_ % 12 == 0 && carrierCash_ > 10000000) {
        // The public freight cooperative distributes its surplus to existing households.
        // Retaining a fixed operating reserve prevents permanent monetary hoarding.
        uint64_t population = 0;
        for (const auto& d : network_->sites)
            population += d.population;
        auto surplus = carrierCash_ - 10000000;
        carrierCash_ -= surplus;
        int64_t distributed = 0;
        if (population)
            for (size_t i = 0; i < sites_.size(); i++) {
                const auto people = network_->sites[i].population;
                const auto share = (surplus / int64_t(population)) * int64_t(people) +
                                   (surplus % int64_t(population)) * int64_t(people) / int64_t(population);
                sites_[i].householdCash += share;
                distributed += share;
            }
        treasury_ += surplus - distributed;
    }
    development(std::span(crewUsed).first(sites_.size()));
    recoveryOffers();
    offers();
    haulOffers();
    observeOutcomes();
}
std::string Economy::execute(const EconomicCommand& c, EconomyPlayer& player) {
    if (!c.sequence || c.sequence <= player.lastCommand)
        return "ORDER ALREADY PROCESSED";
    player.lastCommand = c.sequence;
    if (c.action == EconomicAction::DisableVessel)
        return disableVessel(c);
    auto index = siteIndex(c.port);
    if (!index)
        return "PORT HAS NO MARKET";
    if (unsigned(c.action) > unsigned(EconomicAction::DisableVessel))
        return "INVALID ECONOMIC ACTION";
    if (!reconcilePlayer(player))
        return "HAUL SAVE MISMATCH / TRADING PAUSED";
    if (c.action == EconomicAction::FitWeapon || c.action == EconomicAction::SellWeapon)
        return weaponTrade(c, player, *index);
    if (c.action == EconomicAction::AcceptRecovery || c.action == EconomicAction::DeliverRecovery)
        return recoveryCommand(c, player, *index);
    if (c.action == EconomicAction::RepairHull || c.action == EconomicAction::UpgradeCargo)
        return service(c, player, *index);
    if (c.action >= EconomicAction::AcceptHaul)
        return haulCommand(c, player);
    if (c.action == EconomicAction::Invest || c.action == EconomicAction::CancelProject ||
        c.action == EconomicAction::CollectDividends)
        return invest(c, player, *index);
    if (size_t(c.good) >= goodCount || c.kg < 1 || c.kg > maxShipPayloadKg)
        return "INVALID CARGO QUANTITY";
    auto i = *index, g = size_t(c.good);
    auto& s = sites_[i];
    if (c.action == EconomicAction::Contract) {
        auto it = std::find_if(contracts_.begin(), contracts_.end(),
                               [&](const auto& x) { return x.id == c.contract && x.site == i; });
        if (it == contracts_.end())
            return "CONTRACT NO LONGER AVAILABLE";
        if (it->phase == ContractPhase::Open) {
            it->phase = ContractPhase::Accepted;
            return "CONTRACT ACCEPTED / ESCROW FUNDED";
        }
        if (it->phase != ContractPhase::Accepted)
            return "CONTRACT CLOSED";
        g = size_t(it->good);
        auto n = std::min({c.kg, player.availableCargo(it->good), it->requested - it->delivered,
                           economicCapacity(network_->sites[i], s) - total(s.stock)});
        if (n <= 0)
            return "REQUIRED CARGO OR WAREHOUSE SPACE UNAVAILABLE";
        const auto before = economicVitals(network_->sites[i], s, tick_);
        auto payment = n * it->unitCents;
        player.cargo[g] -= n;
        s.stock[g] += n;
        player.credits += payment;
        it->escrow -= payment;
        it->delivered += n;
        // Player transfers are accounted separately from industrial conversion.
        s.imported[g] += n;
        externalMoneyDelta_ -= payment;
        if (it->delivered == it->requested)
            it->phase = ContractPhase::Complete;
        Stock delivery{};
        delivery[g] = n;
        recordOutcome(c, i, OutcomeKind::Supply, delivery, payment, before);
        event(network_->sites[i].name + " / PLAYER CONTRACT DELIVERY");
        return "DELIVERED " + std::to_string(n) + " KG / " + std::to_string(payment / 100) + " CR";
    }
    auto q = quote(i, c.good, c.kg);
    if (c.action == EconomicAction::Buy) {
        if (player.massKg() + c.kg > player.capacityKg ||
            player.volumeLitres() + c.kg * goods[g].litresPerKg > player.capacityLitres)
            return "CARGO HOLD FULL / MASS OR VOLUME";
        if (q.available < c.kg)
            return "INSUFFICIENT UNRESERVED STOCK";
        if (q.buyCents > c.limitCents)
            return "QUOTE CHANGED / REVIEW PRICE";
        if (player.credits < q.buyCents)
            return "INSUFFICIENT CREDITS";
        s.stock[g] -= c.kg;
        player.cargo[g] += c.kg;
        player.credits -= q.buyCents;
        s.cash += q.buyCents;
        s.exported[g] += c.kg;
        externalMoneyDelta_ += q.buyCents;
        return "BOUGHT " + std::to_string(c.kg) + " KG " + std::string(goods[g].name);
    }
    if (player.availableCargo(c.good) < c.kg)
        return "INSUFFICIENT UNSEALED SHIP CARGO";
    if (total(s.stock) + c.kg > economicCapacity(network_->sites[i], s))
        return "PORT WAREHOUSE FULL";
    if (q.sellCents < c.limitCents)
        return "QUOTE CHANGED / REVIEW PRICE";
    if (s.cash < q.sellCents)
        return "PORT CANNOT FINANCE ORDER";
    const auto before = economicVitals(network_->sites[i], s, tick_);
    s.stock[g] += c.kg;
    player.cargo[g] -= c.kg;
    s.cash -= q.sellCents;
    player.credits += q.sellCents;
    s.imported[g] += c.kg;
    externalMoneyDelta_ -= q.sellCents;
    Stock delivery{};
    delivery[g] = c.kg;
    recordOutcome(c, i, OutcomeKind::Sale, delivery, q.sellCents, before);
    return "SOLD " + std::to_string(c.kg) + " KG " + std::string(goods[g].name);
}
int64_t Economy::moneyTotal() const {
    int64_t n = treasury_ + carrierCash_;
    for (const auto& s : sites_)
        n += s.cash + s.householdCash + s.dividends + (s.project ? s.project->escrow : 0);
    for (const auto& f : freight_)
        n += f.payment + f.fee;
    for (const auto& c : contracts_)
        n += c.escrow;
    for (const auto& h : hauls_)
        n += h.escrow;
    for (const auto& j : recoveries_)
        n += j.escrow;
    return n;
}
Stock Economy::inventoryTotal() const {
    Stock n{};
    for (const auto& s : sites_) {
        const auto capital = facilityMaterials(s);
        for (size_t g = 0; g < goodCount; g++)
            n[g] += s.stock[g] + capital[g];
    }
    for (const auto& f : freight_)
        if (cargoOnboard(f.phase))
            n[size_t(f.good)] += f.kg;
    return n;
}
Stock Economy::initialTotal() const {
    Stock n{};
    for (const auto& s : network_->sites)
        for (size_t g = 0; g < goodCount; g++)
            n[g] += s.initial[g];
    return n;
}
Stock Economy::externalGoodsDelta() const {
    Stock n{};
    for (const auto& s : sites_)
        for (size_t g = 0; g < goodCount; g++)
            n[g] += s.extracted[g] + s.produced[g] + s.imported[g] - s.consumed[g] - s.exported[g];
    return n;
}
size_t Economy::residentBytes() const {
    return network_->residentBytes() + sites_.capacity() * sizeof(EconomicSiteState) +
           freight_.capacity() * sizeof(EconomicFreight) + contracts_.capacity() * sizeof(EconomicContract) +
           events_.capacity() * sizeof(EconomicEvent) + hauls_.capacity() * sizeof(HaulContract) +
           recoveries_.capacity() * sizeof(RecoveryJob) + outcomes_.capacity() * sizeof(EconomicOutcome);
}
std::string Economy::serialize() const {
    std::ostringstream o;
    o << "INFINITE_ECONOMY 6 " << network_->fingerprint << ' ' << tick_ << ' ' << nextContract_ << ' '
      << treasury_ << ' ' << carrierCash_ << ' ' << externalMoneyDelta_ << ' ' << sites_.size() << '\n';
    for (const auto& s : sites_) {
        o << s.cash << ' ' << s.householdCash << ' ' << s.condition << ' ' << s.health << ' '
          << s.outageUntil;
        for (const auto* a :
             {&s.stock, &s.reserved, &s.extracted, &s.produced, &s.consumed, &s.imported, &s.exported})
            writeStock(o, *a);
        o << ' ' << std::quoted(s.bottleneck) << '\n';
    }
    o << freight_.size() << '\n';
    for (const auto& f : freight_)
        o << f.location << ' ' << f.source << ' ' << f.destination << ' ' << unsigned(f.phase) << ' '
          << unsigned(f.good) << ' ' << f.kg << ' ' << f.startTick << ' ' << f.endTick << ' ' << f.trips
          << ' ' << f.payment << ' ' << f.fee << '\n';
    o << contracts_.size() << '\n';
    for (const auto& c : contracts_)
        o << c.id << ' ' << c.deadline << ' ' << c.site << ' ' << unsigned(c.good) << ' ' << c.requested
          << ' ' << c.delivered << ' ' << c.unitCents << ' ' << c.escrow << ' ' << unsigned(c.phase) << '\n';
    o << events_.size() << '\n';
    for (const auto& e : events_)
        o << e.tick << ' ' << std::quoted(e.text) << '\n';
    for (const auto& s : sites_) {
        for (auto level : s.modules)
            o << level << ' ';
        o << s.dividends << ' ' << s.dividendsPaid << ' ' << s.operatingBaseline << ' ' << bool(s.project);
        if (s.project) {
            o << ' ' << unsigned(s.project->module) << ' ' << s.project->progress << ' ' << s.project->escrow;
            writeStock(o, s.project->delivered);
        }
        o << '\n';
    }
    o << nextHaul_ << ' ' << hauls_.size() << '\n';
    for (const auto& h : hauls_)
        o << h.id << ' ' << h.deadline << ' ' << h.source << ' ' << h.destination << ' ' << unsigned(h.good)
          << ' ' << h.kg << ' ' << h.cost << ' ' << h.fee << ' ' << h.escrow << ' ' << unsigned(h.phase)
          << '\n';
    writeRecovery(o);
    o << "WRECKS "
      << std::count_if(freight_.begin(), freight_.end(), [](const auto& f) { return f.disabledAt >= 0; })
      << '\n';
    o << std::setprecision(17);
    for (size_t i = 0; i < freight_.size(); ++i)
        if (freight_[i].disabledAt >= 0)
            o << i << ' ' << freight_[i].disabledAt << '\n';
    return o.str();
}
bool Economy::restore(std::string_view text) {
    Economy candidate(network_);
    std::istringstream in{std::string(text)};
    std::string header;
    unsigned version;
    uint64_t fingerprint;
    size_t count;
    if (!(in >> header >> version >> fingerprint >> candidate.tick_ >> candidate.nextContract_ >>
          candidate.treasury_ >> candidate.carrierCash_ >> candidate.externalMoneyDelta_ >> count) ||
        header != "INFINITE_ECONOMY" || (version < 1 || version > 6) ||
        fingerprint != network_->fingerprint || count != sites_.size() || candidate.treasury_ < 0 ||
        candidate.carrierCash_ < 0 || candidate.treasury_ > 1000000000000000LL ||
        candidate.carrierCash_ > 1000000000000000LL || candidate.externalMoneyDelta_ < -1000000000000000LL ||
        candidate.externalMoneyDelta_ > 1000000000000000LL || candidate.tick_ > 1000000000000ULL)
        return false;
    for (auto& s : candidate.sites_) {
        if (!(in >> s.cash >> s.householdCash >> s.condition >> s.health >> s.outageUntil) || s.cash < 0 ||
            s.householdCash < 0 || s.cash > 1000000000000000LL || s.householdCash > 1000000000000000LL ||
            s.condition > 1000 || s.health > 1000)
            return false;
        for (auto* a :
             {&s.stock, &s.reserved, &s.extracted, &s.produced, &s.consumed, &s.imported, &s.exported})
            if (!readStock(in, *a))
                return false;
        if (!(in >> std::quoted(s.bottleneck)) || s.bottleneck.size() > 256)
            return false;
        for (size_t g = 0; g < goodCount; g++)
            if (s.reserved[g] > s.stock[g])
                return false;
    }
    if (!(in >> count) || count != freight_.size())
        return false;
    for (auto& f : candidate.freight_) {
        unsigned phase, good;
        if (!(in >> f.location >> f.source >> f.destination >> phase >> good >> f.kg >> f.startTick >>
              f.endTick >> f.trips >> f.payment >> f.fee) ||
            f.location >= sites_.size() || f.source >= sites_.size() || f.destination >= sites_.size() ||
            phase > 4 || good >= goodCount || f.kg < 0 || f.kg > 1000 || f.payment < 0 || f.fee < 0 ||
            f.payment > 1000000000LL || f.fee > 1000000000LL)
            return false;
        f.phase = FreightPhase(phase);
        f.good = Good(good);
    }
    if (!(in >> count) || count > sites_.size() * 16)
        return false;
    candidate.contracts_.clear();
    for (size_t j = 0; j < count; j++) {
        EconomicContract c;
        unsigned good, phase;
        if (!(in >> c.id >> c.deadline >> c.site >> good >> c.requested >> c.delivered >> c.unitCents >>
              c.escrow >> phase) ||
            c.site >= sites_.size() || good >= goodCount || phase > 3 || c.requested < 0 ||
            c.requested > 200 || c.delivered < 0 || c.delivered > c.requested || c.unitCents < 0 ||
            c.unitCents > 100000 || c.escrow < 0)
            return false;
        c.good = Good(good);
        c.phase = ContractPhase(phase);
        if (c.escrow != ((phase == 0 || phase == 1) ? (c.requested - c.delivered) * c.unitCents : 0))
            return false;
        candidate.contracts_.push_back(c);
    }
    if (!(in >> count) || count > 32)
        return false;
    candidate.events_.clear();
    for (size_t j = 0; j < count; j++) {
        EconomicEvent e;
        if (!(in >> e.tick >> std::quoted(e.text)) || e.text.size() > 512)
            return false;
        candidate.events_.push_back(e);
    }
    for (auto& s : candidate.sites_) {
        s.operatingBaseline = s.cash; // v1 migration: no retroactive dividends or refits
        if (version == 1)
            continue;
        for (auto& level : s.modules)
            if (!(in >> level) || level > 3)
                return false;
        unsigned pending;
        if (!(in >> s.dividends >> s.dividendsPaid >> s.operatingBaseline >> pending) || pending > 1 ||
            s.dividends < 0 || s.dividends > 1000000000000000LL || s.dividendsPaid < 0 ||
            s.dividendsPaid > 1000000000000000LL || s.operatingBaseline < 0 ||
            s.operatingBaseline > 1000000000000000LL)
            return false;
        if (pending) {
            FacilityProject p;
            unsigned module;
            if (!(in >> module >> p.progress >> p.escrow) || module >= facilityModuleCount ||
                s.modules[module] >= 3 || !readStock(in, p.delivered))
                return false;
            p.module = FacilityModule(module);
            auto r = facilityRefit(p.module, s.modules[module] + 1);
            if (p.progress >= r.ticks || p.escrow != r.labourCents - r.labourCents * p.progress / r.ticks ||
                (p.progress && p.delivered != r.materials))
                return false;
            for (size_t g = 0; g < goodCount; g++)
                if (p.delivered[g] > r.materials[g])
                    return false;
            s.project = p;
        }
    }
    for (size_t i = 0; i < sites_.size(); i++)
        if (network_->sites[i].orbital &&
            (candidate.sites_[i].modules[1] || candidate.sites_[i].modules[2] ||
             (candidate.sites_[i].project && candidate.sites_[i].project->module != FacilityModule::Storage)))
            return false;
    if (version >= 3) {
        if (!(in >> candidate.nextHaul_ >> count) || !candidate.nextHaul_ ||
            candidate.nextHaul_ > 1000000000000ULL || count > 32)
            return false;
        for (size_t j = 0; j < count; j++) {
            HaulContract h;
            unsigned good, phase;
            if (!(in >> h.id >> h.deadline >> h.source >> h.destination >> good >> h.kg >> h.cost >> h.fee >>
                  h.escrow >> phase) ||
                !h.id || h.id >= candidate.nextHaul_ || h.deadline > 1000000000000ULL ||
                h.source >= sites_.size() || h.destination >= sites_.size() || h.source == h.destination ||
                good >= goodCount || h.kg < 10 || h.kg > maxShipPayloadKg || h.cost < 1 ||
                h.cost > 1000000000 || h.fee < 1 || h.fee > 1000000000 ||
                phase > unsigned(HaulPhase::Expired))
                return false;
            h.good = Good(good);
            h.phase = HaulPhase(phase);
            const auto expected = h.phase == HaulPhase::Open || h.phase == HaulPhase::Reserved ||
                                          h.phase == HaulPhase::InTransit
                                      ? h.cost + h.fee
                                      : 0;
            if (h.escrow != expected || std::any_of(candidate.hauls_.begin(), candidate.hauls_.end(),
                                                    [&](const auto& x) { return x.id == h.id; }))
                return false;
            candidate.hauls_.push_back(h);
        }
        if (std::count_if(candidate.hauls_.begin(), candidate.hauls_.end(), [](const auto& h) {
                return h.phase == HaulPhase::Reserved || h.phase == HaulPhase::InTransit;
            }) > 1)
            return false;
    }
    if (version >= 5 && !candidate.readRecovery(in))
        return false;
    if (version >= 6) {
        std::string tag;
        if (!(in >> tag >> count) || tag != "WRECKS" || count > candidate.freight_.size())
            return false;
        for (size_t j = 0; j < count; ++j) {
            size_t i;
            double time;
            if (!(in >> i >> time) || i >= candidate.freight_.size() || !std::isfinite(time) || time < 0 ||
                time > 1e12 || candidate.freight_[i].disabledAt >= 0 || candidate.freight_[i].payment ||
                candidate.freight_[i].fee ||
                ((candidate.freight_[i].phase == FreightPhase::Loading ||
                  candidate.freight_[i].phase == FreightPhase::Reposition) &&
                 candidate.freight_[i].kg))
                return false;
            candidate.freight_[i].disabledAt = time;
        }
    }
    in >> std::ws;
    if (!in.eof())
        return false;
    // Reservations must be backed by a specific unpaid cargo commitment.
    for (size_t i = 0; i < sites_.size(); i++)
        for (size_t g = 0; g < goodCount; g++) {
            int64_t reserved = 0;
            for (const auto& f : candidate.freight_)
                if ((f.phase == FreightPhase::Reposition || f.phase == FreightPhase::Loading) &&
                    f.source == i && size_t(f.good) == g)
                    reserved += f.kg;
            for (const auto& h : candidate.hauls_)
                if ((h.phase == HaulPhase::Open || h.phase == HaulPhase::Reserved) && h.source == i &&
                    size_t(h.good) == g)
                    reserved += h.kg;
            for (const auto& j : candidate.recoveries_)
                if (j.site == i && j.kind == RecoveryKind::Maintenance &&
                    (j.phase == RecoveryPhase::Accepted || j.phase == RecoveryPhase::Working))
                    reserved += j.delivered[g] -
                                ((Good(g) == Good::Machinery || Good(g) == Good::Filters) ? j.progress : 0);
            if (reserved != candidate.sites_[i].reserved[g])
                return false;
        }
    if (candidate.inventoryTotal() != ([&] {
            auto a = candidate.initialTotal(), d = candidate.externalGoodsDelta();
            for (size_t g = 0; g < goodCount; g++)
                a[g] += d[g];
            return a;
        }()))
        return false;
    if (candidate.moneyTotal() != candidate.endowmentMoney_ + candidate.externalMoneyDelta_)
        return false;
    for (size_t i = 0; i < sites_.size(); i++) {
        if (total(candidate.sites_[i].stock) > economicCapacity(network_->sites[i], candidate.sites_[i]))
            return false;
        for (size_t g = 0; g < goodCount; g++)
            if (candidate.sites_[i].extracted[g] > network_->sites[i].reserves[g])
                return false;
    }
    *this = std::move(candidate);
    return true;
}
std::optional<size_t> EconomySnapshot::siteIndex(const ActivityId& id) const {
    if (network)
        for (size_t i = 0; i < network->sites.size(); i++)
            if (network->sites[i].id == id)
                return i;
    return {};
}
EconomyQuote EconomySnapshot::quote(size_t i, Good g, int64_t kg) const {
    return quoted(network->sites.at(i), sites.at(i), g, kg);
}
std::optional<ActivityId> EconomySnapshot::contractDestination() const {
    if (network)
        for (const auto& j : recoveries)
            if ((j.phase == RecoveryPhase::Accepted || j.phase == RecoveryPhase::Working) &&
                j.site < network->sites.size())
                return network->sites[j.site].id;
    if (network)
        for (const auto& c : contracts)
            if (c.phase == ContractPhase::Accepted && c.site < network->sites.size())
                return network->sites[c.site].id;
    return {};
}
MarketView economicMarketView(const EconomySnapshot* s, const std::optional<ActivityId>& port,
                              size_t selected, int64_t quantity, bool connected, MarketPage page,
                              const MarketContext& context) {
    MarketView v;
    v.page = page;
    v.open = true;
    v.selected = std::min(selected, goodCount - 1);
    v.quantity = quantity;
    v.connected = connected;
    if (!s) {
        v.message = "PREPARING LOCAL ECONOMY / BACKGROUND WORKER";
        return v;
    }
    v.credits = s->player.credits;
    v.cargoKg = s->player.massKg();
    v.cargoLitres = s->player.volumeLitres();
    v.capacityKg = s->player.capacityKg;
    v.capacityLitres = s->player.capacityLitres;
    v.tick = s->tick;
    v.message = s->message;
    if (s->player.haul)
        v.activeJob = std::string(s->player.haul->onboard ? "SEALED " : "PICKUP ") +
                      std::to_string(s->player.haul->kg) + "KG / F4 HAUL";
    if (page == MarketPage::Outcomes) {
        v.ready = true;
        v.connected = false;
        economicOutcomeView(v, *s, selected);
        return v;
    }
    auto i = port ? s->siteIndex(*port) : std::nullopt;
    if (!i) {
        v.message = "SELECT A HARBOUR OR SURFACE PORT / K OR SHIFT-K";
        return v;
    }
    v.ready = true;
    v.site = s->network->sites[*i].name;
    v.role = s->network->sites[*i].role;
    v.bottleneck = s->sites[*i].bottleneck;
    if (s->catchingUp) {
        v.connected = false;
        v.message = "ECONOMY CATCHING UP / TRADING PAUSED";
    }
    if (page == MarketPage::Weapons) {
        v.selected = std::min<size_t>(selected, maxWeaponLevel - 1);
        auto money = [](int64_t cents) {
            std::ostringstream o;
            o << std::fixed << std::setprecision(2) << cents / 100.;
            return o.str();
        };
        for (unsigned level = 1; level <= maxWeaponLevel; ++level) {
            const auto& spec = weaponSpec(level);
            auto q = economicWeaponQuote(*s, *i, level);
            v.detail.push_back(std::string(spec.name) + (s->player.weaponLevel == level ? " / FITTED" : ""));
            v.detail.push_back("DAMAGE " + std::to_string(int(spec.damage)) + " / RANGE " +
                               std::to_string(int(spec.rangeMetres / 1000)) + " KM / " +
                               std::to_string(int(1 / spec.interval * 10) / 10.) + " SHOTS/S");
            v.detail.push_back("FIT / UPGRADE " + money(q.totalCents()) + " CR / PORT PARTS AND CREW");
            std::string parts = "PARTS: ";
            for (size_t g = 0; g < goodCount; ++g)
                if (q.materials[g])
                    parts +=
                        std::string(goodInfo(Good(g)).name) + " " + std::to_string(q.materials[g]) + "KG ";
            v.detail.push_back(parts);
            v.detail.push_back(q.status);
        }
        const auto sale = economicWeaponQuote(*s, *i, s->player.weaponLevel, true);
        v.summary = s->player.weaponLevel ? "FITTED " + std::string(weaponSpec(s->player.weaponLevel).name) +
                                                " / BUYBACK " + money(sale.sellCents) + " CR"
                                          : "WEAPON MOUNT EMPTY / SELECT A LASER TO BUY";
        v.bottleneck =
            "ENTER FITS SELECTED GRADE / BACKSPACE SELLS CURRENT WEAPON / PHYSICAL TRADE LINK REQUIRED";
        return v;
    }
    if (page == MarketPage::Services) {
        v.selected = std::min<size_t>(selected, 1);
        const double hull = std::isfinite(context.hull) ? std::clamp(context.hull, 0., 100.) : 100.;
        const auto points = unsigned(std::ceil(std::clamp(100. - hull, 0., 10.)));
        auto credits = [](int64_t cents) {
            std::ostringstream out;
            out << std::fixed << std::setprecision(2) << cents / 100.;
            return out.str();
        };
        for (auto service : {ShipService::Repair, ShipService::CargoHold}) {
            const bool repair = service == ShipService::Repair;
            const auto level = std::min(maxCargoLevel, s->player.cargoLevel + 1);
            auto q = economicServiceQuote(*s, *i, service, repair ? points : level);
            v.detail.push_back(repair ? "HULL REPAIR / +" + std::to_string(points) + " POINTS / HULL " +
                                            std::to_string(int(hull)) + "%"
                                      : "CARGO REFIT / LEVEL " + std::to_string(s->player.cargoLevel) +
                                            "/3 > " + std::to_string(level) + "/3");
            v.detail.push_back(repair ? "REPAIR UP TO 10 POINTS PER ORDER / REPEAT FOR MORE DAMAGE"
                                      : std::to_string(holdCapacityKg(level)) + " KG / " +
                                            std::to_string(holdCapacityLitres(level)) +
                                            " L / INTERNAL BAY RACKS");
            v.detail.push_back("TOTAL " + credits(q.totalCents()) + " CR / PARTS " + credits(q.partsCents) +
                               " / CREW " + credits(q.labourCents));
            std::string parts = "PORT PARTS: ";
            for (size_t g = 0; g < goodCount; g++)
                if (q.materials[g])
                    parts +=
                        std::string(goodInfo(Good(g)).name) + " " + std::to_string(q.materials[g]) + "KG ";
            v.detail.push_back(std::move(parts));
            v.detail.push_back(q.status);
        }
        v.summary = "HOLD " + std::to_string(v.cargoKg) + "/" + std::to_string(v.capacityKg) + " KG / " +
                    std::to_string(v.cargoLitres) + "/" + std::to_string(v.capacityLitres) + " L";
        v.bottleneck = "PORT SUPPLIES UNRESERVED PARTS / CREW PAID / REPAIRS PERSIST THROUGH RESTART";
        return v;
    }
    if (page == MarketPage::Jobs) {
        v.selected = s->jobs.empty() ? 0 : std::min(selected, s->jobs.size() - 1);
        v.summary = std::string(s->player.carrierRank()) + " / DELIVERED " +
                    std::to_string(s->player.deliveries) + " / LATE " +
                    std::to_string(s->player.lateDeliveries) + " / WITHDRAWN " +
                    std::to_string(s->player.returnedHauls) + " / EARNED " +
                    std::to_string(s->player.freightEarnings / 100) + " CR";
        for (const auto& job : s->jobs) {
            v.detail.push_back(job.title);
            v.detail.push_back(job.route);
        }
        if (!s->jobs.empty()) {
            const auto& job = s->jobs[v.selected];
            const auto plan = economicPlanJob(*s, job, context);
            auto duration = [](double seconds) {
                if (!std::isfinite(seconds) || seconds > 99 * 86400.)
                    return std::string(">99D");
                const auto n = int64_t(std::ceil(std::max(0., seconds)));
                if (n >= 86400)
                    return std::to_string(n / 86400) + "D " + std::to_string(n % 86400 / 3600) + "H";
                if (n >= 3600)
                    return std::to_string(n / 3600) + "H " + std::to_string(n % 3600 / 60) + "M";
                return std::to_string(n / 60) + "M " + std::to_string(n % 60) + "S";
            };
            int64_t litres = job.kg * goodInfo(job.good).litresPerKg;
            if (job.recovery) {
                litres = 0;
                for (size_t g = 0; g < goodCount; ++g)
                    litres += job.required[g] * goodInfo(Good(g)).litresPerKg;
            }
            v.plan = "DIRECT " + flightRange(plan.pickupMetres + plan.deliveryMetres) + " / LOAD " +
                     std::to_string(job.kg) + " KG / " + std::to_string(litres) + " L / " +
                     (plan.fits ? "HOLD FITS" : "NEEDS MORE HOLD SPACE / SERVICES");
            v.planTiming = plan.procurement ? "SOURCE GOODS FIRST / TRAVEL ESTIMATE AWAITS PROCUREMENT"
                           : !plan.timed
                               ? "SELECTED ROUTE ONLY / SHIP POSITION UNAVAILABLE"
                               : "EST N MANUAL " + duration(plan.seconds) + " / DEADLINE " +
                                     duration(std::max(0., (double(job.deadline) - double(s->tick)) *
                                                               economyTickSeconds)) +
                                     " / MARGIN " + (plan.slackSeconds < 0 ? "-" : "+") +
                                     duration(std::abs(plan.slackSeconds));
        }
        v.bottleneck = s->player.haul && s->player.haul->system != s->network->system
                           ? "ACTIVE HAUL IN ANOTHER SYSTEM / F4 RETURNS TO ITS SYSTEM"
                           : "MANUAL N CRUISE / J FINAL APPROACH / FLIGHT TIME / DETOURS AND WEATHER EXTRA";
        if (s->jobs.empty())
            v.message = "NO FUNDED JOBS YET / CHECK AGAIN AS LOCAL SHORTAGES DEVELOP";
        return v;
    }
    if (page == MarketPage::Routes) {
        v.selected = s->routes.empty() ? 0 : std::min(selected, s->routes.size() - 1);
        v.summary = "LIVE QUOTES / COST AND GROSS MARGIN / PRICES MAY CHANGE IN FLIGHT";
        v.bottleneck = "BOUNDED LOCAL SEARCH / EMPTY HOLD SPACE / CURRENT WALLET AND BUYER FUNDS";
        for (const auto& r : s->routes) {
            v.detail.push_back(std::to_string(r.kg) + " KG " + std::string(goodInfo(r.good).name) +
                               " / COST " + std::to_string(r.costCents / 100) + " CR / MARGIN +" +
                               std::to_string((r.revenueCents - r.costCents) / 100) + " CR");
            v.detail.push_back(s->network->sites[r.source].name + " > " +
                               s->network->sites[r.destination].name + " / " + flightRange(r.distanceMetres));
        }
        if (v.detail.empty())
            v.message = "NO AFFORDABLE PROFITABLE ROUTES / FREE HOLD SPACE OR BUILD CAPITAL";
        return v;
    }
    if (page == MarketPage::Industry) {
        const auto& state = s->sites[*i];
        const auto& site = s->network->sites[*i];
        v.selected = std::min(selected, facilityModuleCount - 1);
        v.summary = "DIVIDENDS " + std::to_string(state.dividends / 100) + " CR / PAID " +
                    std::to_string(state.dividendsPaid / 100) + " CR / STORAGE " +
                    std::to_string(economicCapacity(site, state)) + " KG";
        for (size_t m = 0; m < facilityModuleCount; m++) {
            auto r = facilityRefit(FacilityModule(m), std::min(3u, state.modules[m] + 1));
            const bool active = state.project && size_t(state.project->module) == m;
            v.detail.push_back(std::string(r.name) + " / LEVEL " + std::to_string(state.modules[m]) + "/3" +
                               (active ? " / ACTIVE PROJECT" : ""));
            v.detail.push_back(std::string(r.effect) + (site.orbital && m ? " / SURFACE ONLY" : ""));
            v.detail.push_back("LABOUR " + std::to_string(r.labourCents / 100) + " CR / BUILD " +
                               std::to_string(r.ticks * 5) + " ECON MIN / 2% CASH-SURPLUS SHARE PER LEVEL");
            std::string needed = "PARTS: ", missing = "STILL TO SOURCE: ";
            for (size_t g = 0; g < goodCount; g++) {
                const auto left = r.materials[g] - (active ? state.project->delivered[g] : 0);
                if (r.materials[g])
                    needed += std::string(goodInfo(Good(g)).name) + " " + std::to_string(left) + "KG ";
                if (left > s->player.availableCargo(Good(g)))
                    missing += std::string(goodInfo(Good(g)).name) + " " +
                               std::to_string(left - s->player.availableCargo(Good(g))) + "KG ";
            }
            v.detail.push_back(state.modules[m] == 3 ? "MAXIMUM REFIT LEVEL" : needed);
            v.detail.push_back(state.modules[m] == 3 ? "EXISTING FACILITY FULLY REFITTED" : missing);
        }
        if (state.project) {
            const auto& p = *state.project;
            auto r = facilityRefit(p.module, state.modules[size_t(p.module)] + 1);
            v.bottleneck =
                std::string(r.name) + " / " + std::to_string(p.progress) + "/" + std::to_string(r.ticks) +
                " TICKS / " +
                (p.delivered != r.materials    ? "AWAITING PARTS"
                 : state.outageUntil > s->tick ? "PAUSED / PLANT OUTAGE"
                                               : "CONSTRUCTION / REQUIRES HEALTHY CREW AND UTILITIES");
        }
        return v;
    }
    for (size_t g = 0; g < goodCount; g++) {
        auto q = s->quote(*i, Good(g), quantity);
        std::ostringstream line;
        line << std::left << std::setw(20) << goods[g].name << std::right << std::setw(8) << q.available
             << std::setw(8) << s->player.availableCargo(Good(g)) << std::setw(11) << q.buyCents / 100.
             << std::setw(11) << q.sellCents / 100.;
        v.rows[g] = line.str();
    }
    for (const auto& c : s->contracts)
        if (c.site == *i && (c.phase == ContractPhase::Accepted || c.phase == ContractPhase::Open)) {
            v.contract = std::string(c.phase == ContractPhase::Accepted ? "DELIVER " : "CONTRACT ") +
                         std::to_string(c.requested - c.delivered) + " KG " +
                         std::string(goods[size_t(c.good)].name) + " / " + std::to_string(c.unitCents / 100) +
                         " CR/KG / " +
                         std::to_string(c.deadline > s->tick ? ((c.deadline - s->tick) * 5 + 59) / 60 : 0) +
                         " MIN / F3 " + (c.phase == ContractPhase::Accepted ? "DELIVER" : "ACCEPT");
            break;
        }
    return v;
}
} // namespace infinite
