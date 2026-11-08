#include "infinity/Cockpit.hpp"
#include "infinity/CockpitContext.hpp"
#include "infinity/Economy.hpp"
#include "infinity/ExpeditionPlanner.hpp"
#include "infinity/OrbitalDebris.hpp"
#include "infinity/SurfaceRecon.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>

namespace infinity {
namespace {
const glm::vec3 ink{.018, .035, .052}, muted{.43, .60, .66}, ice{.49, .85, .93}, mint{.44, .93, .74},
    amber{1, .65, .28}, pearl{.84, .92, .91}, trafficInk{.90, .61, .76};
const glm::vec3 danger{1,.30,.22};
void triangle(Mesh& m, glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, glm::vec3 color, float alpha = 1) {
    const auto first = uint32_t(m.vertices.size());
    for (auto p : {a, b, c})
        m.vertices.push_back({float(p.x), float(p.y), float(p.z), rgba(color, alpha)});
    m.indices.insert(m.indices.end(), {first, first + 1, first + 2});
}
void rect(Mesh& m, double x, double y, double w, double h, glm::vec3 c, float alpha = 1) {
    triangle(m, {x, y, 0}, {x + w, y, 0}, {x, y + h, 0}, c, alpha);
    triangle(m, {x + w, y, 0}, {x + w, y + h, 0}, {x, y + h, 0}, c, alpha);
}
void line(Mesh& m, double x, double y, double a, double b, glm::vec3 c) {
    addLine(m, {x, y, 0}, {a, b, 0}, c);
}
// Small native-resolution instrument type. No font library or atlas allocation.
const std::map<char, std::string_view> glyphs{
    {'A', "01110100011000111111100011000110001"}, {'B', "11110100011000111110100011000111110"},
    {'C', "01111100001000010000100001000001111"}, {'D', "11110100011000110001100011000111110"},
    {'E', "11111100001000011110100001000011111"}, {'F', "11111100001000011110100001000010000"},
    {'G', "01111100001000010111100011000101111"}, {'H', "10001100011000111111100011000110001"},
    {'I', "11111001000010000100001000010011111"}, {'J', "00111000100001000010000101001001100"},
    {'K', "10001100101010011000101001001010001"}, {'L', "10000100001000010000100001000011111"},
    {'M', "10001110111010110101100011000110001"}, {'N', "10001110011010110011100011000110001"},
    {'O', "01110100011000110001100011000101110"}, {'P', "11110100011000111110100001000010000"},
    {'Q', "01110100011000110001101011001001101"}, {'R', "11110100011000111110101001001010001"},
    {'S', "01111100001000001110000010000111110"}, {'T', "11111001000010000100001000010000100"},
    {'U', "10001100011000110001100011000101110"}, {'V', "10001100011000110001100010101000100"},
    {'W', "10001100011000110101101011101110001"}, {'X', "10001100010101000100010101000110001"},
    {'Y', "10001100010101000100001000010000100"}, {'Z', "11111000010001000100010001000011111"},
    {'0', "01110100011001110101110011000101110"}, {'1', "00100011000010000100001000010001110"},
    {'2', "01110100010000100010001000100011111"}, {'3', "11110000010000101110000010000111110"},
    {'4', "00010001100101010010111110001000010"}, {'5', "11111100001000011110000010000111110"},
    {'6', "01110100001000011110100011000101110"}, {'7', "11111000010001000100010000100001000"},
    {'8', "01110100011000101110100011000101110"}, {'9', "01110100011000101111000010000101110"},
    {'/', "00001000010001000100010001000010000"}, {'-', "00000000000000011111000000000000000"},
    {'+', "00000001000010011111001000010000000"}, {',', "00000000000000000000001100010001000"},
    {'.', "00000000000000000000000000011000110"}, {':', "00000011000110000000011000110000000"},
    {'%', "11001110100001000100010001001100011"}, {'m', "00000000001101010101101011010110101"},
    {'k', "10000100001001010100110001010010010"}, {'n', "00000000001111010001100011000110001"},
    {'p', "00000000001111010001111101000010000"}, {'u', "00000000001000110001100011001101101"},
    {'s', "00000000000111110000011100000111110"}, {'e', "00000000000111010001111111000001111"}};
void text(CockpitOverlay& h, std::string_view s, double x, double y, double size, glm::vec3 color,
          size_t maxChars = 100, bool literal = false) {
    auto& m=h.lettering;
    const auto firstVertex=m.vertices.size(),firstIndex=m.indices.size();
    const double firstX=x;
    const double pixel = size / 7.;
    size_t count = 0;
    for (char c : s) {
        if (count++ >= maxChars)
            break;
        if (!literal && c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');
        if (auto it = glyphs.find(c); it != glyphs.end())
            for (int row = 0; row < 7; row++)
                for (int col = 0; col < 5;) {
                    if (it->second[size_t(row * 5 + col)] != '1') {
                        ++col;
                        continue;
                    }
                    const int start = col++;
                    while (col < 5 && it->second[size_t(row * 5 + col)] == '1')
                        ++col;
                    rect(m, x + start * pixel, y + row * pixel, (col - start) * pixel, pixel, color);
                }
        x += pixel * 6;
    }
    if(m.vertices.size()!=firstVertex){
        const auto length=std::min(maxChars,s.size());
        h.textRuns.push_back({{firstX,y,std::max(0.,length*pixel*6-pixel),size},firstVertex,
            m.vertices.size()-firstVertex,firstIndex,m.indices.size()-firstIndex,std::string(s.substr(0,length))});
    }
}
std::string elide(std::string_view value, double width, double size) {
    const size_t count = size_t(std::max(0., std::floor(width / (size * 6 / 7))));
    if (value.size() <= count)
        return std::string(value);
    return count > 3 ? std::string(value.substr(0, count - 3)) + "..." : std::string{};
}
void fitted(CockpitOverlay& h, std::string_view s, double x, double y, double width, double size, glm::vec3 color,
            bool literal = false) {
    text(h, elide(s, width, size), x, y, size, color, 200, literal);
}
void occlude(CockpitOverlay& h,CockpitRect panel) {
    std::erase_if(h.contactHits,[&](const auto& hit){return hit.bounds.overlaps(panel);});
    // Panels and text are submitted in separate batches. Remove covered
    // underlay runs before adding modal content, including whole words at edges.
    auto& mesh=h.lettering;size_t vertices=0,indices=0,runs=0;
    for(auto run:h.textRuns){
        if(run.bounds.overlaps(panel))continue;
        for(size_t i=0;i<run.vertexCount;++i)mesh.vertices[vertices+i]=mesh.vertices[run.firstVertex+i];
        for(size_t i=0;i<run.indexCount;++i)mesh.indices[indices+i]=uint32_t(mesh.indices[run.firstIndex+i]-run.firstVertex+vertices);
        run.firstVertex=vertices;run.firstIndex=indices;vertices+=run.vertexCount;indices+=run.indexCount;
        h.textRuns[runs++]=std::move(run);
    }
    mesh.vertices.resize(vertices);mesh.indices.resize(indices);h.textRuns.resize(runs);
    auto& strokes=h.strokes;vertices=indices=0;
    for(size_t i=0;i+1<strokes.indices.size();i+=2){
        const auto a=strokes.vertices[strokes.indices[i]],b=strokes.vertices[strokes.indices[i+1]];
        const CockpitRect bounds{std::min(a.x,b.x),std::min(a.y,b.y),std::max(.01,double(std::abs(a.x-b.x))),std::max(.01,double(std::abs(a.y-b.y)))};
        if(bounds.overlaps(panel))continue;
        strokes.vertices[vertices]=a;strokes.vertices[vertices+1]=b;
        strokes.indices[indices++]=uint32_t(vertices++);strokes.indices[indices++]=uint32_t(vertices++);
    }
    strokes.vertices.resize(vertices);strokes.indices.resize(indices);
}
std::vector<std::string> wrapped(std::string_view message,double width,double size,size_t maximum=3) {
    std::vector<std::string> lines;
    const auto columns=size_t(std::max(1.,std::floor(width/(size*6/7))));
    while(!message.empty()&&lines.size()<maximum){
        while(!message.empty()&&message.front()==' ')message.remove_prefix(1);
        if(message.empty())break;
        if(lines.size()+1==maximum){lines.push_back(elide(message,width,size));break;}
        auto end=std::min(columns,message.size());
        if(end<message.size())if(const auto space=message.substr(0,end).find_last_of(' ');space!=std::string_view::npos&&space>columns/3)end=space;
        lines.emplace_back(message.substr(0,end));message.remove_prefix(end);
    }
    return lines;
}
void card(CockpitOverlay&,CockpitRect,glm::vec3);
void card(CockpitOverlay& h, CockpitRect r, glm::vec3 tint) {
    h.hitRegions.push_back(r);
    rect(h.panels, r.x, r.y, r.w, r.h, ink, .94f);
    rect(h.panels, r.x, r.y, 2, r.h, tint * .55f);
    line(h.strokes, r.x + 2, r.y, r.x + r.w, r.y, tint * .38f);
    line(h.strokes, r.x + 2, r.y + r.h, r.x + r.w, r.y + r.h, tint * .15f);
}
void diamond(Mesh& m, glm::dvec2 p, double radius, glm::vec3 color) {
    for (int i = 0; i < 4; ++i) {
        const double a = i * 1.570796326795, b = (i + 1) * 1.570796326795;
        line(m, p.x + std::cos(a) * radius, p.y + std::sin(a) * radius, p.x + std::cos(b) * radius,
             p.y + std::sin(b) * radius, color);
    }
}
void bar(Mesh& m, double x, double y, double w, double value, glm::vec3 color) {
    rect(m, x, y, w, 3, muted * .22f);
    rect(m, x, y, w * std::clamp(value, 0., 1.), 3, color);
}
} // namespace
std::string flightQuantity(double v, bool speed) {
    char b[64];
    const double a = std::abs(v);
    double divisor = 1;
    const char* suffix = "m";
    if (a >= 9.4607304725808e15) {
        divisor = 9.4607304725808e15;
        suffix = "LY";
    } else if (a >= 1.495978707e10) {
        divisor = 1.495978707e11;
        suffix = "AU";
    } else if (a >= 1000) {
        divisor = 1000;
        suffix = "km";
    } else if (a > 0 && a < 1e-9) {
        divisor = 1e-12;
        suffix = "pm";
    } else if (a > 0 && a < 1e-6) {
        divisor = 1e-9;
        suffix = "nm";
    } else if (a > 0 && a < .001) {
        divisor = 1e-6;
        suffix = "um";
    } else if (a > 0 && a < 1) {
        divisor = .001;
        suffix = "mm";
    }
    const double n = v / divisor;
    if (std::abs(n) > 99999)
        std::snprintf(b, sizeof(b), "%.2e %s%s", n, suffix, speed ? "/s" : "");
    else
        std::snprintf(b, sizeof(b), std::abs(n) < 100 ? "%.1f %s%s" : "%.0f %s%s", n, suffix,
                      speed ? "/s" : "");
    return b;
}
bool CockpitRect::contains(glm::dvec2 p) const {
    return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
}
bool CockpitRect::overlaps(const CockpitRect& r) const {
    return x < r.x + r.w && x + w > r.x && y < r.y + r.h && y + h > r.y;
}
std::vector<std::pair<size_t,size_t>> cockpitTextConflicts(const CockpitOverlay& h) {
    std::vector<std::pair<size_t,size_t>> result;
    for(size_t i=0;i<h.textRuns.size();++i)
        for(size_t j=0;j<i;++j)
            if(h.textRuns[i].bounds.overlaps(h.textRuns[j].bounds))result.emplace_back(j,i);
    return result;
}
void maskCockpitMarkers(Mesh& markers, const CockpitViewport& viewport, const CockpitOverlay& h) {
    size_t vertices = 0, indices = 0;
    for (size_t i = 0; i + 1 < markers.indices.size(); i += 2) {
        const auto a = markers.vertices[markers.indices[i]], b = markers.vertices[markers.indices[i + 1]];
        const auto from = viewport.toCanvas({a.x, a.y}), to = viewport.toCanvas({b.x, b.y});
        const CockpitRect bounds{std::min(from.x, to.x) - 2, std::min(from.y, to.y) - 2,
                                 std::abs(from.x - to.x) + 4, std::abs(from.y - to.y) + 4};
        if (std::any_of(h.hitRegions.begin(), h.hitRegions.end(), [&](auto r) { return bounds.overlaps(r); }) ||
            std::any_of(h.worldLabels.begin(), h.worldLabels.end(), [&](auto r) { return bounds.overlaps(r); }) ||
            std::any_of(h.textRuns.begin(), h.textRuns.end(), [&](const auto& r) { return bounds.overlaps(r.bounds); }))
            continue;
        markers.vertices[vertices] = a;
        markers.vertices[vertices + 1] = b;
        markers.indices[indices++] = uint32_t(vertices++);
        markers.indices[indices++] = uint32_t(vertices++);
    }
    markers.vertices.resize(vertices);
    markers.indices.resize(indices);
}
CockpitViewport::CockpitViewport(int w, int h, CockpitRect safe) {
    w = std::max(1, w);
    h = std::max(1, h);
    const double halfW =
        std::min(w * .5 - std::max(0., safe.x), std::min(double(w), safe.x + safe.w) - w * .5);
    const double halfH =
        std::min(h * .5 - std::max(0., safe.y), std::min(double(h), safe.y + safe.h) - h * .5);
    width = std::clamp(int(std::floor(halfW * 2)), 1, w);
    height = std::clamp(int(std::floor(halfH * 2)), 1, h);
    if ((w - width) % 2 && width > 1)
        --width;
    // The 900-unit design needs at least a square canvas. Fit uniformly in
    // tall windows rather than creating negative instrument/label widths.
    height = std::min(height, width);
    if ((h - height) % 2 && height > 1)
        --height;
    x = (w - width) / 2;
    y = (h - height) / 2;
    projectionScale = double(h) / height;
}
glm::dvec2 CockpitViewport::toCanvas(glm::dvec2 p) const {
    return (p - glm::dvec2(x, y)) * (900. / height);
}
glm::dvec2 CockpitViewport::toPixels(glm::dvec2 p) const {
    return p * (height / 900.) + glm::dvec2(x, y);
}
CockpitLayout::CockpitLayout(double aspect)
    : width(900 * aspect), traffic{24, 176, 274, 263}, celestial{width - 310, 176, 286, 331},
      life{width - 310, 550, 286, 48}, navigation{24, 758, std::clamp((width - 48) * .31, 240., 430.), 124},
      flight{}, scope{}, systems{width - 24 - std::clamp((width - 48) * .22, 185., 290.), 758,
                                 std::clamp((width - 48) * .22, 185., 290.), 124},
      windscreen{traffic.x + traffic.w + 18, 166, celestial.x - traffic.x - traffic.w - 36, 472} {
    const double mid = systems.x - navigation.x - navigation.w - 36;
    flight = {navigation.x + navigation.w + 12, 758, mid * .48, 124};
    scope = {flight.x + flight.w + 12, 758, mid * .52, 124};
}
CrewPanelLayout::CrewPanelLayout(double aspect) {
    const double width = 900 * aspect, pw = std::min(960., width - 48), map = pw * .35;
    panel = {(width - pw) * .5, 168, pw, 578};
    for (size_t i = 0; i < crewRoomCount; ++i) {
        auto p = crewRoomPosition(CrewStation(i));
        if (i >= 5)
            p.x = i == 5 ? -2 : 2;
        rooms[i] = {panel.x + map * .5 + p.x * map * .105 - map * .2, panel.y + 154 + p.y * 23, map * .4, 56};
    }
    for (size_t i = 0; i < maxShipCrew; ++i)
        roster[i] = {panel.x + map + 12, panel.y + 66 + i * 31, pw - map - 28, 29};
}
CrewFlightLayout::CrewFlightLayout(double aspect) {
    const double width = 900 * aspect;
    panel = {24, 176, std::clamp(width * .5 - 64, 160., 316.), 570};
    automatic = {panel.x + panel.w - 72, panel.y + 8, 60, 18};
    weapon = {std::max(width * .5 - 158, panel.x + panel.w + 12), 665, 316, 80};
    const double noticeWidth = std::clamp(width - 656, 24., 650.);
    notice = {(width - noticeWidth) * .5, 126, noticeWidth, 29};
    const auto col = (panel.w - 30) * .5;
    for (size_t i = 0; i < maxShipCrew; ++i)
        roster[i] = {panel.x + 10, panel.y + 68 + i * 27, panel.w - 20, 25};
    for (size_t i = 0; i < crewRoomCount; ++i)
        rooms[i] = {panel.x + 10 + (i % 2) * (col + 10), panel.y + 321 + (i / 2) * 22, col, 20};
    for (size_t i = 0; i < actions.size(); ++i)
        actions[i] = {panel.x + 10 + (i % 2) * (col + 10), panel.y + 416 + (i / 2) * 26, col, 24};
    const auto gunWidth = (panel.w - 40) / maxWeaponMounts;
    for (unsigned i = 0; i < maxWeaponMounts; ++i)
        guns[i] = {panel.x + 10 + i * (gunWidth + 10), panel.y + 286, gunWidth, 18};
}
CockpitRect weaponStationCard(double aspect, bool flightConsole) {
    const auto weapon =
        flightConsole ? CrewFlightLayout(aspect).weapon : CockpitRect{450 * aspect - 158, 665, 316, 80};
    return {weapon.x, 603, 316, 50};
}
PlannerLayout::PlannerLayout(double aspect,bool expanded,const ExpeditionPlan* view) {
    const double w=900*aspect,pw=std::min(1040.,w-48);
    panel={(w-pw)*.5,112,pw,expanded?634.:576.};
    const double lw=(pw-60)*.43;
    left={panel.x+18,180,lw,438};right={left.x+lw+24,180,pw-60-lw,438};
    for(unsigned i=0;i<offers.size();++i)offers[i]={left.x,181+i*25.,left.w,23};
    for(unsigned i=0;i<minus.size();++i){minus[i]={right.x+right.w-58,182+i*28.,24,23};plus[i]={right.x+right.w-24,182+i*28.,24,23};}
    section={left.x,expanded?332.:267.,left.w,expanded?174.:239.};composition={left.x,512,left.w,7};
    route={panel.x+18,574,pw-36,54};
    const double slot=(right.w-16)/3;
    for(unsigned i=0;i<bays.size();++i)bays[i]={right.x+(i==0?1:i==1?0:2)*(slot+8),389,slot,79};
    for(unsigned i=0;i<steps.size();++i)steps[i]={route.x+i*route.w/10,route.y,route.w/10,route.h};
    const double bw=(pw-64)/3;
    for(unsigned i=0;i<actions.size();++i)actions[i]={panel.x+18+(i%3)*(bw+14),638+(i/3)*40.,bw,28};
    details={panel.x+pw-130,154,112,22};close={panel.x+pw-106,expanded?710.:670.,88,16};
    if(!expanded){
        const double compactWidth=std::min(660.,w-64);
        panel={(w-compactWidth)*.5,250,compactWidth,318};
        left={panel.x+24,panel.y+48,panel.w-48,148};right=left;
        section=composition={0,0,0,0};bays.fill({0,0,0,0});minus.fill({0,0,0,0});plus.fill({0,0,0,0});
        offers.fill({0,0,0,0});offers[0]={left.x,panel.y+45,left.w,47};
        const auto summary=view?summarizePlan(*view):PlannerSummary{};
        const auto lines=view?std::max(size_t(1),wrapped(summary.instruction,left.w,10,3).size()):size_t(3);
        body={left.x,panel.y+105,left.w,double(lines)*13};
        double cursor=body.y+body.h+14;
        debrief={left.x,cursor,left.w,0};
        if(view&&view->debrief){debrief.h=view->accounting.recorded?58:24;cursor+=debrief.h+8;}
        status={left.x,cursor,left.w,9};
        if(view&&(!summary.context.empty()||(view->visual.shipMetres<.012&&view->stage!=PlanStage::Loading)))cursor+=20;
        receipt={left.x,cursor,left.w,0};
        if(view&&(view->busy||(!view->message.empty()&&!(view->debrief&&view->message.starts_with("CAVERN ASSAY COMPLETE"))))){const auto rows=wrapped(view->busy?"AWAITING SAVED RECEIPT":view->message,left.w,8.5,2).size();
            receipt.h=rows*12;cursor+=receipt.h+14;}
        route={left.x,cursor,left.w,4};cursor+=22;
        for(unsigned i=0;i<steps.size();++i)steps[i]={route.x+i*route.w/10,route.y,route.w/10-4,route.h};
        actions.fill({0,0,0,0});actions[0]={left.x,cursor,left.w-126,34};
        details={panel.x+panel.w-138,cursor,114,34};cursor+=50;
        footerY=cursor;close={panel.x+panel.w-108,footerY-3,84,16};panel.h=footerY-panel.y+22;
    }
}
namespace {
std::optional<size_t> contactRow(glm::dvec2 p, CockpitRect r, size_t count, size_t capacity) {
    if (!r.contains(p) || p.y < 205 || p.y >= 205 + 34 * std::min(count, capacity))
        return {};
    return size_t((p.y - 205) / 34);
}
} // namespace
std::optional<size_t> CockpitLayout::trafficRow(glm::dvec2 p, size_t count) const {
    return contactRow(p, traffic, count, 6);
}
std::optional<size_t> CockpitLayout::celestialRow(glm::dvec2 p, size_t count) const {
    return contactRow(p, celestial, count, 8);
}
std::vector<size_t> cockpitActivityRows(std::span<const ActivityContact> contacts) {
    std::vector<size_t> rows;
    for (size_t i = 0; i < contacts.size() && rows.size() < 6; ++i)
        if (!isOrbitalDebris(contacts[i].body.id) || contacts[i].selected)
            rows.push_back(i);
    for (size_t i = 0; i < contacts.size(); ++i)
        if (contacts[i].selected && std::find(rows.begin(), rows.end(), i) == rows.end()) {
            rows.back() = i;
            break;
        }
    // Keep a known port readable among nearby freighters, while K cycling and
    // the contact vector retain their original order and identities.
    std::optional<size_t> nearest;
    for (size_t i = 0; i < contacts.size(); ++i)
        if ((contacts[i].body.kind == ActivityKind::Harbour ||
             contacts[i].body.kind == ActivityKind::SurfacePort) &&
            !contacts[i].body.economicBrief.empty() &&
            (!nearest || contacts[i].distance < contacts[*nearest].distance))
            nearest = i;
    if (nearest && rows.size() > 1 && std::find(rows.begin(), rows.end(), *nearest) == rows.end()) {
        auto slot = rows.size() - 1;
        if (contacts[rows[slot]].selected)
            --slot;
        rows[slot] = *nearest;
    }
    return rows;
}
std::vector<size_t> cockpitCelestialRows(std::span<const CelestialContact> contacts) {
    std::vector<size_t> rows;
    for (unsigned priority = 0; priority < 3; priority++)
        for (size_t i = 0; i < contacts.size() && rows.size() < 8; i++) {
            const auto& c = contacts[i];
            const unsigned rank = c.selected ? 0 : hasSurfaceLife(c) ? 1 : 2;
            if (rank == priority)
                rows.push_back(i);
        }
    return rows;
}
std::optional<Address> nextLivingWorld(std::span<const CelestialContact> contacts,
                                       const std::optional<Address>& current) {
    std::vector<Address> worlds;
    for (const auto& c : contacts)
        if (hasSurfaceLife(c))
            worlds.push_back(c.address);
    if (worlds.empty())
        return {};
    std::sort(worlds.begin(), worlds.end());
    const auto next = current ? std::upper_bound(worlds.begin(), worlds.end(), *current) : worlds.begin();
    return next == worlds.end() ? worlds.front() : *next;
}
CockpitTargetMarker cockpitTargetMarker(glm::dvec3 d, double aspect, double projectionScale) {
    CockpitLayout l(aspect);
    const glm::dvec2 center{l.width * .5, 450};
    CockpitTargetMarker result;
    result.point = center;
    const double length = glm::length(d);
    if (!std::isfinite(length) || length < 1e-30)
        return result;
    d /= length;
    result.aft = d.z >= 0;
    glm::dvec2 offset{d.x, -d.y};
    if (!result.aft) {
        const auto projected = center + offset * (450 / .7002075 / -d.z * projectionScale);
        if (l.windscreen.contains(projected)) {
            result.point = projected;
            return result;
        }
    }
    result.offscreen = true;
    if (glm::length(offset) < 1e-12)
        offset = {0, 1};
    offset = glm::normalize(offset);
    result.direction = offset;
    const double limitX =
        (offset.x < 0 ? center.x - l.windscreen.x : l.windscreen.x + l.windscreen.w - center.x) - 12;
    const double limitY =
        (offset.y < 0 ? center.y - l.windscreen.y : l.windscreen.y + l.windscreen.h - center.y) - 12;
    const double distance =
        std::min(limitX / std::max(1e-12, std::abs(offset.x)), limitY / std::max(1e-12, std::abs(offset.y)));
    result.point = center + offset * distance;
    return result;
}
CockpitRect cockpitContactsSummary(){return {24,144,274,120};}
CockpitRect cockpitLifeBadge(double aspect,bool expanded){return expanded?CockpitRect{900*aspect-106,186,68,18}:CockpitRect{196,153,86,18};}
static std::vector<std::pair<std::string,glm::vec3>> computerDetails(const std::vector<CockpitAlert>& alerts,const FlightInstruments& t,const CrewView* crew,double detailWidth){
    std::vector<std::pair<std::string,glm::vec3>> details;
    auto detailOrder=alerts;
    std::stable_partition(detailOrder.begin(),detailOrder.end(),[](const auto& a){return a.key=="notice";});
    for(const auto& a:detailOrder){
        details.emplace_back(a.title,a.level==CockpitAlertLevel::Critical?danger:a.level==CockpitAlertLevel::Warning?amber:ice);
        const auto content=a.detail.empty()?a.action:a.detail;
        for(const auto& line:wrapped(content,detailWidth-28,9,12))details.emplace_back(line,muted);
    }
    if(!t.systemFault.empty())details.emplace_back(t.systemFault,ice);
    if(crew)for(const auto& line:wrapped(crew->summary,detailWidth-28,9,3))details.emplace_back(line,ice);
    if(!t.crewFire.empty())for(const auto& line:wrapped(t.crewFire,detailWidth-28,9,3))details.emplace_back(line,muted);
    details.emplace_back("FLIGHT "+std::to_string(int(std::min(1e9,t.flightSeconds)))+"S / "+flightQuantity(t.travelMetres),muted);
    if(!t.weather.empty())for(const auto& line:wrapped(t.weather+" / "+t.weatherDetail,detailWidth-28,9,4))details.emplace_back(line,muted);
    return details;
}
static CockpitRect computerPanel(double aspect,size_t rows){
    const double w=900*aspect;const bool wide=w>=1200;
    const double width=wide?std::clamp(w-656.,240.,650.):w-48;
    return {(w-width)*.5,wide?252.:624.,width,42+std::min(size_t(wide?25:6),rows)*13.};
}
static void drawAlertChannel(CockpitOverlay& h,const std::vector<CockpitAlert>& alerts,double aspect,bool expanded,bool probe,bool compact,const FlightInstruments& t,const CrewView* crew){
    if(alerts.empty()&&!expanded)return;
    const double w=900*aspect,width=w<900?std::max(24.,w-48):std::clamp(w-656.,240.,650.);
    if(!alerts.empty()){
    const auto& primary=alerts.front();const auto color=primary.level==CockpitAlertLevel::Critical?danger:primary.level==CockpitAlertLevel::Warning?amber:ice;
    const double size=11;const auto heading=wrapped(primary.title,width-28,size,compact?1:3);
    std::string body=primary.action;
    if(body.empty()&&!primary.detail.empty()){
        body=primary.detail;if(body.starts_with(primary.title))body.erase(0,primary.title.size());
        while(body.starts_with(" / "))body.erase(0,3);
    }
    if(body.size()>size_t(std::max(30.,(width-28)*7/60)))body="CTRL-TAB MESSAGE DETAILS";
    auto lines=wrapped(body,width-28,9.5,compact?1:2);
    const auto receipt=compact||primary.key=="notice"?alerts.end():std::find_if(alerts.begin(),alerts.end(),[](const auto& a){return a.key=="notice";});
    const double height=20+heading.size()*15+lines.size()*13+(receipt!=alerts.end()?17:0)+(!compact&&alerts.size()>1?14:0);
    const CockpitRect panel{(w-width)*.5,probe?132.:w<900?170.:64.,width,height};
    occlude(h,panel);card(h,panel,color);h.popups.push_back({panel,primary.level==CockpitAlertLevel::Critical});
    std::erase_if(h.worldLabels,[&](const auto& r){return r.overlaps(panel);});
    double y=panel.y+10;
    for(const auto& line:heading){fitted(h,line,panel.x+14,y,panel.w-28,size,color);y+=15;}
    for(const auto& line:lines){fitted(h,line,panel.x+14,y,panel.w-28,9.5,ice);y+=13;}
    if(receipt!=alerts.end()){fitted(h,receipt->title,panel.x+14,y+3,panel.w-28,9.5,mint);y+=17;}
    if(!compact&&alerts.size()>1)fitted(h,"+"+std::to_string(alerts.size()-1)+" / CTRL-TAB DETAILS",panel.x+14,y+3,panel.w-28,8.5,muted);
    }
    if(expanded){
        const bool wide=w>=1200;
        const double detailWidth=wide?std::clamp(w-656.,240.,650.):w-48;
        const double x=(w-detailWidth)*.5;
        const auto details=computerDetails(alerts,t,crew,detailWidth);
        const auto count=std::min(size_t(wide?25:6),details.size());
        const auto detail=computerPanel(aspect,details.size());occlude(h,detail);card(h,detail,ice);
        fitted(h,"FLIGHT COMPUTER / "+std::to_string(alerts.size())+" ALERTS",x+14,detail.y+14,detailWidth-28,10.5,pearl);
        for(size_t i=0;i<count;++i)fitted(h,details[i].first,x+14,detail.y+40+i*13,detailWidth-28,9,details[i].second);
    }
}
static void drawContactSummary(CockpitOverlay& h,double aspect,std::span<const CelestialContact> contacts,std::span<const ActivityContact> activity){
    const auto panel=cockpitContactsSummary();card(h,panel,ice);
    fitted(h,"CONTACTS "+std::to_string(contacts.size()+activity.size()),38,155,150,10,ice);
    h.contactHits.push_back({{24,144,166,28},{},{},true,false});
    const auto living=std::count_if(contacts.begin(),contacts.end(),hasSurfaceLife);
    const auto badge=cockpitLifeBadge(aspect,false);
    if(living){rect(h.panels,badge.x,badge.y,badge.w,badge.h,mint*.14f);
        fitted(h,std::to_string(living)+" LIFE",badge.x+6,badge.y+4,badge.w-12,9.5,mint);
        h.contactHits.push_back({badge,{},{},false,true});}
    else fitted(h,"CTRL-TAB",badge.x,badge.y+4,badge.w,9,muted);
    const auto rows=cockpitSummaryRows(contacts,activity);
    for(size_t i=0;i<rows.size();++i){const auto row=rows[i];const auto y=181+i*25.;
        const auto name=row.activity?activity[row.index].body.name:contacts[row.index].label;
        const auto range=row.activity?activity[row.index].distance:contacts[row.index].distanceMetres;
        const bool selected=row.activity?activity[row.index].selected:contacts[row.index].selected;
        const auto color=row.activity?(activity[row.index].body.hostile?danger:selected?ice:pearl):hasSurfaceLife(contacts[row.index])?mint:ice;
        if(selected)rect(h.panels,30,y-4,262,23,ice*.12f,.9f);
        fitted(h,name,38,y,159,10.5,color);
        fitted(h,flightQuantity(range),201,y+1,82,9.5,muted,true);
        CockpitContactHit hit{{30,y-4,262,23},{},{},false,false};
        if(row.activity)hit.activity=activity[row.index].body.id;else hit.world=contacts[row.index].address;
        h.contactHits.push_back(std::move(hit));
    }
    if(rows.empty())fitted(h,"NO LOCAL CONTACTS / CTRL-TAB",38,190,246,9,muted);
}
CockpitRect surfaceReconPanel(double aspect){const double width=std::min(640.,900*aspect-48);return {(900*aspect-width)*.5,aspect<1?230.:200.,width,430};}
CockpitRect surfaceReconMapRect(double aspect){const auto p=surfaceReconPanel(aspect);return {p.x+16,p.y+48,p.w-32,176};}
static void drawSurfaceRecon(CockpitOverlay& h,const SurfaceReconView& v,double aspect){
    const auto panel=surfaceReconPanel(aspect),area=surfaceReconMapRect(aspect);
    occlude(h,panel);card(h,panel,ice);
    fitted(h,"SURFACE RECONNAISSANCE",panel.x+16,panel.y+14,panel.w-32,13,pearl);
    rect(h.panels,area.x,area.y,area.w,area.h,{.018,.028,.05},1);
    if(!v.map){fitted(h,v.status,area.x+12,area.y+70,area.w-24,10,ice);}
    else {
        const auto& map=*v.map;
        auto project=[&](glm::dvec2 p){return glm::dvec2(area.x+(p.x/map.radius+1)*.5*area.w,area.y+(p.y/map.radius+1)*.5*area.h);};
        auto point=[&](glm::dvec3 p){return project(reconPosition(map,p));};
        double low=1e30,high=-1e30;for(const auto& c:map.cells){low=std::min(low,double(c.height));high=std::max(high,double(c.height));}
        const double contourStep=std::max(20.,std::ceil((high-low)/240.)*20.);
        for(unsigned z=0;z+1<reconGrid;++z)for(unsigned x=0;x+1<reconGrid;++x){const auto& c=map.cells[z*reconGrid+x];
            const double elevation=(c.height-low)/std::max(15.,high-low);
            auto tint=glm::mix(glm::vec3(.055,.035,.13),glm::vec3(.18,.45,.43),float(elevation));
            tint*=float(1-std::min(.65,double(c.slope)));
            if(!c.dry)tint={.035,.09,.23};
            const auto a=project({-map.radius+x*map.radius*2/(reconGrid-1),-map.radius+z*map.radius*2/(reconGrid-1)});
            const double width=area.w/(reconGrid-1),height=area.h/(reconGrid-1);
            rect(h.panels,a.x,a.y,width+.1,height+.1,tint,.94f);
            const std::array<double,4> heights{c.height,map.cells[z*reconGrid+x+1].height,map.cells[(z+1)*reconGrid+x+1].height,map.cells[(z+1)*reconGrid+x].height};
            const std::array<glm::dvec2,4> corners{a,a+glm::dvec2(width,0),a+glm::dvec2(width,height),a+glm::dvec2(0,height)};
            const auto [minimum,maximum]=std::minmax_element(heights.begin(),heights.end());
            for(double level=std::ceil(*minimum/contourStep)*contourStep;level<=*maximum;level+=contourStep){
                std::array<glm::dvec2,4> hits;unsigned count=0;
                for(unsigned edge=0;edge<4;++edge){const unsigned next=(edge+1)%4;
                    if((heights[edge]<level)!=(heights[next]<level))hits[count++]=glm::mix(corners[edge],corners[next],(level-heights[edge])/(heights[next]-heights[edge]));}
                for(unsigned i=0;i+1<count;i+=2)line(h.strokes,hits[i].x,hits[i].y,hits[i+1].x,hits[i+1].y,ice*.38f);
            }
        }
        for(const auto& w:v.weather)if(w.severity>.3){const auto p=project(w.point);
            const double width=area.w/5,height=area.h/5;
            const double left=std::max(area.x,p.x-width*.5),top=std::max(area.y,p.y-height*.5);
            rect(h.panels,left,top,std::max(0.,std::min(area.x+area.w,left+width)-left),std::max(0.,std::min(area.y+area.h,top+height)-top),amber,float(w.severity*.20));}
        const auto ship=project(glm::clamp(v.ship,glm::dvec2(-map.radius),glm::dvec2(map.radius)));
        if(const auto selected=v.target()){const auto p=point(selected->metres);line(h.strokes,ship.x,ship.y,p.x,p.y,ice*.7f);}
        for(const auto& target:v.points){const auto p=point(target.metres);const bool selected=target.id==v.selected;
            const auto tint=target.kind==ReconKind::Incident?(target.phase==RecoveryPhase::Complete?mint:amber):target.kind==ReconKind::Assay?mint:target.kind==ReconKind::Estimate?trafficInk:ice;
            const double size=selected?7:4;
            diamond(h.strokes,p,size,tint);
            if(target.kind==ReconKind::Estimate){line(h.strokes,p.x-3,p.y,p.x+3,p.y,tint);}
            else if(target.kind==ReconKind::Assay){line(h.strokes,p.x-3,p.y,p.x+3,p.y,tint);line(h.strokes,p.x,p.y-3,p.x,p.y+3,tint);}
            else if(target.kind==ReconKind::Entrance){rect(h.panels,p.x-2,p.y-2,4,4,ice);}
            else if(target.kind==ReconKind::Incident){line(h.strokes,p.x,p.y-2,p.x,p.y+2,tint);}
            if(selected)diamond(h.strokes,p,11,tint*.65f);
        }
        auto heading=glm::dvec2(v.heading.x*area.w,v.heading.y*area.h);heading=glm::normalize(heading);
        const auto side=glm::dvec2(-heading.y,heading.x);
        triangle(h.panels,{ship+heading*7.,0},{ship-heading*4.+side*4.,0},{ship-heading*4.-side*4.,0},mint);
    }
    fitted(h,v.loading?v.status:v.status+" / SIGNAL "+std::to_string(int(v.scanQuality*100))+"%",panel.x+16,panel.y+234,panel.w-32,8,ice);
    const auto selected=v.target();fitted(h,selected?selected->name:"NO LOCAL RETURNS",panel.x+16,panel.y+254,panel.w-32,11,selected&&selected->kind==ReconKind::Incident?amber:pearl);
    const auto detail=wrapped(selected?selected->detail:"APPROACH SOLID TERRAIN / F5 CLOSE",panel.w-32,8.5);
    for(size_t i=0;i<std::min(size_t(2),detail.size());++i)fitted(h,detail[i],panel.x+16,panel.y+273+i*13,panel.w-32,8.5,ice);
    fitted(h,v.navigator,panel.x+16,panel.y+306,panel.w-32,8.5,v.navWatching?mint:muted);
    fitted(h,v.engineer,panel.x+16,panel.y+323,panel.w-32,8.5,v.engineerWatching?mint:muted);
    fitted(h,v.action,panel.x+16,panel.y+349,panel.w-32,9,amber);
    if(v.workProgress>0)bar(h.panels,panel.x+16,panel.y+369,panel.w-32,v.workProgress,mint);
    fitted(h,"CLICK MAP / SHIFT-F5 NEXT / F4 ROUTE / 0 RESPOND",panel.x+16,panel.y+389,panel.w-32,8,ice);
    fitted(h,"CTRL-F5 NAV / ALT-F5 ENGINEER / F5 CLOSE",panel.x+16,panel.y+407,panel.w-32,8,muted);
}
Mesh cockpitMesh(double aspect) {
    Mesh m;
    auto point = [&](glm::dvec2 p, double depth) {
        return glm::dvec3((p.x * 2 - 1) * aspect * .7002075 * depth, (1 - p.y * 2) * .7002075 * depth,
                          -depth);
    };
    auto tri = [&](glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, glm::vec3 col) {
        auto n = glm::normalize(glm::cross(b - a, c - a));
        if (glm::dot(n, -(a + b + c)) < 0)
            n = -n;
        const float diffuse =
            float(.48 + .52 * std::max(0., glm::dot(n, glm::normalize(glm::dvec3(.6, .7, 1)))));
        triangle(m, a, b, c, col * diffuse);
    };
    auto panel = [&](std::initializer_list<glm::dvec2> points, glm::vec3 color, double front = .76,
                     double bevel = .10) {
        std::vector<glm::dvec2> outer(points), inner;
        glm::dvec2 center{};
        for (auto p : outer)
            center += p;
        center /= outer.size();
        for (auto p : outer)
            inner.push_back(glm::mix(p, center, bevel));
        auto depth = [&](glm::dvec2 p) { return front / (1 + (p.y - .74) * .28); };
        for (size_t i = 1; i + 1 < inner.size(); i++)
            tri(point(inner[0], depth(inner[0])), point(inner[i], depth(inner[i])),
                point(inner[i + 1], depth(inner[i + 1])), color);
        for (size_t i = 0; i < outer.size(); i++) {
            const size_t j = (i + 1) % outer.size();
            auto a = point(inner[i], depth(inner[i])), b = point(inner[j], depth(inner[j]));
            auto c = point(outer[i], depth(outer[i]) + .035), d = point(outer[j], depth(outer[j]) + .035);
            tri(a, c, b, color * 1.5f + glm::vec3(.04f));
            tri(b, c, d, color * 1.5f + glm::vec3(.04f));
            auto e = point(outer[i], depth(outer[i]) + .18), f = point(outer[j], depth(outer[j]) + .18);
            tri(c, e, d, ink);
            tri(d, e, f, ink);
        }
    };
    const glm::vec3 titanium{.176, .255, .306}, dark{.067, .114, .149}, copper{.357, .255, .173};
    constexpr double top = .052, deck = .842;
    panel({{0, top - .05}, {.24, top - .025}, {.21, top + .009}, {0, top + .035}}, titanium, .86);
    panel({{1, top - .05}, {.76, top - .025}, {.79, top + .009}, {1, top + .035}}, titanium, .86);
    panel({{0, top}, {.032, top + .032}, {.085, deck - .045}, {.054, deck + .03}, {0, deck - .005}}, titanium,
          .7, .16);
    panel({{1, top}, {.968, top + .032}, {.915, deck - .045}, {.946, deck + .03}, {1, deck - .005}}, titanium,
          .7, .16);
    panel({{0, deck - .005}, {.13, deck - .055}, {.28, deck + .025}, {0, 1}}, titanium, .65);
    panel({{1, deck - .005}, {.87, deck - .055}, {.72, deck + .025}, {1, 1}}, titanium, .65);
    panel({{.13, deck}, {.87, deck}, {1, 1}, {0, 1}}, dark, .68, .035);
    panel({{.11, deck - .041}, {.14, deck - .043}, {.232, deck + .005}, {.216, deck + .011}}, copper, .62,
          .22);
    panel({{.89, deck - .041}, {.86, deck - .043}, {.768, deck + .005}, {.784, deck + .011}}, copper, .62,
          .22);
    for (int side : {0, 1})
        for (int i = 0; i < 6; i++) {
            double x = side ? 1 - .018 - i * .012 : .018 + i * .012;
            panel({{x, deck - .016}, {x + .006, deck - .016}, {x + .006, deck - .012}, {x, deck - .012}},
                  i == 5 ? amber : ice * .7f, .58, 0);
        }
    for (double y : {.25, .43, .61})
        for (int side : {0, 1}) {
            double x = .023 + (y - top) * .077;
            if (side)
                x = 1 - x;
            panel({{x - .003, y - .004}, {x + .003, y - .004}, {x + .003, y + .004}, {x - .003, y + .004}},
                  muted, .66, .35);
        }
    m.compact();
    return m;
}
CockpitOverlay cockpitOverlay(const FlightInstruments& t,double aspect,std::string_view mode,std::string_view notice,
                              std::span<const CelestialContact> contacts,std::span<const ActivityContact> activity,
                              bool help,double projectionScale,const MarketView* market,glm::dquat head,const CrewView* crew) {
    return cockpitOverlay(t,aspect,mode,notice,contacts,activity,help,projectionScale,market,head,crew,nullptr);
}
CockpitOverlay cockpitOverlay(const FlightInstruments& t, double aspect, std::string_view,
                              std::string_view notice, std::span<const CelestialContact> contacts,
                              std::span<const ActivityContact> activity, bool help, double projectionScale,
                              const MarketView* market, glm::dquat headInShip, const CrewView* crew,const ExpeditionPlan* planner) {
    CockpitOverlay h;
    std::optional<CockpitRect> economicPanel;
    std::optional<CockpitRect> crewCommandPanel;
    std::optional<CockpitRect> stationPanel;
    std::optional<CockpitRect> traversePanel;
    const CockpitLayout l(aspect);
    const double w = l.width, center = w * .5;
    const auto eyeFromShip = glm::inverse(headInShip);
    const auto aimPoint =
        cockpitTargetMarker(eyeFromShip * (t.gunAim * glm::dvec3(0, 0, -1)), aspect, projectionScale).point;
    const double ratio = std::max(1e-12, t.hullMetres / 12);
    const bool specimen = t.phase == "SPECIMEN INSPECTION";
    const auto focus=cockpitFocus(t);
    const bool docking=focus.mode==CockpitMode::Docking;
    const auto landing=t.landing.phase;
    const bool surface=!focus.warp&&(focus.mode==CockpitMode::Landing||focus.mode==CockpitMode::Surface)&&t.altitude>=0;
    const bool modal=help||(market&&market->open)||(crew&&crew->open)||(planner&&planner->open);
    const bool reconOpen=t.recon&&t.recon->open&&!modal&&!(crew&&crew->flightOpen);
    const bool flightCommands=crew&&crew->flightOpen&&!modal;
    const bool catalogues=t.contactsExpanded&&!modal&&!reconOpen;
    const bool missileAlert=glm::length(t.localMissile)>.5;
    const auto living=std::count_if(contacts.begin(),contacts.end(),hasSurfaceLife);
    const auto alerts=cockpitAlerts(t,crew,notice);
    auto finish=[&](CockpitOverlay view){
        drawAlertChannel(view,alerts,aspect,catalogues&&!flightCommands,focus.mode==CockpitMode::Probe&&!(planner&&planner->open),modal,t,crew);
        return view;
    };
    const auto population = [](uint64_t people) {
        char value[40];
        if (people >= 1000000)
            std::snprintf(value, sizeof(value), "%.1fM", double(people) / 1000000);
        else if (people >= 1000)
            std::snprintf(value, sizeof(value), "%.1fK", double(people) / 1000);
        else
            std::snprintf(value, sizeof(value), "%llu", (unsigned long long)people);
        return std::string(value);
    };
    auto label = [&](std::string_view s, double x, double y, double width, glm::vec3 color = muted) {
        fitted(h, s, x, y, width, 10.5, color);
    };
    auto quantity = [&](std::string_view s, double x, double y, double width, double size = 14,
                        glm::vec3 color = pearl) {
        // Unit case is intentional: mm and m must remain distinguishable.
        size = std::min(size, width * 7 / (6 * std::max(size_t(1), s.size())));
        fitted(h, s, x, y, width, size, color, true);
    };
    if (t.phase == "MINING PROBE CAMERA" && !(planner&&planner->open)) {
        card(h,{24,28,w-48,94},ice);
        quantity("MINING PROBE / LIVE FEED",38,43,w-76,18,pearl);
        label(t.exploration[0],38,79,w-76,amber);
        if(planner&&planner->tracking)fitted(h,planner->compact,38,101,w-76,8.5,mint);
        card(h,{24,772,w-48,102},ice);
        quantity(t.exploration[1],38,788,w-76,17,mint);
        label(t.exploration[2],38,821,w-76,pearl);
        label(t.exploration[3],38,849,w-76,ice);
        for (unsigned k=0;k<4;++k) {
            const double a=k*1.5707963267948966;
            line(h.strokes,center+std::cos(a)*9,450+std::sin(a)*9,
                           center+std::cos(a)*21,450+std::sin(a)*21,ice);
        }
        return finish(std::move(h));
    }
    char value[96];
    rect(h.panels,0,0,w,46,ink,.98f);h.hitRegions.push_back({0,0,w,46});
    text(h,"INFINITY",26,15,17.5,pearl);
    fitted(h,focus.title,center-160,17,320,13,focus.mode==CockpitMode::Combat?danger:ice);
    const auto headerHint=focus.mode==CockpitMode::Combat&&!t.crewFire.empty()?t.crewFire.substr(0,t.crewFire.find(" / "))+" / 6 7 8":t.contactsExpanded?"CTRL-TAB CLOSE CONTACTS":"CTRL-TAB CONTACTS";
    fitted(h,headerHint,w-264,20,240,8.5,muted);
    line(h.strokes,24,45,w-24,45,muted*.3f);
    const bool changingScale=t.shrinking||std::abs(t.scaleRate)>.02;
    const CockpitRect scale{24,64,274,changingScale?76.:58.};card(h,scale,ice);
    fitted(h,specimen?"SPECIMEN / SHIP PARKED":t.shrinking?"SHRINK FIELD":"SHIP / FULL SIZE",38,75,246,9,muted);
    quantity(flightQuantity(t.hullMetres),38,94,246,21,pearl);
    if(changingScale){
        const auto note=t.shrinking?"FRAGILE X"+std::to_string(int(std::lround(shipDamageMultiplier(ratio))))+" / TARGET "+flightQuantity(t.targetHullMetres):
            "VIEW "+flightQuantity(std::pow(10.,t.logMetres))+(t.scaleRate<0?" / REFINING":" / WIDENING");
        quantity(note,38,123,246,9.5,t.shrinking?amber:ice);
    }
    const bool showClearance=t.altitude>=0&&!focus.warp&&(focus.mode==CockpitMode::Approach||focus.mode==CockpitMode::Landing||focus.mode==CockpitMode::Surface);
    if(showClearance){
        card(h,{w-310,64,286,92},ice);
        label(t.phase=="CAVERN EXPLORATION"?"WALL CLEARANCE":t.shrinking?"PARENT TERRAIN / AGL":"ALTITUDE / AGL",w-296,76,258);
        quantity(flightQuantity(t.landed?0.:std::max(0.,t.altitude)),w-296,96,258,28,pearl);
        quantity(std::string(t.descent<0?"UP ":"DOWN ")+flightQuantity(std::abs(t.descent),true)+" / DRIFT "+flightQuantity(t.drift,true),w-296,135,258,9.5,ice);
    }
    const bool weatherUseful=!catalogues&&!focus.warp&&!t.weather.empty()&&(focus.mode==CockpitMode::Surface||focus.mode==CockpitMode::Landing||t.weatherSeverity>.35||t.environmentalHeat>.2||t.electricalCharge>.2);
    if(weatherUseful){
        const CockpitRect weather{w-310,showClearance?164.:64.,286,t.weatherDetail.empty()?42.:58.};card(h,weather,ice);
        const auto lines=wrapped(t.weather,258,9.5,2);
        for(size_t i=0;i<lines.size();++i)fitted(h,lines[i],w-296,weather.y+10+i*13,258,9.5,ice);
        if(!t.weatherDetail.empty())fitted(h,t.weatherDetail,w-296,weather.y+weather.h-15,258,8.5,muted);
    }
    const auto& nav=l.navigation;const auto& flight=l.flight;const auto& scope=l.scope;const auto& systems=l.systems;
    for(const auto panel:{nav,flight,scope,systems})card(h,panel,ice);
    label(focus.mode==CockpitMode::Combat?"TARGET":focus.mode==CockpitMode::Surface?"WORKSITE":"DESTINATION",nav.x+14,nav.y+12,nav.w-28,ice);
    const auto destination=cockpitDestination(t);
    const double nameSize=destination.size()*18*6/7.<=nav.w-28?18.:13.;
    const auto destinationLines=wrapped(destination,nav.w-28,nameSize,2);
    for(size_t i=0;i<destinationLines.size();++i)fitted(h,destinationLines[i],nav.x+14,nav.y+34+i*17,nav.w-28,nameSize,pearl);
    const auto range=t.targetVisible||t.navigating?flightQuantity(t.range):std::string("SELECT WITH Y / K");
    const auto eta=t.etaSeconds>=0&&t.etaSeconds<10000?" / ETA "+std::to_string(int(std::ceil(t.etaSeconds)))+"S":"";
    quantity(range+eta,nav.x+14,nav.y+73,nav.w-28,12,ice);
    fitted(h,focus.nextAction,nav.x+14,nav.y+105,nav.w-28,10.5,ice);

    const bool warp=focus.warp;
    label(warp?"WARP VELOCITY":t.relativeMotion?"RELATIVE SPEED":"VELOCITY",flight.x+14,flight.y+12,flight.w-28,ice);
    constexpr double lightSpeed=299792458.;
    auto velocity=flightQuantity(t.relativeMotion&&!warp?t.relativeSpeed:t.speed,true);
    if(warp&&t.speed>=lightSpeed*.1){const double c=t.speed/lightSpeed;std::snprintf(value,sizeof(value),c<10000?"%.1f C":"%.2e C",c);velocity=value;}
    quantity(velocity,flight.x+14,flight.y+34,flight.w-28,30,pearl);
    const auto brakeTime=t.brakingSeconds<10000?std::to_string(int(std::ceil(t.brakingSeconds)))+"S":"--";
    quantity(warp?"LIMIT "+flightQuantity(t.driveSpeed,true):"STOP "+flightQuantity(t.stoppingDistance)+" / "+brakeTime,
             flight.x+14,flight.y+76,flight.w-28,10.5,ice);
    std::snprintf(value,sizeof(value),"%s / %.1fG",t.assist?"ASSIST":"INERTIAL",std::min(999.,t.properG));
    fitted(h,warp?(t.warp==WarpPhase::Drop?"RETURNING TO NORMAL SPACE":"N DROP / B ABORT"):value,flight.x+14,flight.y+105,flight.w-28,9.5,muted);

    const double radarX=scope.x+scope.w*.5,radarY=scope.y+73;
    const double radarRadius=std::min(90.,(scope.w-28)*.5);
    const bool landingDirector=focus.mode==CockpitMode::Landing;
    const bool dockingDirector=focus.mode==CockpitMode::Docking;
    label(dockingDirector?"DOCKING DIRECTOR":landingDirector?"LANDING DIRECTOR":focus.mode==CockpitMode::Combat?"TACTICAL SCOPE":focus.mode==CockpitMode::Surface?"LOCAL SURVEY":"NAVIGATION SCOPE",scope.x+14,scope.y+12,scope.w-28,ice);
    for(double r:{.5,1.})for(int i=0;i<48;++i){const double a=i*6.283185307/48,b=(i+1)*6.283185307/48;
        line(h.strokes,radarX+std::cos(a)*radarRadius*r,radarY+std::sin(a)*23*r,radarX+std::cos(b)*radarRadius*r,radarY+std::sin(b)*23*r,muted*float(r==1?.65:.22));}
    auto scopePoint=[&](glm::dvec3 p,glm::vec3 color,bool selected){
        const auto base=glm::dvec2(radarX+p.x*radarRadius*.85,radarY+p.z*20);const auto tip=base-glm::dvec2(0,p.y*17);
        line(h.strokes,base.x,base.y,tip.x,tip.y,color*.5f);diamond(h.strokes,tip,selected?5.:2.5,color);
    };
    if(dockingDirector){const auto& d=t.docking;const bool aligned=d.heading<.045&&std::abs(d.roll)<.06;
        diamond(h.strokes,{radarX+std::clamp(d.offset.x/std::max(1.,d.width),-1.,1.)*radarRadius*.78,radarY-std::clamp(d.offset.y/std::max(1.,d.width*.52),-1.,1.)*21},5,aligned?mint:ice);
        const double angle=std::clamp(d.roll,-1.4,1.4);line(h.strokes,radarX-19*std::cos(angle),radarY+19*std::sin(angle),radarX+19*std::cos(angle),radarY-19*std::sin(angle),ice);
        fitted(h,d.docked?"CLAMPS SECURE":dockingPhaseName(d.phase),scope.x+14,scope.y+32,scope.w-28,11,mint);
        quantity("LATERAL "+flightQuantity(glm::length(glm::dvec2(d.offset)))+" / "+flightQuantity(d.closing,true),scope.x+14,scope.y+105,scope.w-28,9.5,ice);
    }else if(landingDirector){
        const double shift=std::clamp(t.pitch*38,-18.,18.);const bool aligned=t.safe&&(!t.landing.body||glm::length(t.landing.localOffset)<6*ratio);
        for(int side:{-1,1}){const double a=side*12.,b=side*radarRadius*.8;line(h.strokes,radarX+a*std::cos(t.roll),radarY+shift-a*.28*std::sin(t.roll),radarX+b*std::cos(t.roll),radarY+shift-b*.28*std::sin(t.roll),aligned?mint:ice);}
        diamond(h.strokes,{radarX,radarY},4,pearl);
        if(t.landing.body&&!t.landed){const double scale=std::max(12*ratio,std::max(0.,t.altitude)*.9);const auto offset=t.landing.localOffset/scale;
            diamond(h.strokes,{radarX+std::clamp(offset.x,-1.,1.)*radarRadius*.82,radarY-std::clamp(offset.y,-1.,1.)*21},8,aligned?mint:ice);}
        fitted(h,t.landed?"TOUCHDOWN SECURE":landing==LandingPhase::Flare?"FLARE":aligned?"ALIGNED":"MATCH THE HORIZON",scope.x+14,scope.y+32,scope.w-28,10.5,ice);
        std::snprintf(value,sizeof(value),"ROLL %.0f / PITCH %.0f",t.roll*57.2957795,t.pitch*57.2957795);
        fitted(h,value,scope.x+14,scope.y+105,scope.w-28,9.5,muted);
    }else{
        size_t contextDots=0;
        for(const auto& c:activity)if(!c.occluded&&(c.selected||c.body.hostile||contextDots++<18))scopePoint(c.localDirection,c.body.hostile?danger:c.selected?ice:muted*.6f,c.selected);
        contextDots=0;for(const auto& c:contacts)if(!c.occluded&&!c.selected&&contextDots++<8)scopePoint(c.localDirection,hasSurfaceLife(c)?mint*.6f:muted*.45f,false);
        if(t.targetVisible&&glm::length(t.localTarget)>1e-30)scopePoint(glm::normalize(t.localTarget),ice,true);
        line(h.strokes,radarX,radarY-5,radarX-4,radarY+4,mint);line(h.strokes,radarX-4,radarY+4,radarX+4,radarY+4,mint);line(h.strokes,radarX+4,radarY+4,radarX,radarY-5,mint);
        fitted(h,warp?(t.warp==WarpPhase::Drop?"DESTINATION REVEAL":t.warpDestination?"DESTINATION LOCKED":"MANUAL COURSE"):
            focus.mode==CockpitMode::Surface?(t.shrinking?"MICRO / NANO":"F5 TERRAIN MAP"):t.targetVisible?(t.localTarget.z>=0?"DESTINATION AFT":"DESTINATION AHEAD"):"FORWARD",scope.x+14,scope.y+32,scope.w-28,10,ice);
        if(warp&&t.warpExitSeconds>=0)quantity("DROP ~"+std::to_string(int(std::ceil(t.warpExitSeconds)))+"S",scope.x+14,scope.y+105,scope.w-28,10.5,mint);
        else fitted(h,focus.mode==CockpitMode::Combat?"H SELECT / SPACE FIRE":focus.mode==CockpitMode::Surface?"F12 SCAN / F11 JOURNAL":"CTRL-TAB CONTACTS",scope.x+14,scope.y+105,scope.w-28,9,muted);
    }
    const double gaugeWidth=(systems.w-42)*.5;
    for(int i=0;i<2;++i){const double x=systems.x+14+i*(gaugeWidth+14);
        label(i?"HULL":"THROTTLE",x,systems.y+12,gaugeWidth,ice);
        std::snprintf(value,sizeof(value),"%.0f%%",i?t.hull:t.throttle*100);
        quantity(value,x,systems.y+34,gaugeWidth,29,i&&t.hull<25?danger:pearl);
        bar(h.panels,x,systems.y+73,gaugeWidth,i?t.hull/100:t.throttle,i?(t.hull<25?danger:mint):ice);
    }
    if(market)quantity(std::to_string(market->credits/100)+" CR / "+std::to_string(market->cargoKg)+" KG",systems.x+14,systems.y+90,systems.w-28,9.5,muted);
    const auto systemHint=focus.mode==CockpitMode::Landing||focus.mode==CockpitMode::Surface?(t.gear?"G GEAR DOWN":"G GEAR UP"):"F9 CREW / F8 DECK";
    fitted(h,systemHint,systems.x+14,systems.y+108,systems.w-28,9.5,ice);
    rect(h.panels,0,884,w,16,ink);h.hitRegions.push_back({0,884,w,16});
    fitted(h,"F1 HELP",26,888,90,8.5,muted);
    fitted(h,"F2 TRADE / F3 PLAN / F5 MAP / F9 CREW",std::max(130.,center-220),888,std::max(100.,w-440),8,muted);
    fitted(h,"CTRL-TAB CONTACTS / TAB HUD",w-245,888,221,8,muted);
    if(planner&&planner->tracking&&!planner->open&&!help&&!(crew&&(crew->open||crew->flightOpen))&&!(market&&market->open)){
        const CockpitRect progress{center-205,646,410,22};card(h,progress,ice);fitted(h,planner->compact,progress.x+12,progress.y+7,progress.w-24,8.5,mint);
    }
    if(planner&&planner->open) {
        const auto& plan=*planner;const PlannerLayout layout(aspect,plan.details,&plan);const auto p=layout.panel,a=layout.left,b=layout.right;
        occlude(h,p);card(h,p,mint);drawPlannerDiagrams(h,plan,layout);
        if(!plan.details){
            const auto summary=summarizePlan(plan);
            fitted(h,"EXPEDITION PLANNER",p.x+24,p.y+19,p.w-230,12,pearl);
            const auto phase=(summary.step?std::to_string(summary.step)+"/10 / ":"")+std::string(planStageName(plan.stage));
            fitted(h,phase,p.x+p.w-204,p.y+22,180,8.7,mint);
            const auto destination=plan.returnToPort||plan.stage==PlanStage::Deliver||plan.stage==PlanStage::Complete?plan.issuerName:plan.title;
            fitted(h,plan.contract?destination:plan.stage==PlanStage::Loading?"FINDING LOCAL CONTRACTS":"NO LOCAL ASSAY OFFERS",p.x+60,p.y+53,p.w-84,16,pearl);
            fitted(h,plan.reward,p.x+60,p.y+80,p.w-84,8.4,muted);
            const auto lines=wrapped(summary.instruction,p.w-48,10,3);
            for(size_t i=0;i<lines.size();++i)fitted(h,lines[i],layout.body.x,layout.body.y+i*13,layout.body.w,10,ice);
            if(plan.debrief){const auto& invoice=plan.accounting;const auto r=layout.debrief;
                if(invoice.recorded){
                    const auto net=invoice.netCash();
                    fitted(h,"NET CASH / "+crewMoney(net)+" CR",r.x,r.y,r.w,13,net>=0?mint:amber);
                    fitted(h,"TOOLS "+crewMoney(invoice.equipment)+" / PROBES "+crewMoney(invoice.ammunition)+" / PAID REPAIRS "+crewMoney(invoice.repairs)+" CR",r.x,r.y+21,r.w,8.2,muted);
                    fitted(h,std::to_string(invoice.probesUsed)+" PROBES USED / "+std::to_string(invoice.extractedKg)+" KG MINED / RETURN HULL "+std::to_string(int(invoice.returnHull))+"%",r.x,r.y+37,r.w,8.2,invoice.returnHull<invoice.departureHull?amber:ice);
                }else fitted(h,"COST HISTORY UNAVAILABLE FOR THIS OLDER EXPEDITION",r.x,r.y,r.w,8.3,muted);
            }
            auto context=summary.context;bool warning=summary.warning;
            if(plan.visual.shipMetres<.012&&plan.stage!=PlanStage::Loading){context="SHIP / "+flightQuantity(plan.visual.shipMetres)+" / FRAGILE";warning=true;}
            fitted(h,context,layout.status.x,layout.status.y,layout.status.w,8.8,warning?amber:mint);
            card(h,layout.actions[0],plan.primaryEnabled?mint:muted);
            const auto action=plan.primary==PlannerAction::Resume?std::string("RETURN TO FLIGHT"):plan.primaryLabel;
            fitted(h,action+" / ENTER",layout.actions[0].x+12,layout.actions[0].y+12,layout.actions[0].w-24,9,plan.primaryEnabled?pearl:muted);
            card(h,layout.details,ice);fitted(h,"DETAILS / D",layout.details.x+12,layout.details.y+12,layout.details.w-24,8.3,ice);
            const auto receipt=wrapped(plan.busy?"AWAITING SAVED RECEIPT":plan.debrief&&plan.message.starts_with("CAVERN ASSAY COMPLETE")?"":plan.message,p.w-48,8.5,2);
            for(size_t i=0;i<receipt.size();++i)fitted(h,receipt[i],layout.receipt.x,layout.receipt.y+i*12,layout.receipt.w,8.5,amber);
            fitted(h,"UP/DOWN CONTRACT / F4 TARGET",p.x+24,layout.footerY,p.w-150,7.3,muted);
            fitted(h,"ESC CLOSE",layout.close.x,layout.close.y+3,layout.close.w,7.3,muted);
            return finish(std::move(h));
        }
        fitted(h,"EXPEDITION PLANNER",p.x+18,128,p.w-290,17.5,pearl);
        fitted(h,planStageName(plan.stage),p.x+p.w-252,131,234,10.5,mint);
        fitted(h,plan.details?"UP-DOWN CONTRACT / B CAVERN / N NEW SITE":"UP / DOWN CONTRACT",p.x+18,157,p.w-170,8,muted);
        card(h,layout.details,ice);fitted(h,plan.details?"SUMMARY / D":"DETAILS / D",layout.details.x+9,layout.details.y+7,layout.details.w-18,8,ice);
        fitted(h,"CLOSE / ESC",layout.close.x,layout.close.y+3,layout.close.w,7.5,muted);
        for(size_t row=0;row<(plan.details?layout.offers.size():size_t(1))&&plan.firstOffer+row<plan.offers.size();++row){
            const size_t i=plan.firstOffer+row;const auto& offer=plan.offers[i];const auto r=layout.offers[row];
            const bool selected=i==plan.selected;card(h,r,selected?mint:muted);
            const auto phase=offer.phase==SurveyPhase::Open?"OPEN":offer.phase==SurveyPhase::Accepted?"ACTIVE":offer.phase==SurveyPhase::Complete?"PAID":"CLOSED";
            fitted(h,std::string(selected?"+ ":"  ")+phase+" / "+offer.title,r.x+6,r.y+7,r.w-12,8.5,selected?pearl:muted);
        }
        fitted(h,"WORLD / "+plan.title,a.x+25,plan.details?293:237,a.w-25,10.5,mint);
        fitted(h,plan.geology,a.x,plan.details?315:253,plan.details?a.w*.58:a.w,8.2,pearl);
        if(plan.details)fitted(h,plan.conditions,a.x+a.w*.60,315,a.w*.40,8.2,ice);
        const auto& visual=plan.visual;
        const double diagramY=layout.section.y;
        fitted(h,visual.hasBore?"SAVED EXCAVATION":"PLANNED SECTION",a.x+9,diagramY+8,a.w*.64,8,visual.hasBore?mint:muted);
        fitted(h,"SCHEMATIC",a.x+a.w*.73,diagramY+8,a.w*.24,7.2,muted);
        const double keyX=a.x+a.w*.71,keyWidth=a.w*.27;
        fitted(h,std::to_string(int(std::lround(visual.boreDepth)))+" M TARGET",keyX,diagramY+57,keyWidth,8,ice);
        fitted(h,visual.hasBore?std::to_string(int(std::lround(visual.reached)))+" M CUT":"UNSCANNED",keyX,diagramY+77,keyWidth,8,visual.hasBore?pearl:muted);
        if(visual.chamberReady){const auto& deposit=visual.deposits[std::min(plan.find,cavernFindCount-1)];
            fitted(h,deposit.grade?"ASSAY "+std::to_string(deposit.grade):"UNSCANNED",keyX,diagramY+99,keyWidth,8,deposit.grade?mint:muted);
            if(plan.details||deposit.marked)fitted(h,deposit.marked?"MARKED":"UNMARKED",keyX,diagramY+119,keyWidth,8,deposit.marked?mint:muted);
        }else if(visual.hasBore){
            fitted(h,"HEAT "+std::to_string(int(std::lround(visual.heat*100)))+"%",keyX,diagramY+99,keyWidth,8,visual.heat>.65?amber:ice);
            if(plan.details||visual.phase==ProbePhase::Paused)fitted(h,probePhaseName(visual.phase),keyX,diagramY+119,keyWidth,7.5,ice);
        }
        const auto shipSize=flightQuantity(visual.shipMetres)+(visual.shipMetres<.012?" / FRAGILE":visual.shipMetres>=11.88?" / FULL SIZE":" / SHRUNK");
        fitted(h,"SHIP / "+shipSize,a.x+9,488,a.w*.62,7.7,visual.shipMetres<.012?amber:mint);
        fitted(h,visual.hasBore?"LOCAL RECORD":"BULK ESTIMATE",a.x+a.w*.64,488,a.w*.34,7.2,muted);
        const std::array materialNames{"ICE","ORE","SIL","CAR"};
        const std::array<glm::vec3,4> materialColors{ice,amber,trafficInk,muted};
        if(plan.details)for(unsigned i=0;i<materialNames.size();++i)fitted(h,std::string(materialNames[i])+" "+std::to_string(int(std::lround(visual.composition[i]*100)))+"%",a.x+i*a.w/4,523,a.w/4-4,7.3,materialColors[i]);
        else fitted(h,"BULK MATERIALS / ESTIMATE",a.x,525,a.w,7.3,muted);
        fitted(h,plan.reward,a.x,plan.details?541:213,a.w,8.8,pearl);
        if(plan.details)fitted(h,"ISSUER / "+plan.issuerName,a.x,555,a.w,7.3,muted);
        const std::array names{"DEPTH / ARROWS","FEED / SHIFT-ARROWS","BAY / CTRL-ARROWS"};
        const std::array values{std::to_string(int(plan.depth))+" M",std::to_string(int(std::lround(plan.feed*100)))+"%",std::string(weaponMountName(plan.bay))};
        if(plan.details)for(size_t i=0;i<names.size();++i){
            fitted(h,names[i],b.x,189+i*28,b.w-174,8.2,ice);
            fitted(h,values[i],b.x+b.w-166,188+i*28,100,9.5,pearl);
            for(const auto r:{layout.minus[i],layout.plus[i]})card(h,r,ice);
            fitted(h,"-",layout.minus[i].x+8,layout.minus[i].y+7,14,9,ice);
            fitted(h,"+",layout.plus[i].x+8,layout.plus[i].y+7,14,9,ice);
        }
        fitted(h,"NEXT",b.x,plan.details?285:234,b.w,9,mint);
        fitted(h,plan.objective,b.x,plan.details?307:256,b.w,plan.details?11.5:13,pearl);
        const auto instructions=wrapped(plan.instruction,b.w,8.6,3);
        for(size_t i=0;i<instructions.size();++i)fitted(h,instructions[i],b.x,(plan.details?330:283)+i*12,b.w,8.6,ice);
        fitted(h,"SHIP / TOOL STATIONS",b.x,373,b.w*.46,8,ice);
        for(unsigned i=0;i<layout.bays.size();++i){const auto r=layout.bays[i];const auto& station=visual.bays[i];
            const std::array bayNames{"NOSE","PORT","STARBOARD"};
            fitted(h,bayNames[i],r.x+7,r.y+5,r.w-17,7.3,i==plan.bay?ice:muted);
            const auto tool=station.tool==PlannerTool::Probe?"PROBE / "+std::to_string(station.rounds):station.tool==PlannerTool::Miner?"MINER "+std::to_string(station.level):station.tool==PlannerTool::Gun?"GUN "+std::string(weaponGrade(station.level)):"EMPTY BAY";
            fitted(h,tool,r.x+7,r.y+51,r.w-14,7.9,station.tool==PlannerTool::Empty?muted:pearl);
            const bool staffed=station.operatorReadiness>=(station.tool==PlannerTool::Probe?.25:.0001);
            fitted(h,station.tool==PlannerTool::Empty?"UNFITTED":station.captain?"CAPTAIN":staffed?"CREW READY":"NEEDS CREW",r.x+20,r.y+65,r.w-28,6.8,staffed?mint:amber);
        }
        auto ready=[&](unsigned i){return i<plan.checks.size()&&plan.checks[i].ready;};const double half=(b.w-8)*.5;
        if(plan.details){fitted(h,ready(3)?"OPERATORS READY":"ASSIGN CREW / F9",b.x+18,481,half-22,7.8,ready(3)?mint:amber);
            fitted(h,ready(4)?"SAMPLE SPACE / OK":"HOLD FULL",b.x+half+26,481,half-22,7.8,ready(4)?mint:amber);
            fitted(h,ready(5)?"FULL SIZE / HULL OK":"CHECK HULL / SIZE",b.x+18,504,half-22,7.8,ready(5)?mint:amber);
            fitted(h,ready(6)?"NAV / ENG READY":"NAV / ENG OPTIONAL",b.x+half+26,504,half-22,7.8,ready(6)?mint:muted);
        }else{const std::array labels{"CREW","HOLD","SHIP","NAV"};const double width=(b.w-24)/4;
            for(unsigned i=0;i<labels.size();++i)fitted(h,labels[i],b.x+i*(width+8)+17,481,width-21,7.5,ready(i+3)?mint:i==3?muted:amber);}
        fitted(h,plan.stage==PlanStage::Complete?"SAMPLE DELIVERED":std::to_string(int(visual.sampleKg))+" / 10 KG SAMPLE ABOARD",b.x+124,530,b.w-124,8,mint);
        fitted(h,plan.details?"R / SUGGESTED "+std::to_string(int(plan.suggestedDepth))+" M / "+std::to_string(int(std::lround(plan.suggestedFeed*100)))+"% FEED":"NEXT PROBE / "+std::to_string(int(plan.depth))+" M / "+std::to_string(int(std::lround(plan.feed*100)))+"% FEED",b.x,552,b.w,8,ice);
        const std::array routeNames{"PREPARE","TRAVEL","DRILL","ENTER","SCAN","SHRINK","MARK","REGROW","MINE","RETURN"};
        for(unsigned i=0;i<routeNames.size();++i){const auto r=layout.steps[i];const auto& step=plan.steps[i];const double width=std::string_view(routeNames[i]).size()*8*6/7.;
            fitted(h,routeNames[i],r.x+(r.w-width)*.5,615,r.w-4,8,step.done?mint:step.active?pearl:muted);}
        const std::array buttons{plan.primaryLabel,std::string("TARGET OBJECTIVE / F4"),std::string("SUPPLIES / F2"),
            std::string("CREW / F9"),std::string(plan.tracking?"HIDE TRACKER / T":"TRACK CONTRACT / T"),std::string("USE SUGGESTED / R")};
        for(size_t i=0;i<buttons.size();++i){const auto r=layout.actions[i];if(r.w<=0)continue;const bool enabled=i?true:plan.primaryEnabled;
            card(h,r,enabled?i?ice:mint:muted);fitted(h,buttons[i],r.x+10,r.y+10,r.w-20,8.5,enabled?pearl:muted);}
        fitted(h,plan.busy?"AWAITING FLIGHT COMPUTER / SAVED RECEIPT":plan.message,p.x+18,plan.details?728:670,p.w-150,8.3,amber);
        return finish(std::move(h));
    }
    if (crew && crew->open) {
        const CrewPanelLayout layout(aspect);
        const auto p = layout.panel;
        const double map = p.w * .35, rx = p.x + map + 20, rw = p.w - map - 40;
        occlude(h,p);card(h, p, mint);
        fitted(h, "SHIP CREW / STATIONS AND DAMAGE CONTROL", p.x + 18, p.y + 15, p.w - 36, 15,
               pearl);
        fitted(h, crew->summary + " / AUTO " + (crew->autoDamageControl ? "ON" : "OFF"), p.x + 18,
               p.y + 39, p.w - 36, 9.5, ice);
        auto portEdge = [](const CockpitRect& r, glm::dvec2 toward) {
            const glm::dvec2 centre{r.x + r.w * .5, r.y + 48};
            const auto d = toward - centre;
            const double tx = std::abs(d.x) > 1e-9 ? ((d.x > 0 ? r.x + r.w : r.x) - centre.x) / d.x : 1e10;
            const double ty = std::abs(d.y) > 1e-9 ? ((d.y > 0 ? r.y + r.h : r.y) - centre.y) / d.y : 1e10;
            return centre + d * std::min(tx, ty);
        };
        for (auto [a, b] : crewCorridors()) {
            const auto ra = layout.rooms[a], rb = layout.rooms[b];
            const auto x = portEdge(ra, {rb.x + rb.w * .5, rb.y + 48}),
                       y = portEdge(rb, {ra.x + ra.w * .5, ra.y + 48});
            line(h.strokes, x.x, x.y, y.x, y.y, muted * .7f);
        }
        for (size_t i = 0; i < crewRoomCount; ++i) {
            const auto r = layout.rooms[i];
            const auto& room = crew->rooms[i];
            const auto colour = room.fire > 1         ? glm::vec3(1, .3, .12)
                                : room.oxygen < 25    ? amber
                                : room.integrity < 70 ? trafficInk
                                                      : ice;
            card(h, r, crew->station == CrewStation(i) ? mint : colour);
            fitted(h, crewStationName(CrewStation(i)), r.x + 5, r.y + 6, r.w - 10, 8, pearl);
            fitted(h,
                   "SYS " + std::to_string(int(room.integrity)) + " O2 " + std::to_string(int(room.oxygen)),
                   r.x + 5, r.y + 20, r.w - 10, 7.6, colour);
            fitted(h,
                   room.vented
                       ? "VENT OPEN"
                       : "F" + std::to_string(int(room.fire)) + " B" + std::to_string(int(room.breach)),
                   r.x + 5, r.y + 33, r.w - 10, 7.6, colour);
        }
        for (size_t i = 0; i < crew->members.size(); ++i) {
            const auto& m = crew->members[i];
            const auto row = layout.roster[i];
            const auto c = m.health <= 0 ? muted : m.health < 40 ? amber : i == crew->selected ? mint : pearl;
            if (i == crew->selected)
                rect(h.panels, row.x, row.y, row.w, row.h, mint * .16f);
            fitted(h, m.name + " / " + std::string(crewDutyName(m)), row.x + 6, row.y + 4,
                   row.w - 12, 9, c);
            const std::string state = m.health <= 0   ? "LOST"
                                      : m.travel > 0  ? "MOVING"
                                      : m.health < 20 ? "INCAPACITATED"
                                                      : std::string(crewOrderName(m.order));
            fitted(h,
                   state + " / HP " + std::to_string(int(m.health)) + " FAT " +
                       std::to_string(int(m.fatigue)) + " MOR " + std::to_string(int(m.morale)),
                   row.x + 6, row.y + 17, row.w - 12, 8, ice);
            const auto from = layout.rooms[size_t(m.room)], to = layout.rooms[size_t(m.walkTo)];
            const double blend = m.travelDuration > 0 && m.travel > 0 ? 1 - m.travel / m.travelDuration : 0;
            const double x = glm::mix(from.x + from.w * .5, to.x + to.w * .5, blend) + double(i % 3) * 5 - 5;
            const double y = glm::mix(from.y + 48, to.y + 48, blend);
            rect(h.panels, x, y, 4, 4, c);
        }
        fitted(h, "TARGET " + std::string(crewStationName(crew->station)), rx, p.y + 323, rw, 10,
               mint);
        fitted(h, "TRAIT / B N W E L M RANKS: " + crew->selectedSkill, rx, p.y + 342, rw, 8.5, ice);
        fitted(h,
               "STORES / " + std::to_string(int(crew->spares)) + " SPARES / " +
                   std::to_string(int(crew->medical)) + " MEDICAL",
               p.x + 18, p.y + 365, p.w - 36, 9.5, pearl);
        fitted(h, crew->port, p.x + 18, p.y + 385, p.w - 36, 8.5, crew->connected ? mint : muted);
        fitted(h, crew->recruit, p.x + 18, p.y + 403, p.w - 36, 8.5, ice);
        const bool trading = !crew->sell.empty();
        if (trading)
            fitted(h, crew->sell, p.x + 18, p.y + 421, p.w - 36, 8.5, mint);
        fitted(h, crew->supply, p.x + 18, p.y + (trading ? 439 : 421), p.w - 36, 8.5, ice);
        fitted(h, crew->care, p.x + 18, p.y + (trading ? 457 : 439), p.w - 36, 8.5, ice);
        fitted(h, crew->pending ? "PORT ORDER PENDING" : crew->message, p.x + 18,
               p.y + (trading ? 478 : 459), p.w - 36, 9, amber);
        if (!crew->events.empty() && !trading)
            fitted(h, crew->events.back(), p.x + 18, p.y + 478, p.w - 36, 8.5, trafficInk);
        fitted(h, "CLICK CREW THEN ROOM / UP-DOWN CREW / LEFT-RIGHT ROOM / ENTER ASSIGN", p.x + 18,
               p.y + 502, p.w - 36, 8.3, pearl);
        fitted(h, "R REPAIR / F FIRE / M AID / T REST / V VENT / D RECALL / A AUTO / CTRL1-3 GUN",
               p.x + 18, p.y + 521, p.w - 36, 8.3, ice);
        fitted(h,
               "PORT: H BUY / BRACKETS CYCLE / BACKSPACE SELL / SHIFT-BACKSPACE RELEASE / F2 MARKET",
               p.x + 18, p.y + 540, p.w - 36, 8.3, mint);
        fitted(h, "F8 OR ESC CLOSE / FLIGHT CONTINUES / MANUAL HELM SUSPENDED", p.x + 18, p.y + 559,
               p.w - 36, 8.3, muted);
        return finish(std::move(h));
    }
    const bool weaponUseful=focus.mode==CockpitMode::Combat||docking||(focus.mode==CockpitMode::Surface&&t.explorationActive)||!t.stationStatus.empty()||!t.recoveryStatus.empty();
    if (!t.weapon.empty()&&!modal&&!reconOpen&&weaponUseful) {
        if (!t.stationStatus.empty()) {
            const auto station = weaponStationCard(aspect, flightCommands);
            stationPanel = station;
            card(h, station, mint);
            fitted(h, t.stationStatus, station.x + 12, 612, 292, 8.5, mint);
            fitted(h,
                   t.reliefHelm.empty()
                       ? "HELM REMAINS YOURS UNTIL RELIEF ARRIVES"
                       : "HELM " + t.reliefHelm + " / FIT " + std::to_string(int(t.helmQuality * 100)) + "%",
                   station.x + 12, 626, 292, 8.5, ice);
            fitted(h,
                   t.playerGun < maxWeaponMounts ? (t.helmOrder.empty() ? "CREW / COURSE HOLD" : t.helmOrder)
                                                 : "F10 CANCEL / KEEP FLYING DURING HANDOVER",
                   station.x + 12, 640, 292, 8.5, pearl);
            if (t.playerGun < maxWeaponMounts) {
                traversePanel = CockpitRect{center - 85, 493, 170, 96};
                const auto tint = t.playerGun == 1 ? glm::vec3(.75, .55, 1) : mint;
                const double cx = center, cy = 555, radius = 32;
                const double rad = std::numbers::pi / 180.;
                for (double angle = t.gunMinYaw; angle < t.gunMaxYaw; angle += 5) {
                    const double a = angle * rad, b = std::min(angle + 5, t.gunMaxYaw) * rad;
                    line(h.strokes, cx + std::sin(a) * radius, cy - std::cos(a) * radius,
                         cx + std::sin(b) * radius, cy - std::cos(b) * radius, tint * .65f);
                }
                line(h.strokes, cx, cy + 5, cx, cy - 40, muted * .6f);
                line(h.strokes, cx, cy, cx + std::sin(t.gunYaw * rad) * radius,
                     cy - std::cos(t.gunYaw * rad) * radius, tint);
                const bool stop =
                    t.gunYaw < t.gunMinYaw + 2 || t.gunYaw > t.gunMaxYaw - 2 || std::abs(t.gunPitch) > 68;
                fitted(h, stop ? "TRAVERSE STOP" : "GUN ARC / NOSE UP", center - 80, 501, 160, 8,
                       stop ? amber : tint);
                fitted(h,
                       "AZ " + std::to_string(int(t.gunYaw)) + " / EL " + std::to_string(int(t.gunPitch)),
                       center - 80, 578, 160, 8, tint);
                fitted(h,
                       flightCommands ? "CTRL F1-5: COURSE RANGE CIRCLE EVADE RETREAT"
                                      : "1 COURSE  2 RANGE  3 CIRCLE  4 EVADE  5 RETREAT",
                       station.x + 2, 657, 312, 7, ice);
            }
        }
        const CockpitRect armament =
            flightCommands ? CrewFlightLayout(aspect).weapon : CockpitRect{center - 158, 665, 316, 80};
        const auto danger = glm::vec3(1, .3, .2);
        const bool environmentalWarning = t.explorationActive && t.weatherSeverity > .5;
        card(h, armament, t.incomingFire > .2 ? danger : t.weaponHeat > 80 || environmentalWarning ? amber : mint);
        if (t.explorationActive) {
            for (size_t i = 0; i < t.exploration.size(); ++i)
                fitted(h, t.exploration[i], armament.x + 12, 675 + i * 18, 292, i == 0 ? 9.5 : 8.5,
                       environmentalWarning && i >= 2 ? amber : i == 0 ? pearl : ice);
        } else if (docking) {
            const auto& d = t.docking;
            fitted(h, dockingPhaseName(d.phase), armament.x + 12, 675, 292, 9.5, d.docked ? mint : pearl);
            quantity("BERTH " + flightQuantity(d.range) + " / " + flightQuantity(std::abs(d.closing), true),
                     armament.x + 12, 693, 292, 8.8, ice);
            const double lateral = glm::length(glm::dvec2(d.offset));
            quantity("LATERAL " + flightQuantity(lateral) + (d.closing < -.2 ? " / OPENING" : " / CLOSING"),
                     armament.x + 12, 711, 292, 8.8, lateral < 2 ? mint : ice);
            if (d.phase == DockingPhase::Capture)
                bar(h.panels, armament.x + 12, 734, 292, d.capture, mint);
            else
                fitted(h, d.docked ? "WELCOME ABOARD / F2 SERVICES / ENTER UNDOCK"
                                   : !d.active ? "MANUAL / L SOFT CAPTURE / J AUTO DOCK"
                                              : "ARRIVAL > ALIGN > GLIDE > CAPTURE / B TAKE HELM",
                       armament.x + 12, 729, 292, 8, mint);
        } else {
            fitted(h,
                   t.weapon + (t.weaponBattery ? "" : " / HEAT " + std::to_string(int(t.weaponHeat)) + "%"),
                   armament.x + 12, 675, 292, 9.5, pearl);
            fitted(h, t.weaponStatus, armament.x + 12, 693, 292, 8.8,
                   t.weaponHeat > 80 ? amber : ice);
            if (t.pulseHeat)
                bar(h.panels, armament.x + 12, 705, 292, *t.pulseHeat / 100.,
                    *t.pulseHeat > 80 ? amber : ice);
            const auto target =
                std::find_if(activity.begin(), activity.end(), [](const auto& c) { return c.selected; });
            if (target != activity.end() && isOrbitalDebris(target->body.id))
                fitted(h, "ORBITAL FRAGMENT / J MATCH COURSE", armament.x + 12, 711, 292, 8.8, ice);
            else if (target != activity.end())
                fitted(h,
                       t.systemTarget.empty()
                           ? "TARGET HULL " + std::to_string(int(std::ceil(target->body.hull))) + "% / " +
                                 flightQuantity(target->distance)
                           : t.systemTarget,
                       armament.x + 12, 711, 292, 8.8, target->body.hostile ? danger : ice);
            else
                fitted(h, "H TARGET NEAREST HOSTILE", armament.x + 12, 711, 292, 8.8, muted);
            fitted(
                h,
                t.recoveryStatus.empty()
                    ? (t.combatStatus.empty() ? "F12 SCAN / F11 JOURNAL / CTRL-SPACE MINE" : t.combatStatus)
                    : t.recoveryStatus,
                armament.x + 12, 729, 292, 8.8, t.hostileContacts ? danger : muted);
        }
        if (!t.combatMission.empty())
            fitted(h, t.combatMission, armament.x + 4, 750, 308, 8, mint);
    }
    if (market && market->open) {
        const auto& m = *market;
        const CockpitRect panel{center - std::min(790., w - 60) * .5, 166, std::min(790., w - 60), 574};
        occlude(h,panel);card(h, panel, m.connected ? mint : amber);
        h.hitRegions.push_back(panel);
        label("F2 CLOSE / F5 " +
                  std::string(m.page == MarketPage::Goods         ? "GOODS"
                              : m.page == MarketPage::Routes      ? "TRADE ROUTES"
                              : m.page == MarketPage::Industry    ? "INDUSTRY"
                              : m.page == MarketPage::Jobs        ? "JOBS"
                              : m.page == MarketPage::Outcomes    ? "OUTCOMES"
                              : m.page == MarketPage::Weapons     ? "WEAPONS"
                              : m.page == MarketPage::Crew        ? "CREW MARKET"
                              : m.page == MarketPage::Survey      ? "SURVEY AND EQUIPMENT"
                              : m.page == MarketPage::Journal     ? "SURVEY JOURNAL"
                              : m.page == MarketPage::Expeditions ? "MATERIAL EXPEDITIONS"
                              : m.page == MarketPage::Incidents   ? "INCIDENTS"
                                                                  : "SERVICES") +
                  " / ECON " + std::to_string(m.tick * 5 / 60) + "H",
              panel.x + 18, panel.y + 16, panel.w - 36, mint);
        quantity(m.ready ? m.site : "LOCAL ECONOMY", panel.x + 18, panel.y + 39, panel.w - 36, 14, pearl);
        label(m.page == MarketPage::Incidents ? m.role : m.role + " / " +
                  (m.page == MarketPage::Outcomes || m.page == MarketPage::Journal ? "SAVED REPORT"
                   : m.connected                                                   ? "TRADE LINK"
                                 : "REMOTE / APPROACH AND STOP"),
              panel.x + 18, panel.y + 64, panel.w - 36, m.connected ? mint : amber);
        quantity("CREDITS " + std::to_string(m.credits / 100) + " / CARGO " + std::to_string(m.cargoKg) +
                     "/" + std::to_string(m.capacityKg) + " KG / " + std::to_string(m.cargoLitres) + "/" +
                     std::to_string(m.capacityLitres) + " L" +
                     (m.activeJob.empty() ? "" : " / " + m.activeJob),
                 panel.x + 18, panel.y + 86, panel.w - 36, 10.5, ice);
        if (m.page == MarketPage::Goods) {
            label("GOOD                 STOCK   FREE     BUY CR     SELL CR", panel.x + 18, panel.y + 112,
                  panel.w - 36, muted);
            for (size_t g = 0; g < goodCount; g++) {
                const double y = panel.y + 125 + g * 17;
                if (g == m.selected)
                    rect(h.panels, panel.x + 12, y - 3, panel.w - 24, 16, mint * .18f, .95f);
                fitted(h, m.rows[g], panel.x + 18, y, panel.w - 36, 9.2,
                       g == m.selected ? pearl : ice);
            }
            label("ORDER " + std::to_string(m.quantity) + " KG / UP-DOWN GOODS / LEFT-RIGHT SIZE",
                  panel.x + 18, panel.y + 472, panel.w - 36, mint);
            label("ENTER BUY / BACKSPACE SELL / F3 CONTRACT / F4 TARGET", panel.x + 18, panel.y + 492,
                  panel.w - 36, pearl);
            fitted(h, m.summary.empty() ? m.contract : m.summary, panel.x + 18, panel.y + 512,
                   panel.w - 36, 8.4, amber);
        } else if (m.page == MarketPage::Journal || m.page == MarketPage::Survey ||
                   m.page == MarketPage::Expeditions) {
            const bool journal = m.page == MarketPage::Journal;
            const bool expedition = m.page == MarketPage::Expeditions;
            fitted(h,
                   expedition ? "F4 DESTINATION / SHIFT-F4 COLONY / ENTER ACTION / SHIFT-F11 EXIT PROBE"
                   : journal  ? "UP-DOWN RECORDS / F4 VISIT / ENTER SELL DATA"
                              : "UP-DOWN EQUIPMENT OR CONTRACT / LEFT-RIGHT MOUNT",
                   panel.x + 18, panel.y + 112, panel.w - 36, 9, mint);
            for (size_t i = 0; i < m.detail.size(); ++i)
                fitted(h, m.detail[i], panel.x + 18, panel.y + 140 + i * (journal ? 25 : 18),
                       panel.w - 36, i == 0 ? 9.4 : 8.4, i % 2 ? ice : pearl);
            fitted(h, m.summary, panel.x + 18, panel.y + 472, panel.w - 36, 8.4, mint);
            fitted(h,
                   expedition ? "ENTER DIAGNOSE, OPEN LAB OR FIT REPAIR / BACKSPACE CANCEL"
                   : journal ? "F11 CLOSE / F12 SCAN / CTRL-SPACE EXTRACT"
                           : "ENTER FIT OR ACCEPT-CLAIM / BACKSPACE CANCEL / F4 TARGET",
                   panel.x + 18, panel.y + 492, panel.w - 36, 9, pearl);
            fitted(h, expedition ? "SEALED CORE / SAME SOURCE GEOMETRY / PARKED SHIP / SAVED COLONY OUTCOME"
                                         : "SCAN AT ANY SCALE / EXTRACTION AT FULL SIZE / DEPLETION PERSISTS",
                   panel.x + 18, panel.y + 512, panel.w - 36, 8.4, amber);
        } else if (m.page == MarketPage::Crew) {
            fitted(h, m.summary, panel.x + 18, panel.y + 112, panel.w - 36, 9.5, mint);
            const size_t first = m.selected >= 12 ? m.selected - 11 : 0;
            for (size_t row = first; row < std::min(first + 12, m.crewRows.size()); ++row) {
                const auto& offer = m.crewRows[row];
                const auto& person = offer.person;
                const double y = panel.y + 135 + (row - first) * 25;
                if (row == m.selected)
                    rect(h.panels, panel.x + 12, y - 3, panel.w - 24, 24, mint * .16f);
                const auto skill = std::max_element(person.experience.begin(), person.experience.end());
                const auto specialty = CrewStation(skill - person.experience.begin());
                fitted(h,
                       std::string(offer.buy ? "BUY  " : "SELL ") + person.name + " / " +
                           std::string(crewStationName(specialty)) + " / " +
                           crewMoney(offer.buy ? offer.quote.buyCents : offer.quote.sellCents) + " CR",
                       panel.x + 18, y, panel.w - 36, 9, row == m.selected ? pearl : ice);
                fitted(h,
                       "HP " + std::to_string(int(person.health)) + " / FAT " +
                           std::to_string(int(person.fatigue)) + " / MOR " +
                           std::to_string(int(person.morale)) + " / " + offer.quote.status,
                       panel.x + 18, y + 12, panel.w - 36, 8, row == m.selected ? mint : muted);
            }
            fitted(h, "ENTER BUY PORT CREW / BACKSPACE SELL ABOARD CREW", panel.x + 18,
                   panel.y + 461, panel.w - 36, 9, pearl);
            fitted(h, "SHIFT-BACKSPACE RELEASE / UP-DOWN SELECT / F8 CREW STATIONS", panel.x + 18,
                   panel.y + 480, panel.w - 36, 8.5, ice);
        } else if (m.page == MarketPage::Incidents) {
            for (size_t i = 0; i < std::min<size_t>(6, m.detail.size()); ++i)
                fitted(h, m.detail[i], panel.x + 18, panel.y + 125 + i * 25, panel.w - 36,
                       i == 0 ? 11 : 9.4, i == 0 ? pearl : i == 2 ? amber : ice);
            if (m.detail.size() >= 9) {
                label("RESPONSE / LEFT-RIGHT SELECT", panel.x + 18, panel.y + 286, panel.w - 36, mint);
                const std::array<std::string_view, 3> choices{"RESCUE", "RELAY", "RELIEF"};
                for (size_t i = 0; i < choices.size(); ++i) {
                    const double y = panel.y + 310 + i * 30;
                    const bool selected = m.summary == choices[i];
                    if (selected)
                        rect(h.panels, panel.x + 12, y - 5, panel.w - 24, 23, mint * .15f, .95f);
                    fitted(h, m.detail[6 + i], panel.x + 18, y, panel.w - 36, 9.4, selected ? pearl : muted);
                }
                fitted(h, m.plan, panel.x + 18, panel.y + 413, panel.w - 36, 9, pearl);
                fitted(h, m.planTiming, panel.x + 18, panel.y + 432, panel.w - 36, 8.8, amber);
            }
            label("UP-DOWN INCIDENT / F4 TARGET / J APPROACH / ENTER ACTION", panel.x + 18, panel.y + 492,
                  panel.w - 36, pearl);
            label("I JOBS / F5 OTHER MARKET PAGES / F2 CLOSE", panel.x + 18, panel.y + 512,
                  panel.w - 36, ice);
        } else if (m.page == MarketPage::Outcomes) {
            label("DELIVERY REPORT / UP-DOWN HISTORY / BEFORE / LAST OBSERVED", panel.x + 18, panel.y + 112,
                  panel.w - 36, mint);
            for (size_t row = 0; row < m.detail.size(); ++row)
                fitted(h, m.detail[row], panel.x + 18, panel.y + 142 + row * 25, panel.w - 36,
                       row == 0 ? 10 : 8.8, row < 2 ? pearl : ice);
            fitted(h, m.summary, panel.x + 18, panel.y + 472, panel.w - 36, 8.4, mint);
            label("F4 TARGET REPORT PORT / J APPROACH / F5 MARKET PAGES", panel.x + 18, panel.y + 492,
                  panel.w - 36, pearl);
            label("PAYMENTS ARE FINAL / HEALTH AND PRODUCTION RECOVER OVER TIME", panel.x + 18, panel.y + 512,
                  panel.w - 36, amber);
        } else {
            const bool industry = m.page == MarketPage::Industry;
            const bool jobs = m.page == MarketPage::Jobs;
            const bool services = m.page == MarketPage::Services;
            const bool weapons = m.page == MarketPage::Weapons;
            label(weapons
                      ? std::string(weaponMountName(m.weaponMount)) + " / LEFT-RIGHT MOUNT / UP-DOWN WEAPON"
                  : services ? "PORT SERVICES / REPAIRS / HOLD / RESCUE HANDOVER"
                  : jobs     ? "FUNDED JOBS / UP-DOWN SELECT / ONE HAUL + ONE SECURITY"
                  : industry ? "EXISTING FACILITY REFITS / DELIVER CARGO TO BUILD"
                             : "AFFORDABLE LOCAL OPPORTUNITIES / UP-DOWN SELECT",
                  panel.x + 18, panel.y + 112, panel.w - 36, mint);
            const size_t group = industry || services || weapons ? 5 : 2;
            const double rowHeight = industry || services || weapons ? 21 : 13;
            // Keep complete offers together and follow the selected offer.
            // Longer job boards must never run into the plan or action footer.
            const double bodyEnd = jobs ? 435 : 454;
            const size_t capacity = (size_t((bodyEnd - 150) / rowHeight) + 1) / group * group;
            const size_t first = m.detail.size() > capacity
                                     ? std::min(m.selected * group / capacity * capacity,
                                                (m.detail.size() - 1) / capacity * capacity)
                                     : 0;
            const size_t end = std::min(m.detail.size(), first + capacity);
            for (size_t row = first; row < end; row++) {
                const auto y = panel.y + 140 + (row - first) * rowHeight;
                const bool selected = row / group == m.selected;
                if (selected && row % group == 0)
                    rect(h.panels, panel.x + 12, y - 4, panel.w - 24, group * rowHeight - 3, mint * .15f,
                         .95f);
                fitted(h, m.detail[row], panel.x + 18, y, panel.w - 36,
                       row % group == 0 ? 9.4 : 8.4, selected ? (row % group == 0 ? pearl : ice) : muted);
            }
            if (m.detail.size() > capacity)
                fitted(h, "ROWS " + std::to_string(first + 1) + "-" + std::to_string(end) + " / " +
                              std::to_string(m.detail.size()) + " / UP-DOWN SELECT",
                       panel.x + 18, panel.y + bodyEnd, panel.w - 36, 8, muted);
            if (jobs) {
                fitted(h, m.plan, panel.x + 18, panel.y + 451, panel.w - 36, 8.4, ice);
                fitted(h, m.planTiming, panel.x + 18, panel.y + 465, panel.w - 36, 8.4, amber);
            }
            fitted(h, m.summary, panel.x + 18, panel.y + (jobs ? 480 : 472), panel.w - 36, 8.4,
                   mint);
            label(weapons    ? "ENTER FIT / UPGRADE / BACKSPACE SELL SELECTED MOUNT / F4 PORT"
                  : services ? "ENTER SERVICE OR DISEMBARK / F4 TARGET PORT / J APPROACH"
                  : jobs     ? "ENTER ACCEPT / DELIVER / CLAIM / F4 TARGET / J APPROACH"
                  : industry ? "ENTER FUND / DELIVER PARTS / BACKSPACE COLLECT DIVIDENDS"
                             : "ENTER TARGET SOURCE / F4 TARGET BUYER / J APPROACH",
                  panel.x + 18, panel.y + 492, panel.w - 36, pearl);
            label(weapons    ? "SPACE LINKED FIRE / F9 CTRL1-3 ASSIGNS GUNNERS / INDEPENDENT HEAT"
                  : services ? "REPAIRS USE PORT STOCK / CARGO UPGRADES KEEP SEALED CONSIGNMENTS"
                  : jobs     ? "I INCIDENTS / BACKSPACE CANCEL HAUL OR SECURITY / SHIFT-F4 HAUL SOURCE"
                  : industry ? "DELETE CANCEL PROJECT / UNUSED FEES AND PARTS RETURNED"
                             : "QUOTES INCLUDE SPREAD / MARGINAL PRICES / NO PRICE GUARANTEE",
                  panel.x + 18, panel.y + 512, panel.w - 36, amber);
        }
        fitted(h, m.bottleneck, panel.x + 18, panel.y + 532, panel.w - 36, 8.4, muted);
        fitted(h, m.message, panel.x + 18, panel.y + 551, panel.w - 36, 8.4, ice);
        return finish(std::move(h));
    }
    if (help) {
        const CockpitRect guide{center-std::min(1040.,w-48)*.5,112,std::min(1040.,w-48),634};
        occlude(h,guide);card(h, guide, mint);
        fitted(h, "FLIGHT CONTROLS", guide.x + 22, guide.y + 22, guide.w - 44, 17.5, pearl);
        const std::pair<const char*, const char*> controls[]{
            {"W / S", "THROTTLE"},
            {"A / D   Q / E", "STRAFE / LIFT"},
            {"C / MOUSE / ARROWS", "HELM / LOOK"},
            {"U / O  ALT+A / D", "ROLL"},
            {"B / X", "RECOVER / ASSIST"},
            {"Y / SHIFT-Y", "PLANETS / MOONS"},
            {"CTRL-Y", "LIFE-DETECTED WORLDS"},
            {"K / I / J", "TRAFFIC / HAIL / APPROACH-DOCK"},
            {"SHIFT-K / SHIFT-I", "SURFACE PORTS / ARRIVAL CONTACTS"},
            {"ALT-K / ALT-SHIFT-K", "TRACK DEBRIS / LANDABLE FRAGMENTS"},
            {"L / SHIFT-L / G / ENTER", "LAND / ACCEPT RISK / GEAR / LAUNCH"},
            {"N / CTRL-N", "WARP / WARP TO TARGET"},
            {"Z / , / .", "SHRINK / SMALLER / LARGER"},
            {"SHIFT-Z / V", "FULL SIZE / EXPLORER"},
            {"F6 / TAB", "TRAFFIC / HUD"},
            {"CTRL-TAB", "CONTACT CATALOGUES / FLIGHT MESSAGES"},
            {"F7", "COCKPIT MOTION"},
            {"F5 / CTRL-F5 / ALT-F5", "SURFACE MAP / NAV ASSESS / ENGINEER WATCH"},
            {"ON SURFACE MAP", "SHIFT-F5 NEXT / F4 ROUTE / HOLD 0 FIELD WORK"},
            {"F8", "CREW / STATIONS / DAMAGE CONTROL"},
            {"F9", "IN-FLIGHT CREW ORDERS / HELM LIVE"},
            {"F10", "SWAP WITH READY GUNNER / RETURN TO HELM"},
            {"F11 / F12", "JOURNAL / SCAN / CTRL-F12 SEAM / SHIFT-J ENTRY"},
            {"ALT-F12", "CYCLE WORLD GEOLOGY LANDMARKS"},
            {"CTRL+SPACE", "OPERATE FITTED MINING HEAD"},
            {"SPACE", "FIRE FITTED WEAPON OR PROBE LAUNCHER"},
            {"CTRL-P / ALT-P", "LAUNCH PROBE / LIVE CAMERA"},
            {"SHIFT-P / CTRL-J", "PROBE BAY / EXCAVATION ENTRANCE"},
            {"ALT-[ / ]", "PROBE DEPTH PRESET / CAMERA [ ] DEPTH"},
            {"PROBE CAMERA", "P PAUSE / - + FEED / R ABORT / ENTER CHARGE"},
            {"IN CAVERN", "SHIFT-F12 DEPOSIT / SHRINK / F12 / CTRL-F12 MARK"},
            {"H / 6 / 7 / 8", "HOSTILE / GUNNERS HOLD / DEFEND / FOCUS"},
            {"F2 / MARKET F5", "MARKET / JOBS / SERVICES / DELIVERY REPORTS"},
            {"F3", "EXPEDITION PLANNER / CONTRACT / PREPARE / TRACK"},
            {"F1 / ESC", "CLOSE GUIDE / RELEASE HELM"}};
        for (size_t i = 0; i < std::size(controls); ++i) {
            const auto y=guide.y+65+i*15.;
            fitted(h,controls[i].first,guide.x+22,y,guide.w*.30-22,12,ice);
            fitted(h,controls[i].second,guide.x+guide.w*.34,y,guide.w*.64-22,12,pearl);
        }
        return finish(std::move(h));
    }

    if (flightCommands) {
        const CrewFlightLayout layout(aspect);
        const auto p = layout.panel;
        crewCommandPanel = p;
        occlude(h,p);card(h, p, mint);
        fitted(h, "CREW / F9 ORDERS", p.x + 12, p.y + 12, p.w - 94, 12, pearl);
        card(h, layout.automatic, crew->autoDamageControl ? mint : amber);
        fitted(h, crew->autoDamageControl ? "AUTO ON" : "AUTO OFF", layout.automatic.x + 5,
               layout.automatic.y + 5, layout.automatic.w - 10, 7, crew->autoDamageControl ? mint : amber);
        fitted(h, crew->alert, p.x + 12, p.y + 33, p.w - 24, 8.2,
               crew->summary.find("EMERGENCY") != std::string::npos ? amber : ice);
        fitted(h,
               "CREW " +
                   std::to_string(std::count_if(crew->members.begin(), crew->members.end(),
                                                [](const auto& m) { return m.health > 0; })) +
                   "/8 / SPARES " + std::to_string(int(crew->spares)) + " / MED " +
                   std::to_string(int(crew->medical)),
               p.x + 12, p.y + 50, p.w - 24, 8.2, mint);
        for (size_t i = 0; i < crew->members.size() && i < maxShipCrew; ++i) {
            const auto& m = crew->members[i];
            const auto r = layout.roster[i];
            const auto task = i < crew->tasks.size() ? crew->tasks[i] : CrewTaskView{};
            const auto colour = m.health <= 0         ? muted
                                : task.blocked        ? amber
                                : i == crew->selected ? mint
                                                      : ice;
            if (i == crew->selected)
                rect(h.panels, r.x, r.y, r.w, r.h, mint * .16f);
            fitted(h,
                   std::to_string(i + 1) + " " + m.name + " / " +
                       std::string(m.id == crew->reliefHelmId ? "RELIEF HELM" : crewDutyName(m)),
                   r.x + 4, r.y + 3, r.w - 8, 8.6, colour);
            fitted(h,
                   task.status + " / HP " + std::to_string(int(m.health)) + " FAT " +
                       std::to_string(int(m.fatigue)),
                   r.x + 4, r.y + 15, r.w - 8, 7.5, colour);
        }
        constexpr std::array<std::string_view, 3> mountNames{"NOSE", "PORT", "STBD"};
        for (unsigned i = 0; i < maxWeaponMounts; ++i) {
            const auto r = layout.guns[i];
            const auto colour = !crew->weaponLevels[i] ? muted : crew->gunReadiness[i] > 0 ? mint : amber;
            card(h, r, colour);
            fitted(h,
                   std::to_string(i + 1) + " " + std::string(mountNames[i]) + " " +
                       std::string(weaponGrade(crew->weaponLevels[i])),
                   r.x + 4, r.y + 5, r.w - 8, 7, colour);
        }
        const auto detail =
            crew->selected < crew->tasks.size() ? crew->tasks[crew->selected] : CrewTaskView{};
        std::string selectedDetail =
            "ROOM " + std::string(crewStationName(crew->station)) + " / " + detail.detail;
        if (crew->selected < crew->members.size()) {
            const auto& m = crew->members[crew->selected];
            if (m.duty == CrewStation::Weapons && m.weaponMount < maxWeaponMounts)
                selectedDetail =
                    std::string(weaponMountName(m.weaponMount)) + " / " + crew->gunStatus[m.weaponMount];
        }
        fitted(h, selectedDetail, p.x + 12, p.y + 307, p.w - 24, 8, detail.blocked ? amber : mint);
        for (size_t i = 0; i < crewRoomCount; ++i) {
            const auto r = layout.rooms[i];
            const auto& room = crew->rooms[i];
            const auto colour = room.fire > 1 || room.oxygen < 25 ? amber
                                : crew->station == CrewStation(i) ? mint
                                                                  : ice;
            card(h, r, colour);
            fitted(h, std::to_string(i + 1) + " " + std::string(crewStationName(CrewStation(i))),
                   r.x + 4, r.y + 6, r.w - 8, 7.4, colour);
        }
        constexpr std::array<std::string_view, 8> labels{"ASSIGN",        "REPAIR",   "FIGHT FIRE",
                                                         "FIRST AID",     "REST",     "RETURN TO DUTY",
                                                         "BEST STATIONS", "EMERGENCY"};
        for (size_t i = 0; i < labels.size(); ++i) {
            const auto r = layout.actions[i];
            card(h, r, i == 7 ? amber : ice);
            fitted(h, labels[i], r.x + 5, r.y + 8, r.w - 10, 8.2, i == 7 ? amber : pearl);
        }
        fitted(h, crew->message.empty() ? "MANUAL HELM AND WEAPONS REMAIN LIVE" : crew->message,
               p.x + 12, p.y + 529, p.w - 24, 7.8, amber);
        fitted(h, "1-8 CREW / ALT 1-7 ROOM / C CURSOR", p.x + 12, p.y + 547, p.w - 24, 7.6, ice);
        fitted(h, "CTRL1-3 MAN GUN / F10 SWAP / F9 CLOSE", p.x + 12, p.y + 560, p.w - 24, 7.4,
               muted);
    }
    if(!modal&&!flightCommands&&!reconOpen&&!catalogues)drawContactSummary(h,aspect,contacts,activity);
    if (catalogues && !flightCommands && !activity.empty()) {
        card(h, l.traffic, trafficInk);
        label("PORTS / TRAFFIC", 38, 188, 160, trafficInk);
        h.contactHits.push_back({{24,176,274,28},{},{},true,false});
        label("K / I", 238, 188, 56, muted);
        const auto rows = cockpitActivityRows(activity);
        for (size_t row = 0; row < rows.size(); ++row) {
            const auto& c = activity[rows[row]];
            const double y = 211 + row * 34;
            h.contactHits.push_back({{28,y-5,266,33},{},c.body.id,false,false});
            if (c.selected)
                rect(h.panels, 28, y - 5, 266, 33, amber * .16f, .8f);
            fitted(h, (c.selected ? "+ " : "  ") + c.body.name, 36, y, 246, 10.5,
                   c.body.hostile ? glm::vec3(1, .3, .2)
                   : c.selected   ? amber
                   : c.occluded   ? muted
                                  : pearl);
            auto status = c.body.economicBrief.empty() ? c.body.status.substr(0, c.body.status.find(" / "))
                                                       : c.body.economicBrief;
            if (c.occluded)
                status = "OCCULTED";
            quantity(flightQuantity(c.distance) + " / " + status, 52, y + 16, 234, 9,
                     c.selected || c.body.economicAlert > .5f ? amber : muted);
        }
        const auto fragments = std::count_if(activity.begin(), activity.end(),
                                             [](const auto& c) { return isOrbitalDebris(c.body.id); });
        label(fragments ? std::to_string(fragments) + " DEBRIS / ALT-K SCAN"
                        : std::to_string(activity.size()) + " CONTACTS / K SELECT",
              38, 421, 246, fragments ? ice : muted);
        const ActivityContact* report = nullptr;
        for (const auto& contact : activity)
            if (contact.selected && !contact.body.economicBrief.empty()) {
                report = &contact;
                break;
            }
        if (report) {
            const auto& body = report->body;
            const auto colour = body.economicAlert > .9f   ? glm::vec3(1, .42, .25)
                                : body.economicAlert > .5f ? amber
                                                           : mint;
            economicPanel = CockpitRect{24, 450, 274, 166};
            card(h, *economicPanel, colour);
            h.hitRegions.push_back(*economicPanel);
            label(body.kind == ActivityKind::Freighter ? "FREIGHT MANIFEST" : "PORT STATUS", 38, 462, 246,
                  colour);
            fitted(h, body.name, 38, 482, 246, 11, pearl);
            fitted(h, body.economicBrief, 38, 502, 246, 9.4, colour);
            for (size_t i = 0; i < body.economicReport.size(); ++i)
                fitted(h, body.economicReport[i], 38, 521 + i * 18, 246, 8.4, ice);
            label(body.kind == ActivityKind::Freighter ? "I HAIL / J MATCH COURSE"
                                                       : "I HAIL / J APPROACH / F2 MARKET",
                  38, 598, 246, muted);
        }
    }
    if (catalogues && !contacts.empty()) {
        card(h, l.celestial, ice);
        const auto moons =
            std::count_if(contacts.begin(), contacts.end(), [](const auto& c) { return c.moon; });
        const auto planets = std::count_if(contacts.begin(), contacts.end(), [](const auto& c) {
            return c.address.kind() == Kind::Planet && !c.moon;
        });
        label("SYSTEM " + std::to_string(planets) + "P / " + std::to_string(moons) + "M", w - 296, 188, 180,
              ice);
        const auto rows = cockpitCelestialRows(contacts);
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& c = contacts[rows[i]];
            const double x = w - 296, y = 211 + i * 34;
            h.contactHits.push_back({{w-306,y-5,278,33},c.address,{},false,false});
            const bool inhabited = hasSurfaceLife(c);
            if (c.selected)
                rect(h.panels, w - 306, y - 5, 278, 33, amber * .16f, .8f);
            else if (inhabited)
                rect(h.panels, w - 306, y - 5, 278, 33, mint * .10f, .9f);
            const auto color = c.selected ? amber : inhabited ? mint : c.occluded ? muted : pearl;
            fitted(h, (c.selected ? "+ " : "  ") + c.label, x, y, inhabited ? 207 : 258, 10.5,
                   color);
            if (inhabited) {
                rect(h.panels, x + 214, y - 1, 44, 13, mint * .20f);
                fitted(h, "LIFE", x + 219, y + 1, 37, 9, mint);
            }
            const auto direction = c.occluded ? "HIDDEN" : c.localDirection.z < 0 ? "AHEAD" : "AFT";
            const auto status =
                inhabited ? population(c.population) + " PEOPLE" + (c.occluded ? " / HIDDEN" : "")
                : c.life == LifeDetection::Scanning                                 ? "SCANNING"
                : c.life == LifeDetection::None && c.address.kind() == Kind::Planet ? "NO SURFACE LIFE"
                                                                                    : direction;
            quantity(flightQuantity(c.distanceMetres) + " / " + status, x + 17, y + 16, 243, 9,
                     inhabited    ? mint
                     : c.selected ? amber
                                  : muted);
        }
        label("CTRL-Y LIFE / Y WORLDS", w - 296, 490, 258, living ? mint : muted);
        const auto badge=cockpitLifeBadge(aspect,true);
        if(living){rect(h.panels,badge.x,badge.y,badge.w,badge.h,mint*.14f);
            fitted(h,std::to_string(living)+" LIFE",badge.x+4,badge.y+4,badge.w-8,9,mint);
            h.contactHits.push_back({badge,{},{},false,true});}

    }

