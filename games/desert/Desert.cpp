#include "Desert.hpp"
#include "TyreSolver.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace desert {
static double angle(double x) { return std::remainder(x, 2*std::numbers::pi); }
double Course::noise(double x, double z, int octave) const {
    auto ix = static_cast<int64_t>(std::floor(x)), iz = static_cast<int64_t>(std::floor(z));
    double fx = x-std::floor(x), fz = z-std::floor(z);
    fx = fx*fx*fx*(fx*(fx*6-15)+10); fz = fz*fz*fz*(fz*(fz*6-15)+10);
    auto sample = [&](int64_t a, int64_t b) {
        uint64_t key = infinity::mix64(static_cast<uint64_t>(a)) ^ infinity::mix64(static_cast<uint64_t>(b)+0x8f173);
        return infinity::Random{seed ^ key ^ infinity::mix64(uint64_t(octave)+17)}.signedUnit(7);
    };
    return std::lerp(std::lerp(sample(ix,iz),sample(ix+1,iz),fx),std::lerp(sample(ix,iz+1),sample(ix+1,iz+1),fx),fz);
}
namespace fractal=infinity::fractal;
fractal::World Course::field() const {return {float(phase),uint32_t(seed&0xffffffu),&tracks,int(format),float(lapLength),markerCount(),float(gateInterval()),markers.empty()?nullptr:markers.data(),jacks.empty()?nullptr:jacks.data(),int(jacks.size())};}
double Course::rawHeight(double x,double z) const {return fractal::fr_raw_height({float(x),float(z)},field());}
double Course::height(double x,double z) const {return fractal::fr_height({float(x),float(z)},field());}
glm::dvec3 Course::normal(double x,double z) const {return glm::normalize(glm::dvec3(fractal::normal(field(),{float(x),float(height(x,z)),float(z)})));}
double Course::grip(double x,double z) const {return .90+.06*tracks.sample({float(x),float(z)}).compaction;}
double Damage::condition() const {
    double tyresMean=(tyres[0]+tyres[1]+tyres[2]+tyres[3])*.25;
    return std::clamp(chassis*.40+engine*.25+suspension*.20+tyresMean*.15,0.,1.);
}
static glm::dvec2 forwardVector(double yaw) {return {std::sin(yaw),std::cos(yaw)};}
static glm::dvec2 rightVector(double yaw) {return {std::cos(yaw),-std::sin(yaw)};}
struct TyrePatch {std::optional<fractal::Contact> contact;glm::dvec3 normal;double distance=2.;};
static TyrePatch tyrePatch(const Course& course,glm::dvec3 mount,glm::dquat rotation,double steer,double radius) {
    auto up=rotation*glm::dvec3(0,1,0);TyrePatch result{{},up,2.};if(up.y<=.18)return result;
    auto forward=rotation*glm::dvec3(std::sin(steer),0,std::cos(steer)),right=rotation*glm::dvec3(std::cos(steer),0,-std::sin(steer));
    double reach=std::min(.22,radius*.55);glm::dvec3 sum(0);double weightSum=0;
    std::array<std::optional<fractal::Contact>,5> probes;std::array<double,5> distances{};
    for(int probe=0;probe<5;++probe) {
        double along=probe==1?reach:probe==2?-reach:0,across=probe==3?.18:probe==4?-.18:0;
        probes[probe]=fractal::cast(course.field(),glm::vec3(mount+forward*along+right*across),glm::vec3(-up),1.8f);
        distances[probe]=probes[probe]?probes[probe]->distance+radius-std::sqrt(radius*radius-along*along):2.;
        if(distances[probe]<result.distance){result.distance=distances[probe];result.contact=probes[probe];}
    }
    for(int probe=0;probe<5;++probe)if(probes[probe]){double w=std::exp(-std::max(0.,distances[probe]-result.distance)/.025);sum+=glm::dvec3(probes[probe]->normal)*w;weightSum+=w;}
    if(weightSum>0)result.normal=glm::normalize(sum);return result;
}
// The heading is measured clockwise from +Z. Pitch/roll use the rendered body axes.
glm::dquat Car::orientation() const {
    return glm::angleAxis(yaw,glm::dvec3(0,1,0))*glm::angleAxis(pitch,glm::dvec3(1,0,0))*glm::angleAxis(roll,glm::dvec3(0,0,1));
}
void Car::setOrientation(glm::dquat q) {
    q=glm::normalize(q);auto facing=q*glm::dvec3(0,0,1),right=q*glm::dvec3(1,0,0),up=q*glm::dvec3(0,1,0);
    pitch=-std::asin(std::clamp(facing.y,-1.,1.));
    if(std::hypot(facing.x,facing.z)>1e-7)yaw=std::atan2(facing.x,facing.z);
    roll=std::atan2(right.y,up.y);
}
fractal::Vehicle Car::fieldVehicle() const {
    fractal::Vehicle car;
    car.placement={position.x,position.y,position.z,yaw};car.attitude={pitch,roll,damage.chassis,damage.engine};
    for(int i=0;i<4;++i) {car.tyres[i]=float(damage.tyres[i]);car.compression[i]=float(wheels[i].compression);car.steering[i]=float(wheels[i].steer);car.rotation[i]=float(std::remainder(wheels[i].rotation,2*std::numbers::pi));car.dents[i]=float(damage.dents[i]);}
    car.systems={rpm,steering,damage.suspension,damage.alignment};return car;
}
double Car::steeringLimit() const {
    double drift=std::abs(std::atan2(glm::dot(velocity,rightVector(yaw)),std::max(3.,std::abs(speed))));
    return std::min(.57,.57/(1+speed*speed*.010)+drift*.50);
}
void Car::reset(const Course& course,double z,double lane) {
    *this=Car{};yaw=course.heading(z);position=course.point(z,lane);settle(course);
}
void Car::settle(const Course& course) {
    using namespace infinity::buggy;
    auto f=forwardVector(yaw),r=rightVector(yaw);double rearWeight=frontAxle/wheelbase;
    double k=springRate*(.58+.42*damage.suspension);
    std::array<double,4> support{},sag{},radius{};
    for(int i=0;i<4;++i) {
        radius[i]=tyreRadius*(.65+.35*damage.tyres[i]);sag[i]=mass*gravity*(i<2?1-rearWeight:rearWeight)/(2*k);
        double x=position.x+f.x*axle(i)+r.x*side(i)*track*.5,z=position.z+f.y*axle(i)+r.y*side(i)*track*.5;
        support[i]=course.height(x,z)+radius[i]+suspensionRest-sag[i];
    }
    auto front=(support[0]+support[1])*.5,rear=(support[2]+support[3])*.5;
    pitch=-std::atan2(front-rear,wheelbase);roll=std::atan2(std::lerp(support[1],support[3],rearWeight)-std::lerp(support[0],support[2],rearWeight),track);
    position.y=std::lerp(front,rear,rearWeight);
    // Fit the same rounded rolling/shoulder support used while driving. Keep
    // the requested horizontal recovery position fixed during this adjustment.
    for(int iteration=0;iteration<3;++iteration) {
        auto q=orientation();std::array<double,4> error{};
        for(int i=0;i<4;++i){auto mount=position+q*glm::dvec3(side(i)*track*.5,0,axle(i));auto patch=tyrePatch(course,mount,q,wheels[i].steer,radius[i]);error[i]=patch.distance-(suspensionRest+radius[i]-sag[i]);}
        double ef=(error[0]+error[1])*.5,er=(error[2]+error[3])*.5,el=std::lerp(error[0],error[2],rearWeight),right=std::lerp(error[1],error[3],rearWeight);
        position.y-=std::clamp(std::lerp(ef,er,rearWeight)/std::max(.3,(q*glm::dvec3(0,1,0)).y),-.3,.3);
        pitch+=std::clamp(std::atan2(ef-er,wheelbase),-.15,.15);roll-=std::clamp(std::atan2(right-el,track),-.15,.15);
    }
    auto q=orientation();groundContacts=0;
    for(int i=0;i<4;++i) {
        auto mount=position+q*glm::dvec3(side(i)*track*.5,0,axle(i));auto patch=tyrePatch(course,mount,q,wheels[i].steer,radius[i]);auto& wheel=wheels[i];
        wheel.compression=std::clamp(suspensionRest+radius[i]-patch.distance,0.,suspensionTravel);wheel.point=mount-(q*glm::dvec3(0,1,0))*(suspensionRest-wheel.compression);
        wheel.contact=patch.contact&&wheel.compression>0;if(wheel.contact)++groundContacts;wheel.hasTrackPoint=false;
    }
    grounded=groundContacts>0;hasStepMotion=false;lastCollisionPosition=stepStartPosition=position;stepStartOrientation=q;
}
void Car::impact(double closingSpeed,glm::dvec2 outward,int wheel,bool landing) {
    if(!std::isfinite(closingSpeed)||closingSpeed<3.5) return;
    double threshold=landing?7.:3.5;
    double energy=std::pow(std::max(0.,closingSpeed-threshold),2)*(landing?.0012:.0009);
    if(energy<.0002) return;
    energy=std::min(.88,energy);
    auto r=rightVector(yaw),f=forwardVector(yaw);
    double side=glm::dot(outward,r),front=-glm::dot(outward,f);
    if(wheel<0) wheel=(side>0?0:1)+(front>0?0:2);
    wheel=std::clamp(wheel,0,3);
    damage.chassis=std::clamp(damage.chassis-energy*(landing?.32:.80),0.,1.);
    damage.engine=std::clamp(damage.engine-energy*(landing?.18:front<-.3?.75:.55),0.,1.);
    damage.suspension=std::clamp(damage.suspension-energy*(landing?.9:.45),0.,1.);
    damage.tyres[wheel]=std::clamp(damage.tyres[wheel]-energy*(landing?.7:1.35),0.,1.);
    damage.dents[wheel]=std::clamp(damage.dents[wheel]+energy*1.6,0.,1.);
    damage.alignment=std::clamp(damage.alignment+side*energy*.28,-.32,.32);
    impactFlash=std::clamp(energy*2.5+.08,0.,1.);hitAge=0;++impactSerial;
}
void Car::repair(double dt) {
    repairing=true;
    damage.chassis=std::min(1.,damage.chassis+dt*.07);
    damage.engine=std::min(1.,damage.engine+dt*.15);
    damage.suspension=std::min(1.,damage.suspension+dt*.13);
    damage.alignment*=std::exp(-dt*.7);
    for(int i=0;i<4;++i) {damage.tyres[i]=std::min(1.,damage.tyres[i]+dt*.15);damage.dents[i]=std::max(0.,damage.dents[i]-dt*.08);}
    repairProgress=damage.condition();
}
static double crossYaw(glm::dvec2 arm,glm::dvec2 force) {return arm.y*force.x-arm.x*force.y;}
static double angularInverseMass(const Car& car,glm::dvec2 arm,glm::dvec2 direction) {
    using namespace infinity::buggy;
    auto local=glm::conjugate(car.orientation())*glm::dvec3(0,crossYaw(arm,direction),0);
    return local.x*local.x/pitchInertia+local.y*local.y/yawInertia+local.z*local.z/rollInertia;
}
static glm::dvec2 contactVelocity(const Car& car,glm::dvec2 arm) {
    double angular=(car.orientation()*glm::dvec3(car.pitchRate,car.yawRate,car.rollRate)).y;
    return car.velocity+angular*glm::dvec2(arm.y,-arm.x);
}
void Car::contactImpulse(glm::dvec2 point,glm::dvec2 normal,double impulse) {
    using namespace infinity::buggy;
    auto arm=point-glm::dvec2(position.x,position.z);
    velocity+=normal*(impulse/mass);
    auto local=glm::conjugate(orientation())*glm::dvec3(0,crossYaw(arm,normal)*impulse,0);
    pitchRate+=local.x/pitchInertia;yawRate+=local.y/yawInertia;rollRate+=local.z/rollInertia;
}
static double inverseContactMass(const Car& car,glm::dvec3 point,glm::dvec3 direction) {
    using namespace infinity::buggy;
    auto local=glm::conjugate(car.orientation())*glm::cross(point-car.position,direction);
    return 1/mass+local.x*local.x/pitchInertia+local.y*local.y/yawInertia+local.z*local.z/rollInertia;
}
static glm::dvec3 velocityAt(const Car& car,glm::dvec3 point) {
    return glm::dvec3(car.velocity.x,car.verticalSpeed,car.velocity.y)+glm::cross(car.orientation()*glm::dvec3(car.pitchRate,car.yawRate,car.rollRate),point-car.position);
}
void Car::contactImpulse3D(glm::dvec3 point,glm::dvec3 n,double impulse) {
    using namespace infinity::buggy;
    auto force=n*impulse;velocity+=glm::dvec2(force.x,force.z)/mass;verticalSpeed+=force.y/mass;
    auto torque=glm::conjugate(orientation())*glm::cross(point-position,force);
    pitchRate+=torque.x/pitchInertia;yawRate+=torque.y/yawInertia;rollRate+=torque.z/rollInertia;
}
void Car::step(const Course& course,const Input& input,double dt) {
    using namespace infinity::buggy;
    if(dt<=0||!std::isfinite(dt)) return;
    if(dt>infinity::buggy::step+1e-9) {int count=int(std::ceil(std::min(dt,1.)/infinity::buggy::step));for(int i=0;i<count;++i) step(course,input,std::min(dt,1.)/count);return;}
    auto before=position;stepStartPosition=before;auto rotation=orientation();stepStartOrientation=rotation;hasStepMotion=true;
    auto f=forwardVector(yaw),r=rightVector(yaw);
    auto up=rotation*glm::dvec3(0,1,0);
    auto angular=rotation*glm::dvec3(pitchRate,yawRate,rollRate);
    glm::dvec3 linear(velocity.x,verticalSpeed,velocity.y);
    double u=glm::dot(velocity,f),velocityLength=glm::length(linear);
    hitAge+=dt;impactFlash*=std::exp(-dt*5);repairing=false;shiftTime=std::max(0.,shiftTime-dt);
    if(input.repair&&glm::length(velocity)<1.2&&grounded&&up.y>.5) repair(dt);else repairProgress=0;
    double pedal=repairing||damage.wrecked()?0:std::clamp(input.throttle,-1.,1.);
    throttlePedal=0;brakePedal=0;
    if(pedal<0) {if(u>.6) brakePedal=-pedal;else {if(gear!=-1) {gear=-1;shiftTime=.12;}throttlePedal=-pedal;}}
    else if(pedal>0) {if(u<-.6) brakePedal=pedal;else {if(gear<0) {gear=1;shiftTime=.12;}throttlePedal=pedal;}}
    if(repairing) brakePedal=1;
    double pull=(damage.alignment+(damage.tyres[0]-damage.tyres[1])*.12)*std::clamp(std::abs(u)/8.,0.,1.);
    double requested=std::clamp(input.steer+pull,-1.,1.);
    double alignmentTorque=-(wheels[0].lateralForce+wheels[1].lateralForce)*pneumaticTrail;
    double feedback=std::clamp(alignmentTorque/steeringStiffness,-.025,.025)*std::clamp(std::abs(u)/6.,0.,1.);
    steeringFeedback+=(feedback-steeringFeedback)*(1-std::exp(-dt*14));
    double limit=steeringLimit(),target=std::clamp(requested*limit+steeringFeedback,-.57,.57);
    // Exact critically damped rack response. Changing speed or terrain forces
    // changes the target angle, never the angle instantaneously.
    double blend=std::clamp(std::abs(u)/30.,0.,1.),frequency=std::lerp(26.,20.,blend);
    double error=steerAngle-target,c=steeringVelocity+frequency*error,decay=std::exp(-frequency*dt);
    double next=target+(error+c*dt)*decay,rate=std::lerp(3.6,1.8,blend);
    double change=std::clamp(next-steerAngle,-rate*dt,rate*dt);
    steeringVelocity=std::abs(next-steerAngle-change)>1e-10?change/dt:(steeringVelocity-frequency*c*dt)*decay;
    steerAngle=std::clamp(steerAngle+change,-.57,.57);steering=std::clamp(steerAngle/limit,-1.,1.);
    for(int i=0;i<4;++i) {
        wheels[i].steer=i<2 && std::abs(steerAngle)>.001?std::atan(wheelbase/(wheelbase/std::tan(steerAngle)-side(i)*track*.5)):0;
    }
    // Gear hysteresis and a finite torque interruption prevent instantaneous shifts.
    constexpr std::array<double,5> ratios={3.60,2.35,1.65,1.25,.98};
    double wheelRpm=0;for(auto& wheel:wheels) wheelRpm+=std::abs(wheel.angularSpeed)*.25*60/(2*std::numbers::pi);
    double ratio=(gear<0?3.2:ratios[gear-1])*4.5;
    double coupledRpm=wheelRpm*ratio;
    if(gear>0 && shiftTime==0) {
        if(coupledRpm>6200 && gear<5 && std::abs(u)>6200/ratio*(2*std::numbers::pi/60)*tyreRadius*.82) {++gear;shiftTime=.18;}
        else if(coupledRpm<2700 && gear>1) {--gear;shiftTime=.18;}
        ratio=ratios[gear-1]*4.5;coupledRpm=wheelRpm*ratio;
    }
    double desiredRpm=std::max(1050.+throttlePedal*1250,coupledRpm);
    rpm+=(std::clamp(desiredRpm,850.,7200.)-rpm)*(1-std::exp(-dt*12));
    boosting=input.boost&&throttlePedal>.1&&fuel>.03&&u>5&&!damage.wrecked()&&damage.engine>.2&&!repairing&&gear>0;
    fuel=std::clamp(fuel+dt*(boosting?-.25:.075),0.,1.);
    double torque=(330+110*std::exp(-std::pow((rpm-4300)/2100,2)))*(.28+.72*damage.engine);
    torque*=std::clamp((7100-rpm)/650.,0.,1.);
    double engineTorque=throttlePedal*torque*(shiftTime>0?.18:1.)*(boosting?1.48:1.);
    // Overrun is resistance, not a signed drive torque. A car rolling back in
    // a forward gear must not gain wheel energy from "engine braking".
    double overrun=throttlePedal<.05&&wheelRpm>20?std::min(62.,rpm*.012)*ratio*.90:0;
    if(overrun>0)engineTorque=0;
    if(damage.wrecked()) {engineTorque=overrun=0;rpm=0;boosting=false;}
    double driveTorque=engineTorque*ratio*.90*(gear<0?-1.:1.);
    // Reverse has a short ratio and a finite governor, rather than an arbitrary
    // velocity assignment. The wheels still have to transmit its torque.
    if(gear<0 && engineTorque>0) driveTorque*=std::clamp((11.+u)/3.,0.,1.);
    std::array<glm::dvec3,4> mounts{},normals{};
    std::array<double,4> rawCompression{},loads{};
    int oldContacts=groundContacts;groundContacts=0;
    double verticalBefore=verticalSpeed;
    for(int i=0;i<4;++i) {
        auto& wheel=wheels[i];
        mounts[i]=position+rotation*glm::dvec3(side(i)*track*.5,0,axle(i));
        double radius=tyreRadius*(.65+.35*damage.tyres[i]);
        auto patch=tyrePatch(course,mounts[i],rotation,wheel.steer,radius);
        auto contact=patch.contact;auto n=patch.normal;double distance=patch.distance;
        normals[i]=n;
        double compression=suspensionRest+radius-distance;
        // Damping follows contact-point velocity, including body rotation.
        // Clamped suspension travel is presentation state, not a derivative.
        auto contactVelocity=linear+glm::cross(angular,mounts[i]-position-up*distance);
        double rate=-glm::dot(contactVelocity,n)/std::max(.25,glm::dot(n,up));
        double spring=springRate*(.58+.42*damage.suspension);
        double damping=(rate>0?compressionDamping:reboundDamping)*(.48+.52*damage.suspension);
        // Digressive damping lets the tyres follow sharp crests; progressive
        // bump stops keep the chassis from falling through compressed springs.
        double engagement=std::clamp((compression-bumpStart)/(suspensionTravel-bumpStart),0.,1.);
        double damper=damping*rate/(1+std::abs(rate)*.18);
        if(rate>0)damper*=1.+engagement*engagement*.8;
        double bump=std::pow(std::max(0.,compression-bumpStart),2)*650000;
        wheel.contact=compression>0&&up.y>.18&&glm::dot(n,up)>.2;
        loads[i]=wheel.contact?std::clamp(spring*compression+damper+bump,0.,mass*gravity*3):0;
        rawCompression[i]=compression;
        wheel.compression=std::clamp(compression,0.,suspensionTravel);
        wheel.point=mounts[i]-up*(suspensionRest-wheel.compression);
        wheel.groundPoint=wheel.point-n*radius;
        wheel.soilContact=wheel.contact&&contact&&contact->material==0;
        auto soil=course.tracks.sample({float(wheel.groundPoint.x),float(wheel.groundPoint.z)});
        wheel.soilDepth=-soil.height;wheel.compaction=soil.compaction;wheel.shearing=0;
        if(wheel.contact) ++groundContacts;
    }
    for(int axleIndex:{0,2}) if(wheels[axleIndex].contact&&wheels[axleIndex+1].contact) {
        double antiRoll=(rawCompression[axleIndex]-rawCompression[axleIndex+1])*4500;
        antiRoll=std::clamp(antiRoll,-loads[axleIndex],loads[axleIndex+1]);
        loads[axleIndex]+=antiRoll;loads[axleIndex+1]-=antiRoll;
    }
    grounded=groundContacts>0;
    if(grounded&&oldContacts==0&&airtime>.12) impact(std::max(0.,-verticalBefore),-f,-1,true);
    if(grounded) airtime=0;else airtime+=dt;
    glm::dvec3 force(0,-mass*gravity,0),bodyTorque{};
    std::array<TyreContact,4> contacts;
    std::array<double,4> previousOmega{};for(int i=0;i<4;++i) previousOmega[i]=wheels[i].angularSpeed;
    for(int i=0;i<4;++i) {
        auto& wheel=wheels[i];auto& contact=contacts[i];
        contact.radius=tyreRadius*(.65+.35*damage.tyres[i]);contact.load=wheel.load=loads[i];
        double differential=std::clamp((previousOmega[i^1]-previousOmega[i])*8.,-260.,260.);
        double spin=std::max(0.,wheel.slipRatio*(gear<0?-1.:1.)-.16);
        double tractionAssist=wheel.contact&&throttlePedal>0?1/(1+spin*4.):1.;
        contact.driveTorque=driveTorque*(i<2?.15:.35)*tractionAssist+differential;
        contact.brakeTorque=brakePedal*(i<2?1500.:1050.)+(input.brake&&i>=2?2500.:0.)+overrun*(i<2?.15:.35);
        contact.longitudinalStiffness=48000*(.45+.55*damage.tyres[i]);
        contact.corneringStiffness=(i<2?23500.:25000.)*(.45+.55*damage.tyres[i]);
        if(!wheel.contact||contact.load<1)continue;
        auto n=normals[i];
        auto wf=rotation*(glm::angleAxis(wheel.steer,glm::dvec3(0,1,0))*glm::dvec3(0,0,1));
        contact.forward=glm::normalize(wf-n*glm::dot(wf,n));contact.right=glm::normalize(glm::cross(n,contact.forward));
        contact.arm=wheel.point-position-n*contact.radius;
        double mu=(.90+.06*wheel.compaction)*(.32+.68*damage.tyres[i]);
        double sliding=std::clamp((std::hypot(wheel.slipRatio*.8,wheel.slipAngle*1.3)-.22)/.8,0.,1.);
        contact.capacity=mu*contact.load*std::pow(mass*gravity*.25/std::max(300.,contact.load),.10)*(1.-sliding*.16);
        // Rolling resistance acts through the rotor and the contact force, so
        // it cannot stop the chassis independently of tyre grip or wheel spin.
        contact.brakeTorque+=(.035+.13*std::clamp(wheel.soilDepth/fractal::fr_rut_limit(),0.,1.))*contact.load*contact.radius;
        force+=n*contact.load;bodyTorque+=glm::cross(contact.arm,n*contact.load);
    }
    // Predict gravity, suspension and drag first. The coupled tyre solve then
    // sees this step's normal support and all other tyres' velocity changes.
    force-=linear*(.61*velocityLength);
    linear+=force*(dt/mass);
    auto localTorque=glm::conjugate(rotation)*bodyTorque;
    auto localAngular=glm::dvec3(pitchRate,yawRate,rollRate);
    auto gyroscopic=glm::cross(localAngular,localAngular*glm::dvec3(pitchInertia,yawInertia,rollInertia));
    localAngular+=((localTorque-gyroscopic)/glm::dvec3(pitchInertia,yawInertia,rollInertia)-localAngular*glm::dvec3(.18,.12,.18))*dt;
    if(!grounded) {localAngular.x-=pedal*.24*dt;localAngular.z-=input.steer*.16*dt;}
    solveTyres(*this,contacts,rotation,linear,localAngular,dt);
    pitchRate=localAngular.x;yawRate=localAngular.y;rollRate=localAngular.z;
    auto acceleration=(linear-glm::dvec3(velocity.x,verticalSpeed,velocity.y))/dt;
    longitudinalG=glm::dot(acceleration,glm::dvec3(f.x,0,f.y))/gravity;
    lateralG=glm::dot(acceleration,glm::dvec3(r.x,0,r.y))/gravity;
    slip=0;
    for(int i=0;i<4;++i) {
        auto& wheel=wheels[i];const auto& contact=contacts[i];
        wheel.rotation+=wheel.angularSpeed*dt;
        if(contact.load<1) {wheel.slipRatio=wheel.slipAngle=wheel.slip=wheel.longitudinalForce=wheel.lateralForce=0;continue;}
        auto atContact=linear+glm::cross(rotation*localAngular,contact.arm);
        double vx=glm::dot(atContact,contact.forward),vy=glm::dot(atContact,contact.right);
        wheel.slipRatio=(wheel.angularSpeed*contact.radius-vx)/std::max(2.5,std::abs(vx));
        wheel.slipAngle=std::atan2(vy,std::max(1.5,std::abs(vx)));
        wheel.shearing=(std::abs(wheel.angularSpeed*contact.radius-vx)+std::abs(vy)*.6)*dt;
        wheel.slip=std::min(2.,std::hypot(wheel.slipRatio*.6,wheel.slipAngle));
        wheel.longitudinalForce=contact.impulse.x/dt;wheel.lateralForce=contact.impulse.y/dt;slip+=wheel.slip*.25;
    }
    // Free body angles permit an actual rollover. No artificial spring rights
    // an airborne or inverted buggy; body/roof contacts below arrest the fall.
    double angularSpeed=glm::length(localAngular);
    auto integrated=angularSpeed>1e-9?glm::normalize(rotation*glm::angleAxis(angularSpeed*dt,localAngular/angularSpeed)):rotation;
    setOrientation(integrated);
    velocity={linear.x,linear.z};verticalSpeed=linear.y;position+=linear*dt;
    // Solve body contacts at every physics step, including rotational sweeps.
    collide(course,before);
    speed=glm::dot(velocity,forwardVector(yaw));travel+=glm::length(velocity)*dt;
    for(auto& wheel:wheels) {
        if(wheel.soilContact&&wheel.load>100) {
            glm::dvec2 point(wheel.groundPoint.x,wheel.groundPoint.z);
            double rolling=wheel.hasTrackPoint?glm::length(point-wheel.trackPoint):0;
            auto direction=rotation*glm::dvec3(std::sin(wheel.steer),0,std::cos(wheel.steer));
            course.tracks.stamp(wheel.hasTrackPoint?wheel.trackPoint:point,point,{direction.x,direction.z},.40,wheel.load,rolling,wheel.shearing);
            wheel.trackPoint=point;wheel.hasTrackPoint=true;
        } else wheel.hasTrackPoint=false;
    }
    if(boosting&&damage.engine<.35) damage.engine=std::max(0.,damage.engine-dt*.008);
}
}
