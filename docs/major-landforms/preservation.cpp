#include "infinity/SurfaceLife.hpp"
#include <iostream>
#include <iomanip>
using namespace infinity;
int main(){Universe u;std::cout<<std::setprecision(17);
for(const char* a:{"0,0,0/1:0/2:0/3:0","0,0,0/1:0/2:26/3:14","0,0,0/1:0/2:167/3:0","0,0,0/1:0/2:353/3:0"}){
 const auto p=u.describe(Address::parse(a));std::cout<<"NODE "<<a<<' '<<p.seed<<' '<<descriptorFingerprint(p)<<'\n';
 for(unsigned i=0;i<32;++i){const auto d=glm::normalize(glm::dvec3(std::sin(i*3.4)+.1,std::cos(i*.37),std::cos(i*1.9)));
 std::cout<<"H "<<u.terrainHeight(p,d)<<'\n';}
 const auto world=generateSurfaceLife(u,p);
 for(const auto& city:world->settlements)std::cout<<"CITY "<<city.slot<<' '<<cityFingerprint(city)<<' '<<city.seed<<' '<<city.population<<' '<<city.landingPoint.x<<' '<<city.landingPoint.y<<' '<<city.landingPoint.z<<'\n';
}}
