#include "Desert.hpp"
#include <cmath>
#include <algorithm>
#include <numbers>
namespace desert {
namespace fr=infinity::fractal;
Course::Course(uint64_t value,RaceFormat kind):seed(value),phase(infinity::Random{value}.unit(91)*6.28318530718),format(kind) {
    if(format==RaceFormat::Circuit){glm::dvec2 previous=fr::fr_circuit(0,float(phase),uint32_t(seed&0xffffff));lapLength=0;
        for(int i=1;i<=512;++i){glm::dvec2 point=fr::fr_circuit(float(i*2*std::numbers::pi/512),float(phase),uint32_t(seed&0xffffff));lapLength+=glm::length(point-previous);arc.push_back(lapLength);previous=point;}
        auto world=field();for(int gate=0;gate<circuitGates;++gate)for(int side:{-1,1})markers.emplace_back(fr::fr_checkpoint_post(gate,side,world),0);
    }
}
double Course::gateInterval() const{return format==RaceFormat::Circuit?lapLength/circuitGates:gateSpacing;}
int Course::markerCount() const{return format==RaceFormat::Circuit?circuitGates:format==RaceFormat::Endless?0:gateCount;}
glm::dvec2 Course::routePoint(double s,double lane) const {
    auto world=field();glm::dvec2 p=fr::fr_route_point(float(s),world);
    if(format==RaceFormat::Stage)p.x+=lane;else {double h=heading(s);p+=glm::dvec2(std::cos(h),-std::sin(h))*lane;}return p;
}
double Course::center(double s) const{return routePoint(s).x;}
double Course::heading(double s) const{return fr::fr_route_heading(float(s),field());}
glm::dvec3 Course::point(double s,double lane) const{auto p=routePoint(s,lane);return {p.x,height(p.x,p.y),p.y};}
double Course::project(glm::dvec2 p) const {
    if(format!=RaceFormat::Circuit)return p.y;auto r=fr::fr_circuit_radii(uint32_t(seed&0xffffff));
    double theta=std::atan2(p.x/r.x,1-p.y/r.y);if(theta<0)theta+=2*std::numbers::pi;return theta/(2*std::numbers::pi)*lapLength;
}
double Course::distanceAlong(double s) const {
    if(s<=0)return 0;if(format==RaceFormat::Stage)return s;
    if(format==RaceFormat::Circuit){double laps=std::floor(s/lapLength),index=(s/lapLength-laps)*512;int i=std::min(511,int(index));return laps*lapLength+std::lerp(arc[i],arc[i+1],index-i);}
    int i=int(std::floor(s/4.));while(int(arc.size())<=i+1){double end=double(arc.size())*4;arc.push_back(arc.back()+glm::length(routePoint(end)-routePoint(end-4)));}return std::lerp(arc[i],arc[i+1],s/4-i);
}
std::vector<Obstacle> Course::obstaclesNear(glm::dvec2 p) const {
    std::vector<Obstacle> out;
    if(format==RaceFormat::Circuit){for(size_t i=0;i<markers.size();++i){auto v=markers[i];if(glm::length(p-glm::dvec2(v.x,v.z))<80)out.push_back({{v.x,v.z},.14,3.2,i,false,v.y});}return out;}
    int nearest=int(std::floor(p.y/gateSpacing+.5));
    int last=format==RaceFormat::Endless?nearest+2:std::min(gateCount,nearest+2);
    for(int gate=std::max(1,nearest-2);gate<=last;++gate)for(int side:{-1,1}){auto v=fr::fr_checkpoint_post(gate,side,field());out.push_back({{v.x,v.z},.14,3.2,uint64_t(gate*2+(side>0)),false,v.y});}return out;
}
}
