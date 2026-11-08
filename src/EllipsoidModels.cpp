#include "infinity/Ellipsoid.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

namespace infinity::ellipsoid {
static Rotation turn(double angle,Vec axis={1,0,0}) {return glm::angleAxis(angle,glm::normalize(axis));}
static int joint(Model& m,int parent,Vec offset,const char* name,Rotation rotation={1,0,0,0}) {
    m.joints.push_back({parent,offset,rotation,name});return int(m.joints.size()-1);
}
static void part(Model& m,int parent,Vec center,Vec radii,Material material,Rotation rotation={1,0,0,0}) {
    m.parts.push_back({center,rotation,radii,material,parent});
}
static Rotation gait(double time,double phase,double amplitude) {
    // Interpolate between authored opposing local poses, never between meshes.
    double blend=.5+.5*std::sin(time*5+phase);
    return glm::slerp(turn(-amplitude),turn(amplitude),blend);
}
Model human(double time,bool walking) {
    Model m;double walk=walking?1.:0.,breathe=std::sin(time*1.7)*.012;
    int root=joint(m,-1,{0,1.10+std::sin(time*10)*.022*walk,0},"pelvis",turn(std::sin(time*2.5)*.025,{0,0,1}));
    part(m,root,{0,0,0},{.30,.24,.20},Material::Leather);
    int chest=joint(m,root,{0,.32,0},"spine",turn(std::sin(time*5)*.04*walk,{0,1,0}));
    part(m,chest,{0,0,0},{.25,.32+breathe,.19},Material::Ochre);
    part(m,chest,{0,.20,0},{.37,.25,.22},Material::Ochre);
    part(m,chest,{0,.35,.01},{.30,.13,.19},Material::Cloth);
    int head=joint(m,chest,{0,.49,0},"neck",turn(std::sin(time*.8)*.12,{0,1,0}));
    part(m,head,{0,.025,0},{.105,.14,.11},Material::Skin);
    part(m,head,{0,.23,0},{.21,.265,.195},Material::Skin);
    part(m,head,{0,.30,-.015},{.225,.20,.20},Material::Cloth);
    part(m,head,{0,.215,.19},{.065,.08,.09},Material::Skin);
    for(int side:{-1,1}) {
        part(m,head,{side*.20,.23,0},{.045,.085,.065},Material::Skin);
        part(m,head,{side*.085,.265,.171},{.055,.035,.031},Material::Bone);
        part(m,head,{side*.085,.265,.195},{.023,.024,.011},Material::Leather);
        int shoulder=joint(m,chest,{side*.34,.25,0},side<0?"left shoulder":"right shoulder",
            gait(time,side<0?0:glm::pi<double>(),.55*walk+.025));
        part(m,shoulder,{side*.035,-.05,0},{.15,.16,.16},Material::Ochre);
        part(m,shoulder,{side*.04,-.24,0},{.12,.255,.13},Material::Ochre,turn(side*.09,{0,0,1}));
        int elbow=joint(m,shoulder,{side*.06,-.45,0},"elbow",turn(-.15-.34*walk*(.5+.5*std::sin(time*5+side))));
        part(m,elbow,{0,0,0},{.115,.12,.12},Material::Leather);
        part(m,elbow,{0,-.20,.015},{.10,.225,.11},Material::Ochre);
        int wrist=joint(m,elbow,{0,-.40,.03},"wrist");
        part(m,wrist,{0,-.065,.01},{.105,.13,.055},Material::Leather);
        for(int finger=0;finger<3;++finger) part(m,wrist,{(finger-1)*.055,-.17,.025},{.028,.085,.031},Material::Leather,turn((finger-1)*.12,{0,0,1}));
        part(m,wrist,{side*.11,-.08,.02},{.045,.08,.04},Material::Leather,turn(-side*.4,{0,0,1}));
        int hip=joint(m,root,{side*.17,-.11,0},"hip",gait(time,side<0?glm::pi<double>():0,.58*walk));
        part(m,hip,{0,-.24,0},{.16,.31,.17},Material::Cloth);
        int knee=joint(m,hip,{0,-.49,0},"knee",turn(.06+std::max(0.,std::sin(time*5+(side<0?0:glm::pi<double>())))*.66*walk));
        part(m,knee,{0,0,0},{.14,.14,.15},Material::Leather);
        part(m,knee,{0,-.23,-.015},{.12,.255,.13},Material::Cloth);
        int ankle=joint(m,knee,{0,-.43,0},"ankle",turn(-.04));
        part(m,ankle,{0,-.025,.10},{.145,.115,.25},Material::Leather);
        part(m,ankle,{0,.065,-.015},{.13,.13,.15},Material::Leather);
    }
    // 42 independently transformed volumes; no triangle skin or hidden body mesh.
    return m;
}
Model creature(double time,bool walking) {
    Model m;double gaitAmount=walking?.75:.08;
    int root=joint(m,-1,{0,1.15+std::sin(time*3)*.025,0},"beast root",turn(std::sin(time*2)*.04,{0,0,1}));
    part(m,root,{0,0,0},{.65,.55,1.04},Material::Rust);
    part(m,root,{0,.34,-.24},{.54,.56,.67},Material::Rust);
    part(m,root,{0,-.18,.52},{.48,.33,.62},Material::Skin);
    int neck=joint(m,root,{0,.16,.79},"swan neck",turn(-.27+std::sin(time*1.5)*.05));
    part(m,neck,{0,.20,.15},{.28,.50,.27},Material::Rust,turn(.3));
    int head=joint(m,neck,{0,.58,.26},"three eyed head",turn(std::sin(time*1.1)*.18,{0,1,0}));
    part(m,head,{0,0,0},{.43,.34,.42},Material::Rust);
    part(m,head,{0,-.13,.36},{.32,.18,.42},Material::Skin);
    part(m,head,{0,-.22,.50},{.28,.085,.27},Material::Leather);
    for(int eye=-1;eye<=1;++eye) {
        part(m,head,{eye*.23,.13,.32},{.115,.12,.085},Material::Bone);
        part(m,head,{eye*.23,.13,.397},{.044,.055,.019},Material::Leather);
    }
    for(int side:{-1,1}) {
        part(m,head,{side*.34,.23,-.07},{.10,.30,.08},Material::Gold,turn(side*-.55,{0,0,1}));
        part(m,head,{side*.43,.03,-.04},{.24,.09,.14},Material::Skin,turn(side*.27,{0,0,1}));
        for(int end:{-1,1}) {
            int hip=joint(m,root,{side*.47,-.15,end*.66},"beast hip",gait(time,(side==end?0:glm::pi<double>()),gaitAmount));
            part(m,hip,{side*.09,-.22,0},{.23,.34,.22},Material::Rust);
            int knee=joint(m,hip,{side*.10,-.45,0},"backward knee",turn(-.38-.3*std::max(0.,std::sin(time*5+side+end))*gaitAmount));
            part(m,knee,{0,-.23,0},{.12,.28,.13},Material::Skin);
            part(m,knee,{0,-.45,.11},{.18,.12,.28},Material::Leather);
        }
    }
    int parent=root;
    for(int i=0;i<6;++i) {
        parent=joint(m,parent,i==0?Vec(0,.1,-.85):Vec(0,0,-.34),"tail",turn(std::sin(time*2.2-i*.5)*.18,{0,1,0}));
        part(m,parent,{0,0,-.17},{.22-i*.025,.19-i*.023,.28},Material::Rust);
    }
    return m;
}
// The same chassis and cockpit are used in both cameras. Coordinates are relative
// to the physical centre of mass; the wheel centres come from the shared struts.
static Model rallyBuggy(const VehiclePose& pose,Material paint,bool inside) {
    using namespace infinity::buggy;
    Model m;int root=joint(m,-1,{0,0,0},"rally chassis");
    double damage=1-pose.chassis;
    auto bar=[&](Vec a,Vec b,double r,Material mat=Material::Metal) {
        auto e=segment(a,b,r,mat);e.parent=root;m.parts.push_back(e);
    };
    auto panel=[&](Vec p,Vec size,Material mat,Rotation q=Rotation(1,0,0,0)) {part(m,root,p,size,mat,q);};
    // Low skid tray, separate side pods and a tapered front cowl leave a clear
    // open cockpit silhouette, with exposed wheels and a rear engine cradle.
    panel({0,-.28,0},{.73,.105,1.52},Material::Cloth);
    panel({0,-.12,1.18},{.53,.16,.64},paint,turn(-.035));
    panel({0,.12,1.05},{.42,.075,.72},paint);
    panel({0,.198,1.14},{.095,.014,.53},Material::Cloth);
    for(int side:{-1,1}) {
        int si=side>0?1:0;double frontDent=pose.dents[si],rearDent=pose.dents[si+2];
        panel({side*(.60-frontDent*.10),-.04-frontDent*.13,1.10-frontDent*.13},{.20*(1-frontDent*.25),.115*(1-frontDent*.55),.69},frontDent>.5?Material::Rust:paint,turn(side*frontDent*.23,{0,0,1}));
        panel({side*(.69-rearDent*.09),-.04-rearDent*.06,-.18},{.105,.21*(1-rearDent*.35),.70},paint);
        panel({side*.695,.11,-.45},{.020,.025,.44},Material::Bone);
        bar({side*.66,-.23,-1.47},{side*.67,-.23,1.46},.038);
        bar({side*.67,-.18,-1.22},{side*.64,cageRearHeight-damage*.10,-.90},.043,Material::Cloth);
        bar({side*.64,cageRearHeight-damage*.10,-.90},{side*.72,.12,.72},.043,Material::Cloth);
        bar({side*.72,.12,.72},{side*.66,-.22,1.35},.038,Material::Cloth);
        bar({side*.65,.02,-1.33},{side*.64,cageRearHeight-damage*.10,-.90},.033);
        // Rear bracing, roof rail and side impact triangulation.
        bar({side*.64,cageRearHeight-damage*.10,-.90},{side*.65,cageFrontHeight,.38},.041,Material::Cloth);
        bar({side*.66,-.19,-.93},{side*.66,.36,-.05},.025,Material::Cloth);
        bar({side*.66,.36,-.05},{side*.67,-.18,.59},.025,Material::Cloth);
        panel({side*.695,.35,.44},{.080,.095,.17},Material::Cloth);
        panel({side*.705,.355,.45},{.018,.066,.125},Material::Metal);
        // Recessed lamps: no floating lamp volumes above the bonnet.
        panel({side*.44,-.06,1.64},{.135,.105,.055},Material::Cloth);
        panel({side*.44,-.06,1.683},{.105,.075,.018},Material::Bone);
        panel({side*.57,.09,-1.48},{.065,.040,.022},Material::Red);
        for(int i=si;i<4;i+=2) {
            double compression=std::clamp(pose.compression[i],0.,suspensionTravel);
            double y=-suspensionRest+compression,ax=axle(i);
            double steer=pose.independentWheels?pose.wheelSteer[i]:(i<2?pose.steerAngle:0);
            double phase=pose.independentWheels?pose.wheelRotation[i]:pose.wheelPhase;
            double health=pose.tyres[i],radius=tyreRadius*(.65+.35*health);
            double camber=side*(-.025+compression*.12)+(1-health)*std::sin(phase)*.035;
            int hub=joint(m,root,{side*track*.5,y,ax},"upright",turn(steer,{0,1,0})*turn(camber,{0,0,1}));
            int wheel=joint(m,hub,{0,0,0},"rotating tyre",turn(phase));
            part(m,hub,{0,0,0},{.225,radius,.46},Material::Leather);
            for(int face:{-1,1}) {
                part(m,hub,{face*.13,0,0},{.105,radius*.965,.444},Material::Leather);
                part(m,hub,{face*.224,0,0},{.025,radius*.62,.282},Material::Metal);
                part(m,hub,{face*.249,0,0},{.012,radius*.50,.230},Material::Cloth);
            }
            for(int spoke=0;spoke<6;++spoke) {
                double a=phase+spoke*glm::two_pi<double>()/6;
                Vec a0(side*.263,std::sin(a)*.070,std::cos(a)*.070),b0(side*.263,std::sin(a+.16)*.215,std::cos(a+.16)*.215);
                auto e=segment(a0,b0,.023,Material::Metal);e.parent=hub;m.parts.push_back(e);
                part(m,hub,{side*.274,std::sin(a)*.058,std::cos(a)*.058},{.013,.013,.013},Material::Gold);
            }
            part(m,hub,{side*.274,0,0},{.022,.047,.047},Material::Metal);
            for(int tread=0;tread<18;++tread) for(int row:{-1,1}) {
                double a=tread*glm::two_pi<double>()/18+row*.075;
                part(m,wheel,{row*.084,std::sin(a)*radius*.95,std::cos(a)*.438},{.092,.051,.028},Material::Cloth,turn(-a));
            }
            Vec lower(side*.97,y,ax),upper(side*.94,y+.18,ax);
            for(double dz:{-.27,.27}) {
                bar({side*.46,-.27,ax+dz},lower,.028);
                bar({side*.49,.06,ax+dz*.72},upper,.023);
            }
            Vec top(side*.58,.30,ax-.12),bottom(side*.90,y+.04,ax-.02);
            bar(top,bottom,.050,paint);
            bar(glm::mix(top,bottom,.45),bottom,.025,Material::Metal);
            // A connected helical coil around the actual changing damper axis.
            Vec axis=glm::normalize(bottom-top),u=glm::normalize(glm::cross(axis,Vec(0,0,1))),v=glm::cross(axis,u);
            Vec prev=glm::mix(top,bottom,.12)+u*.063;
            for(int coil=1;coil<=28;++coil) {
                double t=coil/28.,a=t*glm::two_pi<double>()*5;
                Vec p=glm::mix(top,bottom,.12+t*.59)+(u*std::cos(a)+v*std::sin(a))*.063;
                bar(prev,p,.010,Material::Metal);prev=p;
            }
            bar({side*.39,-.13,ax+.06},lower+Vec(0,.04,-.06),.013,Material::Cloth);
        }
    }
    bar({-.64,cageRearHeight-damage*.10,-.90},{.64,cageRearHeight-damage*.10,-.90},.044,Material::Cloth);
    bar({-.65,cageFrontHeight,.38},{.65,cageFrontHeight,.38},.038,Material::Cloth);
    bar({-.65,.13,-1.32},{.65,1.52,-.91},.027,Material::Cloth);
    bar({-.74,-.23,1.78},{.74,-.23-damage*.12,1.78-damage*.12},.048);
    bar({-.70,-.22,-1.66},{.70,-.22,-1.66},.045);
    // Rear mounted engine, fan, protective cradle and paired exhausts.
    panel({0,.04,-1.12},{.43,.22,.36},Material::Metal);
    panel({0,.29,-1.06},{.29,.11,.24},Material::Cloth);
    for(int side:{-1,1}) {
        for(int cylinder=0;cylinder<3;++cylinder) panel({side*.30,.15,-.90-cylinder*.19},{.13,.13,.075},Material::Metal,turn(side*.4,{0,0,1}));
        bar({side*.36,.04,-1.20},{side*.53,-.07,-1.69},.045);
        panel({side*.54,-.055,-1.68},{.070,.070,.025},Material::Cloth);
    }
    // A side-mounted spare stays behind the cockpit, rather than obscuring the driver.
    panel({.15,.38,-1.25},{.44,.15,.44},Material::Leather,turn(.18,{0,0,1}));
    panel({.15,.525,-1.25},{.23,.018,.23},Material::Metal,turn(.18,{0,0,1}));
    bar({.71,.15,-1.08},{.72,2.20,-1.15},.009);
    panel({.72,2.03,-1.33},{.010,.12,.22},paint,turn(std::sin(pose.wheelPhase*.07)*.08));
    // Bucket seat, four-point harness and a driver seated inside the cage.
    Vec seat(-.34,.015,-.42);
    panel(seat+Vec(0,0,.02),{.27,.095,.34},Material::Leather);
    panel(seat+Vec(0,.30,-.20),{.27,.37,.09},Material::Cloth,turn(-.14));
    panel(seat+Vec(0,.64,-.22),{.21,.15,.08},Material::Cloth);
    Vec wheelCenter(-.34,.68,.45);double wheelTurn=-pose.steering*1.45;
    int steeringJoint=joint(m,root,wheelCenter,"steering rim",turn(wheelTurn,{0,0,1}));
    for(int j=0;j<28;++j) {
        double a=j*glm::two_pi<double>()/28,b=(j+1)*glm::two_pi<double>()/28;
        auto e=segment({std::cos(a)*.235,std::sin(a)*.215,0},{std::cos(b)*.235,std::sin(b)*.215,0},.019,Material::Leather);e.radii.y*=1.8;e.parent=steeringJoint;m.parts.push_back(e);
    }
    for(int j=0;j<3;++j) {
        double a=j*glm::two_pi<double>()/3;
        auto e=segment({0,0,0},{std::cos(a)*.22,std::sin(a)*.195,0},.012,Material::Metal);e.parent=steeringJoint;m.parts.push_back(e);
    }
    part(m,steeringJoint,{0,0,0},{.061,.052,.022},Material::Cloth);
    part(m,steeringJoint,{0,0,-.024},{.023,.023,.008},paint);
    bar(wheelCenter+Vec(0,-.10,.08),{-.34,.04,.63},.028,Material::Metal);
    // Instrument binnacle and five tactile switches. Needles are physical volumes.
    panel({.23,.55,.77},{.54,.13,.08},Material::Cloth);
    for(int gauge=0;gauge<3;++gauge) {
        Vec center(-.02+gauge*.235,.595,.685);
        part(m,root,center,{.103,.103,.018},Material::Metal);
        part(m,root,center+Vec(0,0,-.016),{.091,.091,.010},Material::Leather);
        for(int tick=0;tick<11;++tick) {
            double a=glm::pi<double>()*1.25-tick*glm::pi<double>()*1.5/10;
            Vec p=center+Vec(std::cos(a)*.076,std::sin(a)*.076,-.029);
            part(m,root,p,{.005,.009,.003},tick>8?Material::Red:Material::Bone,turn(a-glm::half_pi<double>(),{0,0,1}));
        }
        double value=gauge==0?std::abs(pose.speed)/65:gauge==1?pose.rpm/7500:pose.engine;
        double a=glm::pi<double>()*1.25-std::clamp(value,0.,1.)*glm::pi<double>()*1.5;
        bar(center+Vec(0,0,-.033),center+Vec(std::cos(a)*.070,std::sin(a)*.070,-.033),.004,gauge==2?Material::Teal:Material::Red);
    }
    for(int j=0;j<5;++j) panel({-.10+j*.135,.437,.67},{.017,.028,.014},j==4?Material::Red:Material::Metal);
    panel({.39,.13,-.02},{.14,.10,.27},Material::Cloth);
    bar({.32,.20,.03},{.32,.38,.13},.016);
    panel({.32,.395,.13},{.035,.033,.035},Material::Leather);
    for(int side:{-1,1}) {
        Vec shoulder(-.34+side*.205,.64,-.40);
        double a=side<0?glm::pi<double>()*.91:glm::pi<double>()*.09;
        Vec hand=wheelCenter+turn(wheelTurn,{0,0,1})*Vec(std::cos(a)*.235,std::sin(a)*.215,-.020);
        Vec elbow=glm::mix(shoulder,hand,.48)+Vec(side*.085,-.135,-.04);
        bar(shoulder,elbow,.065,paint);bar(elbow,hand,.053,paint);
        panel(hand,{.057,.062,.045},Material::Leather,turn(wheelTurn,{0,0,1}));
        for(int finger=0;finger<3;++finger) panel(hand+Vec((finger-1)*.021,.013,-.032),{.012,.030,.018},Material::Cloth,turn(wheelTurn,{0,0,1}));
        Vec hip(-.34+side*.125,.08,-.35),knee(-.34+side*.135,.02,.05),foot(-.34+side*.15,-.14,.51);
        bar(hip,knee,.083,Material::Cloth);bar(knee,foot,.064,Material::Cloth);panel(foot,{.080,.060,.17},Material::Leather);
        if(!inside) bar({-.34+side*.15,.64,-.39},{-.34+side*.065,.20,-.22},.026,Material::Red);
    }
    if(!inside) {
        panel({-.34,.37,-.40},{.22,.31,.145},paint,turn(-.10));
        panel({-.34,.73,-.38},{.085,.09,.085},Material::Cloth);
        panel({-.34,.965,-.39},{.205,.235,.21},Material::Bone);
        panel({-.34,1.00,-.211},{.173,.078,.045},Material::Leather);
        panel({-.34,.87,-.215},{.151,.075,.061},Material::Bone);
        panel({-.34,.97,-.166},{.13,.045,.010},Material::Teal);
    }
    return m;
}
Model buggy(const VehiclePose& pose,Material paint) {return rallyBuggy(pose,paint,false);}
Model cockpit(const VehiclePose& pose,double) {return rallyBuggy(pose,Material::Ochre,true);}
Model buggy(double steering,double wheelPhase,Material paint) {VehiclePose pose;pose.steering=steering;pose.steerAngle=steering*.35;pose.wheelPhase=wheelPhase;return buggy(pose,paint);}
Model cockpit(double steering,double time) {VehiclePose pose;pose.steering=steering;return cockpit(pose,time);}
}
