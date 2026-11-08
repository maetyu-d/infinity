#include "infinity/ProbePresentation.hpp"
#include <algorithm>
#include <iomanip>
#include <glm/gtc/matrix_transform.hpp>
#include <numbers>
#include <sstream>

namespace infinity {
namespace {
constexpr double tau = 2 * std::numbers::pi;
void quad(Mesh& m, glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, glm::dvec3 d, glm::vec3 colour) {
    const auto n=uint32_t(m.vertices.size());
    for(auto p:{a,b,c,d})m.vertices.push_back({float(p.x),float(p.y),float(p.z),rgba(colour)});
    m.indices.insert(m.indices.end(),{n,n+1,n+2,n,n+2,n+3});
}
void ring(Mesh& m,double inner,double outer,double z,double thickness,glm::vec3 colour) {
    for(unsigned k=0;k<48;++k){
        const double a=k*tau/48,b=(k+1)*tau/48;
        auto p=[](double r,double t,double z){return glm::dvec3(r*std::cos(t),r*std::sin(t),z);};
        quad(m,p(inner,a,z),p(outer,a,z),p(outer,b,z),p(inner,b,z),colour);
        quad(m,p(inner,a,z),p(inner,b,z),p(inner,b,z+thickness),p(inner,a,z+thickness),colour*.45f);
        quad(m,p(outer,a,z),p(outer,a,z+thickness),p(outer,b,z+thickness),p(outer,b,z),colour*.65f);
    }
}
double unit(uint64_t id,unsigned index) { return double(mix64(id+index*0x9e3779b97f4a7c15ULL)>>11)*0x1p-53; }
void box(Mesh& m,glm::dvec3 centre,glm::dvec3 radial,glm::dvec3 half,glm::vec3 colour) {
    const glm::dvec3 x=radial*half.x,y=glm::dvec3(-radial.y,radial.x,0)*half.y,z{0,0,half.z};
    quad(m,centre-x-y-z,centre+x-y-z,centre+x+y-z,centre-x+y-z,colour);
    quad(m,centre-x-y+z,centre-x+y+z,centre+x+y+z,centre+x-y+z,colour*.60f);
    quad(m,centre-x-y-z,centre-x-y+z,centre+x-y+z,centre+x-y-z,colour*.42f);
    quad(m,centre-x+y-z,centre+x+y-z,centre+x+y+z,centre-x+y+z,colour*.82f);
    quad(m,centre-x-y-z,centre-x+y-z,centre-x+y+z,centre-x-y+z,colour*.53f);
    quad(m,centre+x-y-z,centre+x-y+z,centre+x+y+z,centre+x+y-z,colour*.70f);
}
void barrel(Mesh& m,double radius,double length,glm::vec3 colour,unsigned sectors=16) {
    for(unsigned k=0;k<sectors;++k){const double a=k*tau/sectors,b=(k+1)*tau/sectors;
        const auto p=[](double a,double r,double z){return glm::dvec3(std::cos(a)*r,std::sin(a)*r,z);};
        quad(m,p(a,radius,0),p(b,radius,0),p(b,radius,length),p(a,radius,length),colour*float(.62+.38*std::cos(a*.5)*std::cos(a*.5)));
        for(unsigned end=0;end<2;++end){const double z=end?length:0;const auto first=uint32_t(m.vertices.size());
            for(const auto point:{glm::dvec3(0,0,z),p(a,radius,z),p(b,radius,z)})
                m.vertices.push_back({float(point.x),float(point.y),float(point.z),rgba(colour*(end?.85f:.42f))});
            m.indices.insert(m.indices.end(),{first,first+1,first+2});
        }
    }
}
}
ProbeVisual probeVisual(const Borehole& b,double seconds,double density) {
    ProbeVisual v;v.phase=probePose(b,seconds).phase;
    const double resistance=std::clamp(density/3300.,.4,1.8);
    const double drilling=std::clamp(seconds-b.impactTime(),0.,b.drillSeconds);
    const double armed=std::max(0.,seconds-b.impactTime()-b.drillSeconds);
    const double speed=9.5/std::sqrt(resistance),coast=std::min(.35,armed);
    const double runUp=std::min(.35,drilling);
    const double travel=speed*(drilling-runUp+runUp*runUp/.7+coast-coast*coast/.7);
    // Integrate the coast analytically so stopping the cutter cannot snap its angle.
    v.cutterAngle=std::fmod(travel,tau);
    v.rollerAngle=std::fmod(-travel*2.3,tau);v.driveAngle=std::fmod(-travel*.7,tau);
    if(v.phase==ProbePhase::Boring){
        v.rpm=speed*60/tau*(runUp/.35);
        v.load=std::clamp(.35*resistance+.14+.12*std::sin(drilling*3.7)+.06*std::sin(drilling*9.1),.12,.98);
        v.vibration=.0012+.004*v.load;
        v.dust=.22+v.load*.25;
        v.feedStroke=(.004+.009*v.load)*(.5+.5*std::sin(travel*3));
    } else if(v.phase==ProbePhase::Arming) v.rpm=speed*60/tau*(1-coast/.35);
    v.retract=smoothstep(.22,1.15,armed);
    v.heat=std::clamp((.15+.38*resistance)*(1-std::exp(-drilling/2.5))*std::exp(-armed/2.2),0.,1.);
    v.fuse=v.phase==ProbePhase::Arming?std::max(0.,b.blastTime()-seconds):0;
    v.blastAge=seconds-b.blastTime();
    if(v.blastAge>=0){
        v.flash=std::exp(-v.blastAge*14)*(1-smoothstep(.20,.45,v.blastAge));
        v.dust=(1-std::exp(-v.blastAge*12))*std::exp(-v.blastAge*.32);
        v.vibration=.025*std::exp(-v.blastAge*9);
    }
    if(!b.controls.empty()) {
        const auto operation=probeOperation(b,seconds);
        const double work=operation.work;
        const double runUp=std::min(.35,work);
        const double rotation=speed*(work-runUp+runUp*runUp/.7);
        v.cutterAngle=std::fmod(rotation,tau);v.rollerAngle=std::fmod(-rotation*2.3,tau);v.driveAngle=std::fmod(-rotation*.7,tau);
        v.rpm=v.phase==ProbePhase::Boring ? speed*operation.rate*60/tau*std::min(1.,work/.35) : 0;
        v.load=v.phase==ProbePhase::Boring ? std::clamp((.35*resistance+.14)*operation.rate,.1,.99) : 0;
        v.heat=operation.heat;
        if(v.phase!=ProbePhase::Blast&&v.phase!=ProbePhase::Beacon) {
            v.vibration=v.phase==ProbePhase::Boring?.0012+.004*v.load:0;
            v.dust=v.phase==ProbePhase::Boring?.22+v.load*.25:0;
        }
        v.feedStroke=v.phase==ProbePhase::Boring?(.004+.009*v.load)*(.5+.5*std::sin(rotation*3)):0;
        v.retract=operation.stopped>0?smoothstep(.22,1.15,seconds-operation.stopped):0;
    }
    return v;
}
std::string probeToolReadout(const ProbeVisual& v) {
    std::ostringstream out;
    if(v.phase==ProbePhase::Arming)out<<(v.retract<.99?"CUTTER STOP / HYDRAULIC STOW / FUSE ":"CAMERA ANCHORED / CUTTER STOWED / FUSE ")<<std::fixed<<std::setprecision(1)<<v.fuse<<" S";
    else if(v.phase==ProbePhase::Paused)out<<"PAUSED / COOLING "<<int(v.heat*100)<<"% / P RESUME / R ABORT";
    else if(v.phase==ProbePhase::Retracting||v.phase==ProbePhase::Recovered)out<<"CHARGE SAFE / NO BLAST / PROBE EXPENDED";
    else if(v.blastAge>=0)out<<(v.dust>.15?"WORK LIGHT / DUST SETTLING / PROTECTED CAMERA":"WORK LIGHT / CAVERN SURVEY / PROTECTED CAMERA");
    else out<<"CUTTER "<<int(std::round(v.rpm))<<" RPM / LOAD "<<int(std::round(v.load*100))<<"% / TIP HEAT "<<int(std::round(v.heat*100))<<"%";
    return out.str();
}
ProbeRig probeRig() {
    ProbeRig r;
    ring(r.housing,.60,.68,-.87,.18,{.07f,.11f,.15f});
    ring(r.housing,1.245,1.28,-2.02,.075,{.14f,.20f,.25f});
    for(unsigned k=0;k<4;++k){
        const double a=(k+.5)*tau/4;
        const auto radial=glm::dvec3(std::cos(a),std::sin(a),0),side=glm::dvec3(-std::sin(a),std::cos(a),0)*.033;
        quad(r.housing,radial*.64+glm::dvec3(0,0,-.88)-side,radial*1.22+glm::dvec3(0,0,-1.98)-side,
             radial*1.22+glm::dvec3(0,0,-1.98)+side,radial*.64+glm::dvec3(0,0,-.88)+side,{.15f,.21f,.26f});
        box(r.housing,radial*1.20+glm::dvec3(0,0,-1.94),radial,{.055,.075,.04},{.10f,.15f,.20f});
        appendSphere(r.lamps,radial*1.19+glm::dvec3(0,0,-1.88),.037,{.72f,.88f,1.f},4,6,true);
        // Amber service bands and dark cable runs distinguish stationary
        // supports from the rotating steel head at a glance.
        box(r.housing,radial*.69+glm::dvec3(0,0,-.98),radial,{.045,.065,.025},{.52f,.27f,.035f});
    }
    ring(r.cutter,.92,1.055,-.015,.10,{.17f,.23f,.29f});
    ring(r.cutter,.19,.27,.055,.075,{.26f,.33f,.39f});
    barrel(r.cutter,.19,.14,{.11f,.17f,.22f},20);
    for(unsigned k=0;k<3;++k){
        const double offset=k*tau/3;
        auto p=[&](double radius,double a,double z){return glm::dvec3(radius*std::cos(a+offset),radius*std::sin(a+offset),z);};
        // Thick scalloped sectors, with open radial channels for cuttings.
        // Both faces and side walls are authored; there are no paper-thin fan blades.
        for(unsigned segment=0;segment<8;++segment){
            const double a=-.66+segment*1.32/8,b=-.66+(segment+1)*1.32/8;
            const double inner=.28,outer=.93;
            quad(r.cutter,p(inner,a,.065),p(outer,a,.065),p(outer,b,.065),p(inner,b,.065),k==0?glm::vec3(.20f,.29f,.35f):glm::vec3(.15f,.22f,.28f));
            quad(r.cutter,p(inner,a,-.06),p(inner,b,-.06),p(outer,b,-.06),p(outer,a,-.06),{.10f,.15f,.19f});
            quad(r.cutter,p(outer,a,.065),p(outer,a,-.06),p(outer,b,-.06),p(outer,b,.065),{.29f,.36f,.40f});
        }
        for(double a:{-.66,.66})quad(r.cutter,p(.28,a,-.06),p(.93,a,-.06),p(.93,a,.065),p(.28,a,.065),{.07f,.11f,.15f});
        for(unsigned j=0;j<4;++j){const double a=offset-.48+j*.30;
            const auto radial=glm::dvec3(std::cos(a),std::sin(a),0);
            box(r.cutter,radial*.88+glm::dvec3(0,0,.09),radial,{.035,.026,.017},{.39f,.44f,.48f});
        }
        for(double a:{-.34,0.,.34}){const auto radial=glm::dvec3(std::cos(a+offset),std::sin(a+offset),0);
            box(r.cutter,radial*.63+glm::dvec3(0,0,.069),radial,{.17,.018,.003},{.025f,.045f,.06f});}
    }
    for(unsigned k=0;k<24;++k){const double a=k*tau/24;
        const auto radial=glm::dvec3(std::cos(a),std::sin(a),0);
        box(r.cutter,radial*1.065+glm::dvec3(0,0,-.035),radial,{.095,.035,.065},k%4?glm::vec3(.30f,.38f,.44f):glm::vec3(.55f,.30f,.045f));
        box(r.hotTeeth,radial*1.13+glm::dvec3(0,0,-.058),radial,{.028,.026,.018},{1.f,.28f,.035f});
    }
    barrel(r.roller,.112,.37,{.24f,.32f,.39f},16);
    for(unsigned row=0;row<4;++row)for(unsigned k=0;k<8;++k){
        const double a=k*tau/8+(row%2)*.18;const auto radial=glm::dvec3(std::cos(a),std::sin(a),0);
        box(r.roller,radial*.119+glm::dvec3(0,0,.035+row*.095),radial,{.022,.024,.022},{.50f,.56f,.60f});
    }
    ring(r.drive,.25,.33,.12,.055,{.12f,.18f,.23f});
    for(unsigned k=0;k<16;++k){const double a=k*tau/16;const auto radial=glm::dvec3(std::cos(a),std::sin(a),0);
        box(r.drive,radial*.335+glm::dvec3(0,0,.15),radial,{.027,.021,.025},k%4?glm::vec3(.27f,.34f,.39f):glm::vec3(.62f,.32f,.04f));}
    barrel(r.actuator,1,1,{.25f,.31f,.36f},12);
    for(auto* mesh:{&r.housing,&r.cutter,&r.lamps,&r.roller,&r.drive,&r.actuator,&r.hotTeeth})mesh->compact();
    return r;
}
ProbeMechanism probeMechanism(const ProbeVisual& v) {
    ProbeMechanism m;
    // A full-size head pivots edge-on and slides back. This rigid mechanism
    // replaces the old visual shrinking of the whole cutter during stow.
    const double fold=v.retract,slide=smoothstep(.30,1.,fold);
    m.carrier=glm::translate(glm::dmat4(1),glm::dvec3(0,.45*fold*fold,-2.2+1.4*slide-v.feedStroke))
        *glm::rotate(glm::dmat4(1),fold*std::numbers::pi*.5,glm::dvec3(1,0,0));
    m.head=m.carrier*glm::rotate(glm::dmat4(1),v.cutterAngle,glm::dvec3(0,0,1));
    m.drive=m.carrier*glm::rotate(glm::dmat4(1),v.driveAngle,glm::dvec3(0,0,1));
    for(unsigned k=0;k<3;++k){const double a=k*tau/3;
        m.rollers[k]=m.head*glm::translate(glm::dmat4(1),glm::dvec3(std::cos(a)*.45,std::sin(a)*.45,.025))
            *glm::rotate(glm::dmat4(1),a,glm::dvec3(0,0,1))
            *glm::rotate(glm::dmat4(1),std::numbers::pi*.5,glm::dvec3(0,1,0))
            *glm::rotate(glm::dmat4(1),v.rollerAngle,glm::dvec3(0,0,1));
    }
    for(unsigned k=0;k<2;++k){const double x=k?.64:-.64;
        const glm::dvec3 start{x,.04,-.88};const auto finish=glm::dvec3(m.carrier*glm::dvec4(x,0,.11,1));
        const auto delta=finish-start;const double length=glm::length(delta);
        const auto direction=delta/std::max(length,1e-9);
        const auto rotation=glm::mat4_cast(glm::quatLookAtRH(-direction,glm::dvec3(0,1,0)));
        const auto pose=glm::translate(glm::dmat4(1),start)*rotation;
        m.barrels[k]=pose*glm::scale(glm::dmat4(1),glm::dvec3(.047,.047,length*.50));
        m.rods[k]=pose*glm::translate(glm::dmat4(1),glm::dvec3(0,0,length*.42))
            *glm::scale(glm::dmat4(1),glm::dvec3(.022,.022,length*.58));
    }
    return m;
}
ProbeMotes probeMotes(const Borehole& b,const ProbeVisual& v,double seconds,glm::vec3 mineral) {
    ProbeMotes out;
    const double stopped=b.controls.empty()?std::max(0.,seconds-b.impactTime()-b.drillSeconds):
        (probeOperation(b,seconds).stopped>0?std::max(0.,seconds-probeOperation(b,seconds).stopped):0);
    if(v.phase!=ProbePhase::Boring&&v.phase!=ProbePhase::Arming&&v.blastAge<0)return out;
    if(v.blastAge<0&&stopped>.8)return out;
    if(v.blastAge>9)return out;
    const double depth=probePose(b,seconds).depth;
    for(unsigned k=0;k<128;++k){
        const double a=unit(b.id,k*4)*tau,h=unit(b.id,k*4+1),s=unit(b.id,k*4+2),t=unit(b.id,k*4+3);
        ProbeMote m;m.fragment=k<32;m.spark=v.blastAge<0&&k>=32&&k<44;
        m.colour=glm::mix(mineral*.45f,glm::vec3(.28f,.22f,.16f),float(h*.6));
        if(v.blastAge<0){
            const double age=std::fmod((seconds-stopped)*1.4+t,1.)+stopped;
            if(age>=1)continue;
            // Emit from the rotating channels, then carry material back along
            // the shaft with a short tangential drift. It never crosses the lens.
            const double phase=v.cutterAngle+a-(v.rpm*tau/60)*age;
            const double radius=std::min(1.20,.76+h*.38+age*.06);
            m.position={std::cos(phase)*radius,-depth+.03+age*1.5-age*age*.45,std::sin(phase)*radius};
            m.radius=m.fragment?.015+s*.045:.12+s*.24;
            m.opacity=float((1-age)*(m.fragment||m.spark?1:v.dust*.22));
            if(m.spark){m.radius=.007+s*.011;m.colour={1.f,.42f,.08f};if(v.rpm<1)continue;}
        }else{
            const double age=v.blastAge;
            const auto direction=glm::normalize(glm::dvec3(std::cos(a),h*2-1,std::sin(a)));
            m.position=direction*std::min(b.roomRadius()*.86,(3+s*8)*age);
            m.position.y-=b.finalDepth()+std::min(2.,age*age*.18);
            m.radius=m.fragment?.04+s*.12:.3+s*.65+age*.12;
            m.opacity=float(m.fragment?std::exp(-age*.9):v.dust*(.055+s*.055));
        }
        if(m.opacity>.002)out.items[out.count++]=m;
    }
    return out;
}
glm::vec3 probeWallLight(const Borehole& b,const ProbeVisual& v,glm::dvec3 p,glm::dvec3 eye,
                         glm::dvec3 forward,glm::vec3 colour,bool complete) {
    const double join=-b.depth+std::sqrt(b.chamberRadius*b.chamberRadius-b.shaftRadius*b.shaftRadius);
    glm::dvec3 normal;
    if(complete&&p.y<join-.01)normal=glm::normalize(glm::dvec3(0,-b.depth,0)-p);
    else if(!complete&&p.y<=-b.depth+.01)normal={0,1,0};
    else normal=glm::normalize(glm::dvec3(-p.x,0,-p.z));
    if(!std::isfinite(normal.x))normal={0,1,0};
    const auto delta=p-eye;const double distance=std::max(.05,glm::length(delta));
    const auto direction=delta/distance;
    const double cone=smoothstep(.60,.93,glm::dot(direction,glm::normalize(forward)));
    const double diffuse=std::max(0.,glm::dot(normal,-direction));
    const double lamp=std::min(1.6,cone*(.12+.88*diffuse)*4.2/(1+distance*distance*.035));
    const auto base=colour*.62f;
    glm::vec3 lit=base*float(.022+lamp)*glm::vec3(.78f,.90f,1.f);
    // Daylight decays down the retained shaft; the room receives the probe's
    // bounded work light instead of an impossible global underground sun.
    lit+=base*float(.13*std::exp(std::min(0.,p.y)/9.));
    const double toCharge=glm::length(p-glm::dvec3(0,-b.depth,0));
    const double pulse=v.phase==ProbePhase::Arming?.5+.5*std::sin(v.fuse*20):0;
    lit+=base*glm::vec3(1.f,.12f,.025f)*float((pulse*.65+v.flash*16)/(1+toCharge*toCharge*.05));
    const double sheen=std::pow(std::max(0.,diffuse),16)*cone/(1+distance*distance*.04);
    lit+=glm::vec3(.025f,.045f,.07f)*float(sheen);
    return glm::clamp(lit,glm::vec3(0),glm::vec3(1));
}
ProbeWallLighting probeLighting(const Borehole& b,const ProbeVisual& v,glm::dvec3 eye,
                                glm::dvec3 forward,bool complete) {
    const double pulse=v.phase==ProbePhase::Arming?.5+.5*std::sin(v.fuse*20):0;
    return {eye,glm::normalize(forward),b.depth,
            -b.depth+std::sqrt(b.chamberRadius*b.chamberRadius-b.shaftRadius*b.shaftRadius),
            pulse*.65+v.flash*16,complete};
}
glm::vec3 ProbeWallLighting::shade(glm::dvec3 p,glm::vec3 colour) const {
    glm::dvec3 normal;
    if(complete&&p.y<join-.01)normal=glm::normalize(glm::dvec3(0,-depth,0)-p);
    else if(!complete&&p.y<=-depth+.01)normal={0,1,0};
    else normal=glm::normalize(glm::dvec3(-p.x,0,-p.z));
    if(!std::isfinite(normal.x))normal={0,1,0};
    const auto delta=p-eye;const double distance=std::max(.05,glm::length(delta));
    const auto direction=delta/distance;
    const double cone=smoothstep(.60,.93,glm::dot(direction,forward));
    const double diffuse=std::max(0.,glm::dot(normal,-direction));
    const double lamp=std::min(1.6,cone*(.12+.88*diffuse)*4.2/(1+distance*distance*.035));
    const auto base=colour*.62f;
    glm::vec3 lit=base*float(.022+lamp)*glm::vec3(.78f,.90f,1.f);
    lit+=base*float(.13*std::exp(std::min(0.,p.y)/9.));
    const double toCharge=glm::length(p-glm::dvec3(0,-depth,0));
    lit+=base*glm::vec3(1.f,.12f,.025f)*float(charge/(1+toCharge*toCharge*.05));
    const double sheen=std::pow(std::max(0.,diffuse),16)*cone/(1+distance*distance*.04);
    lit+=glm::vec3(.025f,.045f,.07f)*float(sheen);
    return glm::clamp(lit,glm::vec3(0),glm::vec3(1));
}
bool boreEnclosesView(const Borehole& b,double seconds,glm::dvec3 eye,glm::dvec3 forward,
                      double fov,double aspect,double portalRadius,double nearMetres) {
    if(!(fov>0&&fov<3.1&&aspect>0&&portalRadius>=b.shaftRadius&&nearMetres>=0))return false;
    const auto up=glm::normalize(b.entry),delta=b.entry-eye;
    const double tangent=std::tan(fov*.5);
    const double nearReach=nearMetres*std::sqrt(1+tangent*tangent*(1+aspect*aspect));
    if(glm::dot(eye-b.entry,up)>-6||boreClearance(b,eye,seconds)<std::max(.5,nearReach*1.05))return false;
    const double distance=glm::length(delta);
    if(distance<=portalRadius)return false;
    const double diagonal=std::atan(tangent*std::sqrt(1+aspect*aspect));
    const double angle=diagonal+std::asin(std::min(1.,portalRadius/distance))+.03;
    return angle<3.141592653589793&&glm::dot(delta/distance,glm::normalize(forward))<std::cos(angle);
}
} // namespace infinity
