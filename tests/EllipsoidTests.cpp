#include "infinity/Ellipsoid.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace infinity::ellipsoid;
static int checks=0;
static void check(bool pass,const char* message) {++checks;if(!pass) throw std::runtime_error(message);}
static void close(double a,double b,double tolerance,const char* message) {check(std::abs(a-b)<tolerance,message);}
static void ppm(const Rasterizer& r,const std::filesystem::path& p) {
    std::ofstream f(p,std::ios::binary);f<<"P6\n"<<r.width()<<' '<<r.height()<<"\n255\n";
    for(uint32_t c:r.pixels()) {char bytes[3]={char(c&255),char((c>>8)&255),char((c>>16)&255)};f.write(bytes,3);}
}
int main(int argc,char** argv) try {
    Ellipsoid sphere=volume({0,0,5},{1,1,1},Material::Ochre);
    auto hit=intersect(sphere,{0,0,0},{0,0,1});check(bool(hit),"sphere hit");close(hit->t,4,1e-10,"near root");close(hit->normal.z,-1,1e-10,"front normal");
    hit=intersect(sphere,{0,0,5},{0,0,2});check(bool(hit),"inside volume selects far root");close(hit->t,.5,1e-10,"unnormalised ray parameter");close(hit->normal.z,1,1e-10,"inside far outward normal");
    check(!intersect(sphere,{0,0,0},{1,0,0}),"miss returns no hit");
    hit=intersect(sphere,{1,0,0},{0,0,1});check(bool(hit),"tangent hit");close(hit->t,5,1e-8,"tangent depth");
    auto elongated=volume({0,0,7},{3,1,.5},Material::Rust,glm::angleAxis(glm::radians(90.),Vec(0,1,0)));
    hit=intersect(elongated,{0,0,0},{0,0,1});check(bool(hit),"rotated anisotropic primitive");close(hit->t,4,1e-9,"rotation transforms long axis");
    for(int i=0;i<50;++i) {
        auto e=volume({.2,.1,5},{1.7,.8,.55},Material::Teal,glm::angleAxis(i*.07,glm::normalize(Vec(.2,1,.4))));
        auto h=intersect(e,{0,0,0},{.03,.02,1});check(bool(h),"rotating off-axis intersection");
        Vec local=glm::conjugate(e.orientation)*(h->position-e.center)/e.radii;
        close(glm::dot(local,local),1,1e-9,"hit lies on implicit ellipsoid");close(glm::length(h->normal),1,1e-10,"unit inverse-transpose normal");
        auto implicit=[&](Vec p) {auto q=glm::conjugate(e.orientation)*(p-e.center)/e.radii;return glm::dot(q,q)-1;};
        Vec gradient;for(int axis=0;axis<3;++axis) {Vec delta(0);delta[axis]=1e-5;gradient[axis]=(implicit(h->position+delta)-implicit(h->position-delta))/2e-5;}
        check(glm::dot(glm::normalize(gradient),h->normal)>.999999,"normal agrees with implicit gradient");
    }
    Model hierarchy;hierarchy.joints={{-1,{1,0,0},glm::angleAxis(glm::radians(90.),Vec(0,0,1)),"root"},{0,{0,2,0},{1,0,0,0},"child"}};
    auto part=volume({0,1,0},{.2,.8,.3},Material::Skin);part.parent=1;hierarchy.parts={part};auto resolved=resolve(hierarchy);
    close(resolved[0].center.x,-2,1e-9,"hierarchical local joint transform");close(resolved[0].center.y,0,1e-9,"joint translation rotates");
    hierarchy.joints[0].parent=1;bool rejected=false;try {resolve(hierarchy);} catch(const std::invalid_argument&) {rejected=true;}check(rejected,"cycles rejected");
    Camera camera;camera.verticalFov=glm::radians(55.);camera.aspect=1.6;
    Rasterizer analytic(160,100),lut(160,100),reverse(160,100);
    std::vector<Ellipsoid> scene={volume({-.24,0,5},{1.5,.75,.65},Material::Ochre,glm::angleAxis(.6,Vec(0,1,0))),volume({.35,.1,4.9},{.65,1.35,.72},Material::Teal,glm::angleAxis(.4,Vec(0,0,1)))};
    analytic.begin(camera);analytic.draw(scene);reverse.begin(camera);std::reverse(scene.begin(),scene.end());reverse.draw(scene);
    check(analytic.pixels()==reverse.pixels(),"intersections use depth, independent of primitive order");
    for(size_t i=0;i<analytic.depthBuffer().size();++i) if(std::isfinite(analytic.depthBuffer()[i])) {
        int x=int(i%160),y=int(i/160);auto ray=camera.ray(x+.5,y+.5,160,100);double expected=1e9;
        for(auto& e:scene) if(auto h=intersect(e,camera.eye,ray,camera.near)) expected=std::min(expected,h->t);
        close(analytic.depthBuffer()[i],expected,1e-9,"per-pixel analytic depth matches reference rays");
    }
    lut.begin(camera);lut.draw(scene,Mode::Lookup);size_t shared=0;double maximumError=0;
    for(size_t i=0;i<analytic.depthBuffer().size();++i) if(std::isfinite(analytic.depthBuffer()[i])&&std::isfinite(lut.depthBuffer()[i])) {
        ++shared;maximumError=std::max(maximumError,std::abs(analytic.depthBuffer()[i]-lut.depthBuffer()[i]));
    }
    check(shared>1200,"LUT conic covers oriented overlapping ellipsoids");check(maximumError<.09,"LUT surface depth stays near analytic solution");
    for(auto mode:{Mode::Analytic,Mode::Lookup}) {
        Rasterizer occlusion(80,50);Camera c;c.aspect=1.6;occlusion.begin(c);
        std::vector<uint8_t> backdrop(80*50,uint8_t(int(Material::Stone)*16+10));std::vector<double> depths(80*50,4.5);
        occlusion.background(backdrop,depths);occlusion.draw(std::span(&sphere,1),mode);
        check(occlusion.owners()[25*80+40]>=0,"volume in front of background depth mask");
        depths.assign(80*50,3.5);occlusion.begin(c);occlusion.background(backdrop,depths);occlusion.draw(std::span(&sphere,1),mode);
        check(occlusion.owners()[25*80+40]==-1,"foreground depth mask occludes volume");
        occlusion.begin(c);occlusion.triangle({-4,-4,4},{4,-4,4},{0,4,4},{.4,.3,.2},{.4,.3,.2},{.4,.3,.2},false);
        auto behind=volume({0,0,6},{1,1,1},Material::Red);occlusion.draw(std::span(&behind,1),mode);
        close(occlusion.depthBuffer()[25*80+40],4,1e-9,"triangles and volumes share camera-Z depth");
        auto inside=volume({0,0,0},{2,2,2},Material::Bone);occlusion.begin(c);occlusion.draw(std::span(&inside,1),mode);
        check(std::isfinite(occlusion.depthBuffer()[25*80+40]),"camera inside volume renders in both modes");
    }
    // Off-centre perspective views and all three rotation axes exercise the
    // projected conic independently of the central two-volume comparison.
    Rasterizer projectedExact(96,60),projectedLut(96,60);
    for(int frame=0;frame<12;++frame) {
        double phase=frame*.37;
        auto e=volume({std::sin(phase)*1.1,std::cos(phase)*.45,3.8},{1.0,.43,.71},Material::Rust,
                      glm::angleAxis(phase,glm::normalize(Vec(.7,.3,1))));
        Camera view;view.aspect=1.6;view.orientation=glm::angleAxis(.13*std::sin(phase),Vec(0,1,0));
        projectedExact.begin(view);projectedLut.begin(view);
        projectedExact.draw(std::span(&e,1));projectedLut.draw(std::span(&e,1),Mode::Lookup);
        size_t exactCoverage=0,lutCoverage=0;double error=0;
        for(size_t i=0;i<projectedExact.depthBuffer().size();++i) {
            bool a=std::isfinite(projectedExact.depthBuffer()[i]),b=std::isfinite(projectedLut.depthBuffer()[i]);
            exactCoverage+=a;lutCoverage+=b;
            if(a&&b) error=std::max(error,std::abs(projectedExact.depthBuffer()[i]-projectedLut.depthBuffer()[i]));
        }
        check(exactCoverage==lutCoverage && exactCoverage>90,"rotated off-axis LUT silhouette matches analytic coverage");
        check(error<.13,"off-axis LUT depth error remains bounded");
    }
    auto crossing=volume({.05,0,.12},{.20,.35,.20},Material::Teal);
    projectedExact.begin(camera);projectedLut.begin(camera);
    projectedExact.draw(std::span(&crossing,1));projectedLut.draw(std::span(&crossing,1),Mode::Lookup);
    check(projectedExact.depthBuffer()==projectedLut.depthBuffer(),"near-plane-crossing LUT volumes use exact fallback");
    for(uint32_t pixel:analytic.pixels()) check(std::find(analytic.palette().begin(),analytic.palette().end(),pixel)!=analytic.palette().end(),"analytic image stays in generated 256-colour palette");
    LookupTables tables;close(tables.profile(0,0),1,.001,"unit depth table centre");check(tables.profile(1,1)==0,"depth table exterior mask");
    auto idle=human(0,false),walk=human(.4,true),beast=creature(.4,true);
    check(idle.parts.size()>=25&&idle.parts.size()<=50,"human uses 25-50 actual volumes");
    auto a=resolve(idle),b=resolve(walk);double motion=0;for(size_t i=0;i<a.size();++i) motion+=glm::length(a[i].center-b[i].center);
    check(motion>1,"walking articulates local hierarchy");
    auto p0=resolve(human(.4,true)),p1=resolve(human(.4001,true));double delta=0;
    for(size_t i=0;i<p0.size();++i) delta=std::max(delta,glm::length(p0[i].center-p1[i].center));check(delta<.002,"animation interpolates continuously");
    check(beast.parts.size()>25,"strange articulated creature");
    auto vehicle=resolve(buggy(.5,1));check(vehicle.size()>30,"buggy is assembled from volumes");
    VehiclePose freshPose,crushedPose;crushedPose.dents[1]=.85;crushedPose.tyres[1]=.15;crushedPose.chassis=.4;crushedPose.alignment=.2;
    auto freshParts=resolve(buggy(freshPose)),crushedParts=resolve(buggy(crushedPose));
    double deformation=0;for(size_t i=0;i<freshParts.size();++i) deformation+=glm::length(freshParts[i].center-crushedParts[i].center)+glm::length(freshParts[i].radii-crushedParts[i].radii);
    check(deformation>.5,"component damage visibly deforms the actual volume model");
    auto compressed=freshPose;compressed.compression[0]=.35;auto compressedParts=resolve(buggy(compressed));double suspensionMotion=0;
    for(size_t i=0;i<freshParts.size();++i) suspensionMotion=std::max(suspensionMotion,glm::length(freshParts[i].center-compressedParts[i].center));
    check(suspensionMotion>.2,"suspension pose moves actual wheel and damper volumes");
    auto cabinModel=cockpit(freshPose,0),outsideModel=buggy(freshPose);
    auto cabinParts=resolve(cabinModel),outsideParts=resolve(outsideModel);
    for(size_t i=0;i<100;++i) {
        check(glm::length(cabinParts[i].center-outsideParts[i].center)<1e-12,"cockpit and exterior share real chassis and wheel placement");
    }
    VehiclePose mechanical;mechanical.independentWheels=true;mechanical.compression={.10,.22,.06,.18};mechanical.wheelSteer={.20,.24,0,0};mechanical.wheelRotation={.3,.6,.9,1.2};
    auto mechanicalModel=buggy(mechanical);auto mechanicalParts=resolve(mechanicalModel);int hubs=0;
    for(size_t i=0;i<mechanicalModel.parts.size();++i) {
        const auto& part=mechanicalModel.parts[i];
        if(part.parent>=0 && mechanicalModel.joints[part.parent].name=="upright" && part.center==Vec(0)) {
            const auto& hub=mechanicalModel.joints[part.parent];
            check(glm::length(mechanicalParts[i].center-hub.offset)<1e-10,"actual tyre volume is centred on its suspension upright");++hubs;
        }
    }
    check(hubs==4,"shared model has four independent physical tyre centres");
    if(argc>1) {
        std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
        ppm(analytic,out/"analytic.ppm");ppm(lut,out/"lookup.ppm");lut.finish(Debug::Depth);ppm(lut,out/"depth.ppm");
    }
    std::cout<<"PASS "<<checks<<" ellipsoid checks; human_parts="<<idle.parts.size()<<" creature_parts="<<beast.parts.size()<<" LUT_max_depth_error="<<maximumError<<'\n';return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