    if(reconOpen)h.hitRegions.push_back(surfaceReconPanel(aspect));
    if(catalogues&&!flightCommands){
        const double detailWidth=w>=1200?std::clamp(w-656.,240.,650.):w-48;
        h.hitRegions.push_back(computerPanel(aspect,computerDetails(alerts,t,crew,detailWidth).size()));
    }
    // Readable world labels reserve rectangles, avoid all fixed panels and the
    // reticle, and cap at three secondary labels. Geometry and picking stay full.
    if (!t.systemTarget.empty() && glm::length(t.localSystemTarget) > 1e-12 && !(market && market->open) &&
        !help) {
        const auto marker =
            cockpitTargetMarker(glm::inverse(headInShip) * t.localSystemTarget, aspect, projectionScale);
        if (!marker.offscreen && !marker.aft)
            diamond(h.strokes, marker.point, 7, amber);
    }
    const CockpitRect aim{aimPoint.x - 42, aimPoint.y - 27, 84, 54};
    std::optional<CockpitTargetMarker> missileMarker;
    CockpitRect missileBounds{};
    if (missileAlert) {
        const auto marker = cockpitTargetMarker(eyeFromShip * t.localMissile, aspect, projectionScale);
        missileBounds = {marker.point.x - 16, marker.point.y - 16, 32, 32};
        if (std::none_of(h.hitRegions.begin(), h.hitRegions.end(),
                         [&](const auto& panel) { return panel.overlaps(missileBounds); }) &&
            std::none_of(h.textRuns.begin(), h.textRuns.end(),
                         [&](const auto& run) { return run.bounds.overlaps(missileBounds); }))
            missileMarker = marker;
    }
    auto worldLabel = [&](glm::dvec2 anchor, std::string_view name, std::string_view range, glm::vec3 color) {
        const double width = std::min(184., l.windscreen.w - 12);
        for (int placement : {0, 1, 2, 3}) {
            const int side = placement == 1 ? -1 : 1;
            CockpitRect r{side > 0 ? anchor.x + 18 : anchor.x - width - 18,
                          anchor.y + (placement == 2   ? 32
                                      : placement == 3 ? -68
                                                       : -16),
                          width, t.contactsExpanded?39.:23.};
            r.x = std::clamp(r.x, l.windscreen.x, l.windscreen.x + l.windscreen.w - width);
            r.y = std::clamp(r.y, l.windscreen.y, l.windscreen.y + l.windscreen.h - r.h);
            if (r.overlaps(aim) || (missileMarker && r.overlaps(missileBounds)) ||
                std::any_of(h.hitRegions.begin(),h.hitRegions.end(),[&](const auto& panel){return r.overlaps(panel);}) ||
                std::any_of(h.textRuns.begin(),h.textRuns.end(),[&](const auto& run){return r.overlaps(run.bounds);}) ||
                (economicPanel && r.overlaps(*economicPanel)) ||
                (crewCommandPanel && r.overlaps(*crewCommandPanel)) ||
                (stationPanel && r.overlaps(*stationPanel)) ||
                (traversePanel && r.overlaps(*traversePanel)) ||
                std::any_of(h.worldLabels.begin(), h.worldLabels.end(),
                            [&](auto other) { return r.overlaps(other); }))
                continue;
            h.worldLabels.push_back(r);
            rect(h.panels, r.x, r.y, r.w, r.h, ink, .94f);
            line(h.strokes, anchor.x, anchor.y, side > 0 ? r.x : r.x + r.w, r.y + 18, color * .5f);
            fitted(h, name, r.x + 7, r.y + 6, r.w - 14, 9, color);
            if(t.contactsExpanded)quantity(range, r.x + 7, r.y + 22, r.w - 14, 9, muted);
            return true;
        }
        return false;
    };
    if (t.targetVisible) {
        const auto marker = cockpitTargetMarker(eyeFromShip * (t.warpDestination ? t.localWarpDestination : t.localTarget),
                                                 aspect, projectionScale);
        const auto p = marker.point;
        const bool footprint = t.landing.body && !t.landed;
        const auto tint = footprint ? mint : ice;
        if (marker.offscreen) {
            const auto d = marker.direction, across = glm::dvec2(-d.y, d.x);
            line(h.strokes, p.x, p.y, p.x - d.x * 12 + across.x * 6, p.y - d.y * 12 + across.y * 6, tint);
            line(h.strokes, p.x, p.y, p.x - d.x * 12 - across.x * 6, p.y - d.y * 12 - across.y * 6, tint);
        } else {
            for (int side : {-1, 1}) {
                line(h.strokes, p.x + side * 10, p.y - 12, p.x + side * 17, p.y - 12, tint);
                line(h.strokes, p.x + side * 17, p.y - 12, p.x + side * 17, p.y + 12, tint);
                line(h.strokes, p.x + side * 17, p.y + 12, p.x + side * 10, p.y + 12, tint);
            }
        }
        if (!footprint) {
            const auto lifeTarget = std::find_if(contacts.begin(), contacts.end(),
                                                 [](const auto& c) { return c.selected && hasSurfaceLife(c); });
            worldLabel(p,
                       t.warpDestination ? (t.warpReveal > .05 ? "DESTINATION / RESOLVING" : "WARP DESTINATION")
                       : !t.targetLabel.empty() ? t.targetLabel
                       : lifeTarget != contacts.end() ? (marker.aft ? "LIFE DETECTED / AFT" : "LIFE DETECTED")
                       : marker.aft                 ? "TARGET AFT"
                                                    : "TARGET",
                       flightQuantity(t.range), t.targetLabel.empty()&&lifeTarget != contacts.end() ? mint : ice);
        }
    }
    if (missileMarker) {
        const auto& marker = *missileMarker;
        const auto p = marker.point;
        constexpr glm::vec3 alert(1, .42f, .23f);
        if (marker.offscreen) {
            const auto d = marker.direction, across = glm::dvec2(-d.y, d.x);
            for (double side : {-1., 1.}) {
                const auto tail = p - d * 13. + across * (side * 7.);
                line(h.strokes, p.x, p.y, tail.x, tail.y, alert);
            }
        } else {
            diamond(h.strokes, p, 16., alert);
        }
    }
    for (int side : {-1, 1}) {
        line(h.strokes, aimPoint.x + side * 9, aimPoint.y, aimPoint.x + side * 23, aimPoint.y, ice);
        line(h.strokes, aimPoint.x + side * 23, aimPoint.y, aimPoint.x + side * 23, aimPoint.y + 4, ice);
    }
    rect(h.panels, aimPoint.x - 1, aimPoint.y - 1, 2, 2, pearl);
    const auto motion = eyeFromShip * (t.relativeMotion ? t.localRelativeVelocity : t.localVelocity);
    for (int sign : {1, -1}) {
        const auto d = motion * double(sign);
        if (glm::length(d) < 1e-15 || -d.z < glm::length(d) * .12)
            continue;
        const glm::dvec2 p{center + d.x / -d.z * 450 / .7002075 * projectionScale,
                           450 - d.y / -d.z * 450 / .7002075 * projectionScale};
        if (!l.windscreen.contains(p))
            continue;
        const auto colour = sign > 0 ? mint : amber;
        for (int i = 0; i < 24; i++) {
            const double a = i * 6.283185307 / 24, b = (i + 1) * 6.283185307 / 24;
            line(h.strokes, p.x + std::cos(a) * 7, p.y + std::sin(a) * 7, p.x + std::cos(b) * 7,
                 p.y + std::sin(b) * 7, colour);
        }
        if (sign > 0) {
            line(h.strokes, p.x - 16, p.y, p.x - 9, p.y, colour);
            line(h.strokes, p.x + 9, p.y, p.x + 16, p.y, colour);
        } else {
            line(h.strokes, p.x - 5, p.y - 5, p.x + 5, p.y + 5, colour);
            line(h.strokes, p.x - 5, p.y + 5, p.x + 5, p.y - 5, colour);
        }
    }
    size_t secondary = 0;
    if (catalogues && t.phase != "CAVERN EXPLORATION") {
        auto marker = [&](glm::dvec3 d, std::string_view name, double range, glm::vec3 color) {
            if (d.z >= -.01 || secondary >= 3)
                return;
            const glm::dvec2 p{center + d.x / -d.z * 450 / .7002075 * projectionScale,
                               450 - d.y / -d.z * 450 / .7002075 * projectionScale};
            if (!l.windscreen.contains(p) || aim.contains(p))
                return;
            if (worldLabel(p, name, flightQuantity(range), color)) {
                diamond(h.strokes, p, 4, color);
                ++secondary;
            }
        };
        for (const auto& c : contacts)
            if (!c.selected && !c.occluded && hasSurfaceLife(c))
                marker(c.localDirection, "LIFE / " + c.label, c.distanceMetres, mint);
        for (const auto& c : activity)
            if (!c.selected && !c.occluded && c.body.kind == ActivityKind::Harbour && c.distance < 2e7)
                marker(c.localDirection, c.body.name, c.distance,
                       c.body.economicAlert > .5f ? amber : trafficInk);
        for (const auto& c : contacts)
            if (!c.selected && !c.occluded && !hasSurfaceLife(c) && c.angularRadius < .008)
                marker(c.localDirection, c.label, c.distanceMetres, ice);
    }
    if (surface) {
        // Local terrain normal drives the horizon; it is unrelated to galaxy up.
        const auto headUp = eyeFromShip * t.localUp;
        const double viewRoll =
            t.roll + std::atan2(headUp.x, headUp.y) - std::atan2(t.localUp.x, t.localUp.y);
        const double viewPitch = t.pitch + std::asin(std::clamp(-headUp.z, -1., 1.)) -
                                 std::asin(std::clamp(-t.localUp.z, -1., 1.));
        const double cs = std::cos(viewRoll), sn = std::sin(viewRoll);
        for (int pitch : {-20, -10, 0, 10, 20}) {
            const double y = (viewPitch - pitch * .01745329252) * 220;
            if (std::abs(y) > 140)
                continue;
            for (int side : {-1, 1}) {
                const double a = side * (pitch ? 57. : 46.), b = side * (pitch ? 83. : 108.);
                line(h.strokes, center + a * cs + y * sn, 450 - a * sn + y * cs, center + b * cs + y * sn,
                     450 - b * sn + y * cs, pitch ? muted * .5f : mint * .7f);
            }
        }
    }
    if(reconOpen)drawSurfaceRecon(h,*t.recon,aspect);
    return finish(std::move(h));
}
} // namespace infinity
