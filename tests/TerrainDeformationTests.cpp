#include "infinity/Fractal.hpp"
#include "Desert.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace infinity::fractal;
static int checks=0;
void check(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
void lane(Deformation& field,double x,double z,double length=8,double load=2400,double shear=0) {
    for(int i=0;i<int(length/.05);++i)field.stamp({x,z+i*.05},{x,z+(i+1)*.05},{0,1},.4,load,.05,shear);
}
void groove(Deformation& field,glm::dvec2 start,glm::dvec2 end) {
    auto delta=end-start;int count=int(glm::length(delta)/.05);
    for(int pass=0;pass<12;++pass)for(int i=0;i<count;++i)field.stamp(start+delta*(double(i)/count),start+delta*(double(i+1)/count),glm::normalize(delta),.4,3000,.05,.02);
}
void sweep(Deformation& field,glm::dvec2 start,glm::dvec2 end,int pieces,double shear=0) {
    auto delta=end-start;double distance=glm::length(delta)/pieces;
    for(int i=0;i<pieces;++i)field.stamp(start+delta*(double(i)/pieces),start+delta*(double(i+1)/pieces),glm::normalize(delta),.4,2400,distance,distance*shear);
}
int main()try {
    check(std::abs(fr_rut_limit()-.32)<1e-7,"rut depth limit is thirty-two centimetres");
    for(double angle:{0.,.73,1.8})for(double shear:{0.,.6}) {
        glm::dvec2 forward(std::sin(angle),std::cos(angle)),right(forward.y,-forward.x),start(-4.13,-3.71),end=start+forward*8.;
        Deformation fine,coarse;sweep(fine,start,end,400,shear);sweep(coarse,start,end,8,shear);
        for(int i=0;i<=80;++i)for(double offset:{-.3,-.12,0.,.12,.3}){
            glm::vec2 point=start+forward*(i*.1)+right*offset;auto a=fine.sample(point),b=coarse.sample(point);
            check(std::abs(a.height-b.height)<2e-6&&std::abs(a.compaction-b.compaction)<2e-6,"distance-integrated digging and compaction do not depend on segment length or direction");
            check(glm::length(a.gradient-b.gradient)<.0001,"subdividing a tyre sweep preserves its physical sidewall normals");
        }
    }
    Deformation slowSpin,fastSpin;
    for(int i=0;i<20;++i)slowSpin.stamp({0,0},{0,0},{0,1},.4,2400,0,.05);
    for(int i=0;i<200;++i)fastSpin.stamp({0,0},{0,0},{0,1},.4,2400,0,.005);
    check(std::abs(slowSpin.sample({0,0}).height-fastSpin.sample({0,0}).height)<1e-6,"stationary wheelspin excavation depends on slip distance rather than update count");
    Deformation saturated;saturated.stamp({0,0},{0,0},{0,1},.4,2400,0,1e6);auto saturatedRevision=saturated.revision();
    saturated.stamp({0,0},{0,0},{0,1},.4,2400,0,1e6);check(saturated.revision()==saturatedRevision,"unchanged saturated soil does not dirty tiles or trigger GPU uploads");
    auto invalidRevision=saturated.revision();saturated.stamp({0,0},{0,0},{0,1},.4,std::numeric_limits<double>::quiet_NaN(),0,1);
    saturated.stamp({0,0},{3,0},{0,1},.4,2400,3,0);saturated.stamp({0,0},{0,0},{0,1},.4,2400,-1,1);
    check(saturated.revision()==invalidRevision,"invalid loads, backwards travel distances and recovery jumps do not deform terrain");
    for(int i=0;i<=100;++i){auto w=fr_rut_weights(i/100.f),d=fr_rut_derivatives(i/100.f);check(std::abs(w.x+w.y+w.z+w.w-1)<1e-6,"spline weights conserve constant height");check(std::abs(d.x+d.y+d.z+d.w)<1e-6,"spline derivatives sum to zero");check(glm::all(glm::greaterThanEqual(w,glm::vec4(0))),"interpolation cannot raise the terrain");}
    for(int pattern=0;pattern<24;++pattern) {
        glm::vec4 rows[4];for(int z=0;z<4;++z)for(int x=0;x<4;++x)rows[z][x]=fr_random(pattern*917+z*4+x)*fr_rut_limit();
        float bound=fr_rut_cell_curvature(rows[0],rows[1],rows[2],rows[3]);
        check(bound<=fr_rut_curvature(),"local curvature stays inside the global deformation bound");
        auto gradient=[&](glm::vec2 f){RutSample s{0,glm::vec2(0),0};auto wx=fr_rut_weights(f.x),wz=fr_rut_weights(f.y),dx=fr_rut_derivatives(f.x),dz=fr_rut_derivatives(f.y);for(int z=0;z<4;++z)for(int x=0;x<4;++x)s=fr_rut_accumulate(s,{rows[z][x],0},wx[x],wz[z],dx[x],dz[z]);return s.gradient;};
        for(int i=0;i<12;++i){glm::vec2 f(.1f+i*.067f,.2f+i*.053f);float e=.001f;auto x=(gradient(f+glm::vec2(e,0))-gradient(f-glm::vec2(e,0)))/(2*e*fr_rut_cell()),z=(gradient(f+glm::vec2(0,e))-gradient(f-glm::vec2(0,e)))/(2*e*fr_rut_cell());check(std::max(std::abs(x.x)+std::abs(z.x),std::abs(x.y)+std::abs(z.y))<bound+.005,"local ray-step bound contains independently differenced groove curvature");}
    }
    Deformation soil;soil.stamp({0,0},{0,1},{0,1},.4,0,1,10);check(soil.tileCount()==0,"unloaded wheels leave no track");
    soil.stamp({0,0},{0,0},{0,1},.4,2400,0,0);check(soil.tileCount()==0,"stationary non-spinning tyres do not excavate endlessly");
    lane(soil,-4,-5,12);auto rut=soil.sample({-4,1});
    check(rut.height<-.01&&rut.height>-.10,"rolling wheels create a shallow physical groove");
    check(rut.compaction>.2,"rolling wheels visibly compact sand");check(soil.sample({-3,1}).height==0,"deformation stays near the tyre path");
    double first=rut.height;lane(soil,-4,-5,12);check(soil.sample({-4,1}).height<first,"repeated passes deepen existing ruts");
    Deformation light,heavy,spun;lane(light,0,0,8,600);lane(heavy,0,0,8,4000);lane(spun,0,0,8,2400,.06);
    check(heavy.sample({0,4}).height<light.sample({0,4}).height,"loaded tyres cut deeper");check(spun.sample({0,4}).height<soil.sample({-4,1}).height,"sliding or spinning tyres excavate more than rolling");
    auto before=soil.sample({-4,1});lane(soil,600,2500);auto after=soil.sample({-4,1});check(before.height==after.height&&before.compaction==after.compaction,"distant driving does not erase previous tracks");
    World original{.7f,42},deformed{.7f,42,&soil};glm::vec2 p(-4,1);float base=fr_height(p,original),actual=fr_height(p,deformed);
    check(std::abs(actual-base-soil.sample(p).height)<2e-6,"rendered mathematical surface includes actual deformation");
    auto contact=cast(deformed,{p.x,base+1,p.y},{0,-1,0},2);
    check(contact&&std::abs(contact->point.y-actual)<.008,"collision ray finds the lowered rut floor");
    Deformation deep;for(double offset:{-.15,0.,.15})for(int pass=0;pass<40;++pass)lane(deep,offset,-1,4,4000,.04);
    check(deep.sample({0,1}).height<-.28&&deep.sample({0,1}).height>=-fr_rut_limit(),"repeated loaded passes form physically deep thirty-centimetre channels");
    World deepWorld{.7f,42,&deep};
    Deformation poleSoil;World dugPole{.7f,42,&poleSoil};auto pole=fr_checkpoint_post(1,1,dugPole);
    poleSoil.stamp({pole.x,pole.z},{pole.x,pole.z},{0,1},.4,2400,0,1e6);
    auto exposed=fr_environment(pole-glm::vec3(0,.20,0),dugPole);
    check(exposed.material==15&&exposed.distance<0,"checkpoint foundations remain solid when deeper excavation exposes their bases");
    for(int i=0;i<40;++i){glm::vec2 xz(-.6f+i*.03f,1.3f);float h=fr_height(xz,deepWorld);auto hit=cast(deepWorld,{xz.x,h+.7f,xz.y},{0,-1,0},1.5f,.0001f);check(hit&&std::abs(hit->point.y-h)<.002,"collision rays reach deep floors and steep groove shoulders");}
    for(double side:{-1.,1.}) {
        glm::vec3 origin(0,fr_height({0,1.3f},deepWorld)+.04f,1.3f),end(side*.7,fr_height({float(side*.7),1.3f},deepWorld)-.15f,1.3f);
        auto delta=end-origin;float maximum=glm::length(delta);auto direction=delta/maximum;
        float low=0,high=0;
        for(int i=1;i<=2000;++i){high=maximum*i/2000;auto point=origin+direction*high;if(point.y<=fr_height({point.x,point.z},deepWorld))break;low=high;}
        for(int i=0;i<24;++i){float mid=(low+high)*.5;auto point=origin+direction*mid;if(point.y>fr_height({point.x,point.z},deepWorld))low=mid;else high=mid;}
        auto hit=cast(deepWorld,origin,direction,maximum,.0001f);
        check(hit&&std::abs(hit->distance-high)<.003,"slanted shoulder collision agrees with an independent dense-bracketed surface intersection");
    }
    for(int i=0;i<300;++i){glm::vec2 q(-4.4f+i*.0027f,-4.1f+i*.0281f);auto s=soil.sample(q);float e=.001f;
        glm::vec2 numeric((soil.sample(q+glm::vec2(e,0)).height-soil.sample(q-glm::vec2(e,0)).height)/(2*e),(soil.sample(q+glm::vec2(0,e)).height-soil.sample(q-glm::vec2(0,e)).height)/(2*e));
        check(glm::length(numeric-s.gradient)<.002,"rut normals match the deformed surface across tile boundaries");check(s.height<=.000001f&&s.height>=-fr_rut_limit()-.000001f,"deformation respects the geometric depth bound");check(s.compaction>=0&&s.compaction<=1.000001f,"compaction remains bounded");
        auto g=fr_terrain_sample(q,deformed),g0=fr_terrain_sample(q,original);check(glm::length(g.gradient-g0.gradient-s.gradient)<1e-6,"terrain and rut derivatives use the same field");
    }
    std::array<glm::vec2,Deformation::tileSize*Deformation::tileSize> tile;soil.copyTile(-1,-1,tile);
    check(std::any_of(tile.begin(),tile.end(),[](auto n){return n.x>0;}),"negative-coordinate tiles export deformed nodes for the GPU");
    auto rev=soil.revision();soil.clear();check(soil.tileCount()==0&&soil.revision()>rev&&fr_height(p,deformed)==base,"reset clears deformation and invalidates the GPU cache");
    desert::Race race(42,desert::RaceFormat::Stage);lane(race.course.tracks,1,1);auto revision=race.course.tracks.revision();race.recover();check(race.course.tracks.revision()==revision,"recovery preserves existing terrain damage");race.start();check(race.course.tracks.tileCount()==0,"restart starts a fresh desert surface");
    desert::Course pristine(42),crossed(42);desert::Car fixture;fixture.reset(pristine);
    glm::dvec2 forward(std::sin(fixture.yaw),std::cos(fixture.yaw)),right(forward.y,-forward.x),origin(fixture.position.x,fixture.position.z);
    // A diagonal groove staggers left/right contact even in a flat basin;
    // roll and steering effects must not depend on incidental base roughness.
    groove(crossed.tracks,origin+forward*1.-right*4.,origin+forward*9.+right*4.);
    desert::Car a=fixture,b=fixture;a.velocity=b.velocity=forward*7.;a.speed=b.speed=7;
    for(auto* car:{&a,&b})for(auto& wheel:car->wheels)wheel.angularSpeed=7./infinity::buggy::tyreRadius;
    double heightChange=0,pitchChange=0,rollChange=0,loadChange=0,headingChange=0,rackChange=0;
    for(int i=0;i<240;++i) {
        a.step(pristine,{.25,0,false,false},1./120.);b.step(crossed,{.25,0,false,false},1./120.);
        heightChange=std::max(heightChange,std::abs(a.position.y-b.position.y));pitchChange=std::max(pitchChange,std::abs(a.pitch-b.pitch));rollChange=std::max(rollChange,std::abs(a.roll-b.roll));
        headingChange=std::max(headingChange,std::abs(a.yaw-b.yaw));rackChange=std::max(rackChange,std::abs(a.steerAngle-b.steerAngle));
        double la=0,lb=0;for(int wheel=0;wheel<4;++wheel){la+=a.wheels[wheel].load;lb+=b.wheels[wheel].load;}loadChange=std::max(loadChange,std::abs(la-lb));
    }
    std::cout<<"RUT_VEHICLE height="<<heightChange<<" pitch="<<pitchChange<<" roll="<<rollChange<<" load="<<loadChange<<" yaw="<<headingChange<<" rack="<<rackChange<<'\n';
    check(heightChange>.006&&pitchChange>.004&&rollChange>.003,"identical driven cars change height, pitch and roll when crossing a physical rut");
    check(loadChange>800,"rut crossing changes actual suspension loads");
    check(headingChange>.002&&rackChange>.0001,"rut forces affect both heading and the self-aligning steering rack with identical input");
    desert::Course untouched(42),channels(42);fixture.reset(untouched);forward={std::sin(fixture.yaw),std::cos(fixture.yaw)};right={forward.y,-forward.x};origin={fixture.position.x,fixture.position.z};
    for(double side:{-1.,1.})for(double offset:{-.2,0.,.2}){auto line=origin+right*(side*infinity::buggy::track*.5+offset);groove(channels.tracks,line-forward*4.,line+forward*25.);}
    a.reset(untouched);b.reset(channels);
    check(a.position.y-b.position.y>.05,"reset settles the vehicle from all four deformed wheel positions");
    desert::Course settleCourse(42);for(double side:{-1.,1.}){auto line=origin+right*(side*infinity::buggy::track*.5);groove(settleCourse.tracks,line-forward*4.,line+forward*25.);}
    desert::Car settled;settled.reset(settleCourse);double settlingSpeed=0;
    for(int i=0;i<12;++i){settled.step(settleCourse,{0,0,true,false},1./120.);settlingSpeed=std::max(settlingSpeed,std::abs(settled.verticalSpeed));}
    check(settlingSpeed<.3,"fitting the actual tyre shoulders prevents a recovery launch from narrow grooves");
    a.velocity=b.velocity=forward*7.;a.speed=b.speed=7;for(auto* car:{&a,&b})for(auto& wheel:car->wheels)wheel.angularSpeed=7./infinity::buggy::tyreRadius;
    for(int i=0;i<240;++i){a.step(untouched,{},1./120.);b.step(channels,{},1./120.);}
    check(a.travel-b.travel>.1,"deep pre-existing ruts increase real rolling drag during a coasting run");
    std::cout<<"RUT_COAST clean="<<a.travel<<" rutted="<<b.travel<<'\n';
    desert::Race damaged(42,desert::RaceFormat::Stage);damaged.player.damage.tyres[0]=.1;damaged.player.damage.suspension=.4;damaged.recover();
    desert::Car healthy;healthy.reset(damaged.course,3);
    check(healthy.position.y-damaged.player.position.y>.025,"recovery preserves the lower ride height of damaged suspension and tyres");
    check(damaged.player.roll-healthy.roll>.01,"a punctured left front tyre changes the recovered vehicle's support plane");
    desert::Course airborne(42);desert::Car car;car.reset(airborne);car.position.y+=40;
    for(int i=0;i<60;++i)car.step(airborne,{1,0,false,false},1./120.);
    check(airborne.tracks.tileCount()==0,"airborne wheelspin cannot mark or deform the ground");
    std::cout<<"PASS "<<checks<<" persistent terrain deformation checks\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
