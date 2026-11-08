#include "Desert.hpp"
#include <algorithm>
#include <array>
#include <limits>

namespace desert {
namespace {
using V=glm::dvec3;using Q=glm::dquat;
namespace fr=infinity::fractal;
struct Box {V center,half;std::array<V,3> axis;};
Box box(const Car& car,V position,Q rotation) {
    auto field=car.fieldVehicle();
    return {position+rotation*V(fr::fr_block_center(field)),V(fr::fr_block_bounds(field)),
        {rotation*V(1,0,0),rotation*V(0,1,0),rotation*V(0,0,1)}};
}
V local(const Box& b,V p) {p-=b.center;return {glm::dot(p,b.axis[0]),glm::dot(p,b.axis[1]),glm::dot(p,b.axis[2])};}
V vector(const Box& b,V p) {return b.axis[0]*p.x+b.axis[1]*p.y+b.axis[2]*p.z;}
double radius(const Box& b,V axis) {return b.half.x*std::abs(glm::dot(b.axis[0],axis))+b.half.y*std::abs(glm::dot(b.axis[1],axis))+b.half.z*std::abs(glm::dot(b.axis[2],axis));}
double rotationTravel(Q a,Q b) {return 2*std::acos(std::clamp(std::abs(glm::dot(a,b)),0.,1.));}
V velocityAt(const Car& car,V point) {
    return V(car.velocity.x,car.verticalSpeed,car.velocity.y)+glm::cross(car.orientation()*V(car.pitchRate,car.yawRate,car.rollRate),point-car.position);
}
double inverseMass(const Car& car,V point,V direction) {
    using namespace infinity::buggy;
    V arm=glm::conjugate(car.orientation())*glm::cross(point-car.position,direction);
    return 1/mass+arm.x*arm.x/pitchInertia+arm.y*arm.y/yawInertia+arm.z*arm.z/rollInertia;
}
struct Contact {
    V point,normal;double depth=0,target=0,impulse=0;V friction{};
};
void reduce(std::vector<Contact>& contacts) {
    if(contacts.size()<=8)return;
    std::vector<Contact> chosen;
    auto deepest=std::max_element(contacts.begin(),contacts.end(),[](auto& a,auto& b){return a.depth<b.depth;});
    chosen.push_back(*deepest);contacts.erase(deepest);
    while(chosen.size()<8&&!contacts.empty()) {
        double best=-1;size_t index=0;
        for(size_t i=0;i<contacts.size();++i) {
            double spacing=1e20;
            for(auto& c:chosen)spacing=std::min(spacing,glm::dot(c.point-contacts[i].point,c.point-contacts[i].point)+.5*(1-glm::dot(c.normal,contacts[i].normal)));
            if(spacing>best){best=spacing;index=i;}
        }
        chosen.push_back(contacts[index]);contacts.erase(contacts.begin()+index);
    }
    contacts=std::move(chosen);
}
void solve(Car& a,Car* b,std::vector<Contact>& contacts,double friction) {
    if(contacts.empty())return;reduce(contacts);
    double impact=0;V impactNormal=contacts.front().normal;
    for(auto& c:contacts) {
        double closing=-glm::dot(velocityAt(a,c.point)-(b?velocityAt(*b,c.point):V(0)),c.normal);
        c.target=closing>2?closing*.06:0;
        if(closing>impact){impact=closing;impactNormal=c.normal;}
    }
    for(int iteration=0;iteration<10;++iteration)for(auto& c:contacts) {
        auto relative=velocityAt(a,c.point)-(b?velocityAt(*b,c.point):V(0));
        double k=inverseMass(a,c.point,c.normal)+(b?inverseMass(*b,c.point,c.normal):0);
        double next=std::max(0.,c.impulse+(c.target-glm::dot(relative,c.normal))/k);
        double delta=next-c.impulse;c.impulse=next;
        a.contactImpulse3D(c.point,c.normal,delta);if(b)b->contactImpulse3D(c.point,c.normal,-delta);
        relative=velocityAt(a,c.point)-(b?velocityAt(*b,c.point):V(0));
        V tangent=relative-c.normal*glm::dot(relative,c.normal);double speed=glm::length(tangent);
        if(speed<1e-8)continue;tangent/=speed;
        k=inverseMass(a,c.point,tangent)+(b?inverseMass(*b,c.point,tangent):0);
        V accumulated=c.friction-tangent*(speed/k);double length=glm::length(accumulated),limit=friction*c.impulse;
        if(length>limit)accumulated*=limit/length;
        V change=accumulated-c.friction;c.friction=accumulated;
        double magnitude=glm::length(change);
        if(magnitude>1e-9){a.contactImpulse3D(c.point,change/magnitude,magnitude);if(b)b->contactImpulse3D(c.point,change/magnitude,-magnitude);}
    }
    // Split penetration correction changes position, never kinetic energy.
    V correction(0);
    for(int iteration=0;iteration<4;++iteration)for(auto& c:contacts) {
        double residual=c.depth-.001-glm::dot(correction,c.normal);
        if(residual>0)correction+=c.normal*(residual*.35);
    }
    double length=glm::length(correction);if(length>.20)correction*=.20/length;
    a.position+=correction*(b?.5:1.);if(b)b->position-=correction*.5;
    a.impact(impact*(b?.65:1.),{impactNormal.x,impactNormal.z},-1,!b&&impactNormal.y>.6);
    if(b)b->impact(impact*.65,{-impactNormal.x,-impactNormal.z});
}
struct Separation {double gap=-1e20;V normal{0,1,0};int axis=0;};
Separation separate(const Box& a,const Box& b) {
    Separation result;int index=0;V offset=a.center-b.center;
    auto axis=[&](V n) {
        int current=index++;double length=glm::length(n);if(length<1e-7)return;n/=length;
        double signedDistance=glm::dot(offset,n),gap=std::abs(signedDistance)-radius(a,n)-radius(b,n);
        if(gap>result.gap)result={gap,signedDistance<0?-n:n,current};
    };
    for(auto n:a.axis)axis(n);for(auto n:b.axis)axis(n);
    for(auto x:a.axis)for(auto y:b.axis)axis(glm::cross(x,y));
    return result;
}
std::vector<V> clip(const std::vector<V>& polygon,V normal,double plane) {
    std::vector<V> out;if(polygon.empty())return out;
    V previous=polygon.back();double d0=glm::dot(previous,normal)-plane;
    for(V current:polygon) {
        double d1=glm::dot(current,normal)-plane;
        if((d0<=0)!=(d1<=0))out.push_back(previous+(current-previous)*(d0/(d0-d1)));
        if(d1<=0)out.push_back(current);previous=current;d0=d1;
    }
    return out;
}
std::pair<V,V> closestSegments(V p,V u,V q,V v) {
    V r=p-q;double a=glm::dot(u,u),b=glm::dot(u,v),c=glm::dot(v,v),d=glm::dot(u,r),e=glm::dot(v,r),denom=a*c-b*b;
    double s=denom>1e-12?std::clamp((b*e-c*d)/denom,0.,1.):0;
    double t=c>1e-12?std::clamp((b*s+e)/c,0.,1.):0;
    if(a>1e-12)s=std::clamp((b*t-d)/a,0.,1.);
    if(c>1e-12)t=std::clamp((b*s+e)/c,0.,1.);
    return {p+u*s,q+v*t};
}
std::vector<Contact> manifold(const Box& a,const Box& b,Separation s) {
    std::vector<Contact> out;
    if(s.axis<6) {
        bool referenceA=s.axis<3;const Box& ref=referenceA?a:b;const Box& inc=referenceA?b:a;
        int face=s.axis%3;V normal=referenceA?-s.normal:s.normal;
        V center=ref.center+ref.axis[face]*ref.half[face]*(glm::dot(ref.axis[face],normal)<0?-1.:1.);
        int incident=0;for(int i=1;i<3;++i)if(std::abs(glm::dot(inc.axis[i],normal))>std::abs(glm::dot(inc.axis[incident],normal)))incident=i;
        V ic=inc.center+inc.axis[incident]*inc.half[incident]*(glm::dot(inc.axis[incident],normal)>0?-1.:1.);
        int u=(incident+1)%3,v=(incident+2)%3;
        std::vector<V> polygon={ic+inc.axis[u]*inc.half[u]+inc.axis[v]*inc.half[v],ic-inc.axis[u]*inc.half[u]+inc.axis[v]*inc.half[v],ic-inc.axis[u]*inc.half[u]-inc.axis[v]*inc.half[v],ic+inc.axis[u]*inc.half[u]-inc.axis[v]*inc.half[v]};
        for(int i=0;i<3;++i)if(i!=face)for(double side:{-1.,1.}) {
            V n=ref.axis[i]*side;polygon=clip(polygon,n,glm::dot(center,n)+ref.half[i]+.001);
        }
        for(V p:polygon){double depth=glm::dot(center-p,normal);if(depth>=-.004)out.push_back({p+normal*(depth*.5),s.normal,std::max(0.,depth)});}
    } else {
        int ai=(s.axis-6)/3,bi=(s.axis-6)%3;V ac=a.center,bc=b.center;
        for(int i=0;i<3;++i){if(i!=ai)ac+=a.axis[i]*a.half[i]*(glm::dot(a.axis[i],-s.normal)<0?-1.:1.);if(i!=bi)bc+=b.axis[i]*b.half[i]*(glm::dot(b.axis[i],s.normal)<0?-1.:1.);}
        auto [p,q]=closestSegments(ac-a.axis[ai]*a.half[ai],a.axis[ai]*a.half[ai]*2.,bc-b.axis[bi]*b.half[bi],b.axis[bi]*b.half[bi]*2.);
        out.push_back({(p+q)*.5,s.normal,std::max(0.,-s.gap)});
    }
    return out;
}
struct PoleContact {double gap;V point,normal;};
PoleContact poleContact(const Box& b,V start,V end) {
    constexpr double r=.14;V p=local(b,start),d=local(b,end)-p;
    std::vector<double> breaks={0,1};
    for(int i=0;i<3;++i)if(std::abs(d[i])>1e-12)for(double side:{-1.,1.}) {double t=(side*b.half[i]-p[i])/d[i];if(t>0&&t<1)breaks.push_back(t);}
    std::sort(breaks.begin(),breaks.end());double best=1e20,bestT=0;
    auto candidate=[&](double t){V q=p+d*t,delta=q-glm::clamp(q,-b.half,b.half);double dist=glm::dot(delta,delta);if(dist<best){best=dist;bestT=t;}};
    for(size_t i=1;i<breaks.size();++i) {
        double lo=breaks[i-1],hi=breaks[i],mid=(lo+hi)*.5,numerator=0,denominator=0;
        for(int j=0;j<3;++j){double q=p[j]+d[j]*mid,bound=std::clamp(q,-b.half[j],b.half[j]);if(q==bound)continue;numerator+=d[j]*(p[j]-bound);denominator+=d[j]*d[j];}
        candidate(lo);candidate(hi);if(denominator>1e-12)candidate(std::clamp(-numerator/denominator,lo,hi));
    }
    V q=p+d*bestT,surface=glm::clamp(q,-b.half,b.half),n;double distance=std::sqrt(best),gap=distance-r;
    if(distance>1e-8)n=(surface-q)/distance;
    else {
        double depth=1e20;V minimum=glm::min(p,p+d),maximum=glm::max(p,p+d);
        for(int i=0;i<3;++i)for(double side:{-1.,1.}){
            double push=b.half[i]+r+(side>0?maximum[i]:-minimum[i]);
            if(push<depth){depth=push;n=V(0);n[i]=side;}
        }
        gap=-depth;surface=glm::clamp(q,-b.half,b.half);
        for(int i=0;i<3;++i)if(std::abs(n[i])>.5)surface[i]=-n[i]*b.half[i];
    }
    return {gap,b.center+vector(b,surface),vector(b,n)};
}
PoleContact vehiclePoleContact(const Car& car,V position,Q rotation,V start,V end) {
    auto result=poleContact(box(car,position,rotation),start,end);auto field=car.fieldVehicle();
    for(int wheel=0;wheel<4;++wheel) {
        V center=position+rotation*V(fr::fr_wheel_center(field,wheel));double r=fr::fr_wheel_radius(field,wheel);
        double t=std::clamp(glm::dot(center-start,end-start)/glm::dot(end-start,end-start),0.,1.);
        if(glm::length(start+(end-start)*t-center)-std::hypot(r,.20)-.14>result.gap)continue;
        Q wheelRotation=rotation*glm::angleAxis(wheel<2?double(field.steering[wheel]):0.,V(0,1,0));
        V p=glm::conjugate(wheelRotation)*(start-center),d=glm::conjugate(wheelRotation)*(end-start);
        auto distance=[&](double x){return double(fr::fr_cylinder_x(glm::vec3(p+d*x),float(r),.20f));};
        // Minimize distance along the capsule axis against the actual capped
        // cylinder, including its end faces. The convex distance has one basin.
        double lo=0,hi=1;
        for(int iteration=0;iteration<32;++iteration){double u=(2*lo+hi)/3,v=(lo+2*hi)/3;if(distance(u)<distance(v))hi=v;else lo=u;}
        t=(lo+hi)*.5;double sd=distance(t);if(distance(0)<sd){t=0;sd=distance(0);}if(distance(1)<sd){t=1;sd=distance(1);}
        if(sd-.14>=result.gap)continue;
        glm::vec3 q(p+d*t),gradient;
        for(int i=0;i<3;++i){auto a=q,b=q;a[i]+=.0001f;b[i]-=.0001f;gradient[i]=fr::fr_cylinder_x(a,float(r),.20f)-fr::fr_cylinder_x(b,float(r),.20f);}
        V n=glm::length(gradient)>1e-7?V(glm::normalize(gradient)):V(1,0,0);n=wheelRotation*n;
        result={sd-.14,start+(end-start)*t-n*sd,-n};
    }
    return result;
}
void wheelContacts(const Car& source,const Car& target,bool reverse,std::vector<Contact>& contacts) {
    auto vehicle=target.fieldVehicle(),from=source.fieldVehicle();Q rotation=source.orientation();
    for(int wheel=0;wheel<4;++wheel) {
        V center=fr::fr_wheel_center(from,wheel);double r=fr::fr_wheel_radius(from,wheel);
        Q steer=glm::angleAxis(wheel<2?double(from.steering[wheel]):0.,V(0,1,0));
        for(double cap:{-.20,.20})for(int sample=0;sample<12;++sample) {
            double a=sample*6.283185307179586/12;V p=source.position+rotation*(center+steer*V(cap,std::sin(a)*r,std::cos(a)*r));
            float d=fr::fr_vehicle(glm::vec3(p),vehicle,true,true).distance;if(d>=.0015f)continue;
            glm::vec3 gradient;for(int i=0;i<3;++i){auto u=glm::vec3(p),v=u;u[i]+=.001f;v[i]-=.001f;gradient[i]=fr::fr_vehicle(u,vehicle,true,true).distance-fr::fr_vehicle(v,vehicle,true,true).distance;}
            if(glm::length(gradient)<1e-7)continue;
            contacts.push_back({p,V(glm::normalize(gradient))*(reverse?-1.:1.),std::max(0.,-double(d)/.72)});
        }
    }
}
} // namespace

void resolveContact(Car& a,Car& b) {
    V relative=(a.position-a.stepStartPosition)-(b.position-b.stepStartPosition);
    double travel=a.hasStepMotion&&b.hasStepMotion?glm::length(relative):0;
    if(glm::length(a.position-b.position)>5.2+travel)return;
    Box ba=box(a,a.position,a.orientation()),bb=box(b,b.position,b.orientation());
    Separation separation=separate(ba,bb);bool swept=false;
    if(separation.gap>.003&&travel>.05) {
        Q aq=a.orientation(),bq=b.orientation();double bound=travel+2.2*(rotationTravel(a.stepStartOrientation,aq)+rotationTravel(b.stepStartOrientation,bq));
        double t=0;Separation start=separate(box(a,a.stepStartPosition,a.stepStartOrientation),box(b,b.stepStartPosition,b.stepStartOrientation));
        if(start.gap>.003)for(int iteration=0;iteration<32&&t<=1;++iteration) {
            ba=box(a,glm::mix(a.stepStartPosition,a.position,t),glm::slerp(a.stepStartOrientation,aq,t));bb=box(b,glm::mix(b.stepStartPosition,b.position,t),glm::slerp(b.stepStartOrientation,bq,t));
            separation=separate(ba,bb);
            if(separation.gap<.002) {
                a.position=glm::mix(a.stepStartPosition,a.position,t);b.position=glm::mix(b.stepStartPosition,b.position,t);
                a.setOrientation(glm::slerp(a.stepStartOrientation,aq,t));b.setOrientation(glm::slerp(b.stepStartOrientation,bq,t));swept=true;break;
            }
            t+=std::max(.00001,(separation.gap-.001)*.85/std::max(.001,bound));
        }
    }
    std::vector<Contact> contacts;
    if(separation.gap<=.003)contacts=manifold(ba,bb,separation);
    if(!swept&&glm::length(a.position-b.position)<5.2){wheelContacts(a,b,false,contacts);wheelContacts(b,a,true,contacts);}
    solve(a,&b,contacts,.65);
    a.speed=glm::dot(a.velocity,glm::dvec2(std::sin(a.yaw),std::cos(a.yaw)));
    b.speed=glm::dot(b.velocity,glm::dvec2(std::sin(b.yaw),std::cos(b.yaw)));
}
void Car::collide(const Course& course,glm::dvec3 before) {
    auto world=course.field();Q current=orientation(),previous=stepStartOrientation;
    double motion=glm::length(position-before)+2.2*rotationTravel(previous,current);
    if(fr::fr_environment(glm::vec3(position),world).distance>2.4+motion)return;
    // Rebuild the small exact surface set so repairs and local dents never leave
    // an obsolete collision hull behind the visible block.
    collisionSamples=fr::vehicleSamples(fieldVehicle());
    double time=1;int segments=std::clamp(int(std::ceil(rotationTravel(previous,current)/.10)),1,32);
    for(V sample:collisionSamples) {
        V start=before+previous*sample;float initial=fr::fr_environment(glm::vec3(start),world).distance;
        for(int segment=1;segment<=segments;++segment) {
            double t=double(segment)/segments;V point=glm::mix(before,position,t)+glm::slerp(previous,current,t)*sample;
            V delta=point-start;double distance=glm::length(delta);float finish=fr::fr_environment(glm::vec3(point),world).distance;
            if(distance>.001&&initial>.003f&&std::min(initial,finish)<distance+.005)if(auto hit=fr::cast(world,glm::vec3(start),glm::vec3(delta/distance),float(distance),.0007f))time=std::min(time,(segment-1+hit->distance/distance)/segments);
            start=point;initial=finish;
        }
    }
    std::vector<std::pair<V,V>> poles;
    for(auto obstacle:course.obstaclesNear({position.x,position.z})) {
        V p(obstacle.center.x,obstacle.base,obstacle.center.y),end=p+V(0,3.2,0);
        if(std::abs(p.x-position.x)>3+motion||std::abs(p.z-position.z)>3+motion)continue;
        V foot=p-V(0,fr::fr_rut_limit(),0);poles.push_back({foot,end});
        auto start=vehiclePoleContact(*this,before,previous,foot,end);if(start.gap<=.003||motion<.001)continue;
        double t=0;
        for(int iteration=0;iteration<32&&t<=time;++iteration) {
            auto c=vehiclePoleContact(*this,glm::mix(before,position,t),glm::slerp(previous,current,t),foot,end);
            if(c.gap<.002){time=std::min(time,t);break;}
            t+=std::max(.00001,(c.gap-.001)*.85/motion);
        }
    }
    if(time<1){position=glm::mix(before,position,time);setOrientation(glm::slerp(previous,current,time));current=orientation();}
    std::vector<Contact> contacts;
    for(V sample:collisionSamples) {
        V point=position+current*sample;auto hit=fr::fr_environment(glm::vec3(point),world);if(hit.distance>.004f)continue;
        V n=fr::normal(world,glm::vec3(point));double depth=.002-fr::signedDistance(world,glm::vec3(point));
        contacts.push_back({point,n,std::max(0.,depth)});
    }
    for(auto [p,end]:poles) {
        auto c=vehiclePoleContact(*this,position,current,p,end);if(c.gap<.004)contacts.push_back({c.point,c.normal,std::max(0.,.002-c.gap)});
    }
    solve(*this,nullptr,contacts,.55);
}
} // namespace desert
