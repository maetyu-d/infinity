#include "infinity/Cockpit.hpp"
#include "infinity/ExpeditionPlanner.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinity {
namespace {
constexpr glm::vec3 cyan{.38f,.85f,1.f},green{.35f,.95f,.71f},purple{.78f,.51f,1.f},gold{1.f,.67f,.3f},dim{.22f,.36f,.43f};
constexpr double tau=2*std::numbers::pi;
void tri(Mesh& m,glm::dvec2 a,glm::dvec2 b,glm::dvec2 c,glm::vec3 color,float alpha=1){
    const auto first=uint32_t(m.vertices.size());for(const auto p:{a,b,c})m.vertices.push_back({float(p.x),float(p.y),0,rgba(color,alpha)});
    m.indices.insert(m.indices.end(),{first,first+1,first+2});
}
void box(Mesh& m,CockpitRect r,glm::vec3 c,float alpha=1){tri(m,{r.x,r.y},{r.x+r.w,r.y},{r.x,r.y+r.h},c,alpha);tri(m,{r.x+r.w,r.y},{r.x+r.w,r.y+r.h},{r.x,r.y+r.h},c,alpha);}
void line(Mesh& m,glm::dvec2 a,glm::dvec2 b,glm::vec3 c){addLine(m,{a,0},{b,0},c);}
void ring(Mesh& m,glm::dvec2 p,double r,glm::vec3 c,unsigned count=32){for(unsigned i=0;i<count;++i){const double a=i*tau/count,b=(i+1)*tau/count;line(m,p+glm::dvec2(std::cos(a),std::sin(a))*r,p+glm::dvec2(std::cos(b),std::sin(b))*r,c);}}
void disc(Mesh& m,glm::dvec2 p,double r,glm::vec3 c,float alpha){for(unsigned i=0;i<24;++i){const double a=i*tau/24,b=(i+1)*tau/24;tri(m,p,p+glm::dvec2(std::cos(a),std::sin(a))*r,p+glm::dvec2(std::cos(b),std::sin(b))*r,c,alpha);}}
void dash(Mesh& m,glm::dvec2 a,glm::dvec2 b,glm::vec3 c){const unsigned n=std::max(1u,unsigned(glm::length(b-a)/6));for(unsigned i=0;i<n;i+=2)line(m,glm::mix(a,b,double(i)/n),glm::mix(a,b,double(i+1)/n),c);}
void icon(Mesh& m,unsigned kind,glm::dvec2 p,double s,glm::vec3 c){
    auto l=[&](double x,double y,double a,double b){line(m,p+glm::dvec2(x,y)*s,p+glm::dvec2(a,b)*s,c);};
    switch(kind){
    case 0: // ship
        l(0,-1,-.7,.8);l(-.7,.8,0,.45);l(0,.45,.7,.8);l(.7,.8,0,-1);l(-.35,.3,.35,.3);break;
    case 1: // destination and orbital plane
        ring(m,p,s*.65,c,20);l(-1,.55,1,-.55);l(.7,-.8,1,-.55);l(1,-.55,.65,-.35);break;
    case 2: // drill
        l(-.4,-.9,.4,-.9);l(-.4,-.9,-.4,.3);l(.4,-.9,.4,.3);l(-.4,.3,0,1);l(.4,.3,0,1);
        l(-.4,-.4,.4,-.65);l(-.4,.05,.4,-.2);break;
    case 3: // shaft entry
        l(-.9,-.7,-.5,-.7);l(-.5,-.7,-.5,1);l(.9,-.7,.5,-.7);l(.5,-.7,.5,1);l(0,-1,0,.5);l(0,.5,-.3,.15);l(0,.5,.3,.15);break;
    case 4: // survey
        ring(m,p,s*.65,c,20);l(-1,0,-.3,0);l(.3,0,1,0);l(0,-1,0,-.3);l(0,.3,0,1);break;
    case 5: // shrink
    case 7: // regrow
        ring(m,p,s*.16,c,12);
        for(unsigned i=0;i<4;++i){const double a=i*tau/4;const auto d=glm::dvec2(std::cos(a),std::sin(a)),t=glm::dvec2(-d.y,d.x);
            const double tip=kind==5?.38:1.;line(m,p+d*s*.38,p+d*s,c);line(m,p+d*s*tip,p+(d*(kind==5?.65:.73)+t*.21)*s,c);line(m,p+d*s*tip,p+(d*(kind==5?.65:.73)-t*.21)*s,c);}break;
    case 6: // marked crystal
        l(0,-1,.6,0);l(.6,0,0,1);l(0,1,-.6,0);l(-.6,0,0,-1);l(.4,.4,.65,.7);l(.65,.7,1,.15);break;
    case 8: // cutter and ore
        l(-.9,-.7,-.25,-.9);l(-.25,-.9,.4,-.3);l(.4,-.3,.8,-.5);l(-.25,-.9,-.8,.8);
        l(.1,.6,.6,.2);l(.6,.2,1,.6);l(1,.6,.7,1);l(.7,1,.1,.6);break;
    default: // return to port
        l(-.8,-.7,.8,-.7);l(-.8,-.7,-.8,.9);l(.8,-.7,.8,.9);l(-.8,.9,-.2,.9);l(.8,.9,.2,.9);
        l(0,-.3,0,1);l(0,-.3,-.3,0);l(0,-.3,.3,0);break;
    }
}
void status(CockpitOverlay& h,CockpitRect r,bool ready,bool required=true){
    const auto c=ready?green:required?gold:dim;box(h.panels,r,c,.06f);disc(h.panels,{r.x+9,r.y+r.h*.5},3,c,ready?1.f:.18f);
    ring(h.strokes,{r.x+9,r.y+r.h*.5},3,c,12);
}
}
void drawPlannerDiagrams(CockpitOverlay& h,const ExpeditionPlan& plan,const PlannerLayout& l){
    if(!plan.details){
        const auto summary=summarizePlan(plan);const glm::dvec2 point{l.panel.x+35,l.panel.y+62};
        const auto color=summary.warning?gold:cyan;ring(h.strokes,point,16,color,32);
        icon(h.strokes,summary.step?summary.step-1:1,point,9,color);
        for(unsigned i=0;i<l.steps.size();++i){const auto r=l.steps[i];const auto color=plan.steps[i].done?green:summary.step==i+1?cyan:dim;
            box(h.panels,r,color,plan.steps[i].done||summary.step==i+1?.85f:.24f);}
        if(plan.primaryEnabled)box(h.panels,l.actions[0],green,.12f);
        return;
    }
    const auto& v=plan.visual;const auto r=l.section;
    box(h.panels,r,{.025f,.065f,.09f},.9f);
    const auto tint=glm::mix(purple,v.tint,.55f);
    const glm::dvec2 world{l.left.x+9,plan.details?301.:245.};disc(h.panels,world,8,tint,.65f);ring(h.strokes,world,9,cyan,32);
    line(h.strokes,world+glm::dvec2(-12,4),world+glm::dvec2(12,-4),cyan);
    const double surface=r.y+54,cx=r.x+r.w*.51;
    const double scale=(r.h-76)/std::max(125.,v.boreDepth+v.chamberRadius+12);
    const auto chamber=glm::dvec2(cx,surface+v.boreDepth*scale);const double radius=v.chamberRadius*scale;
    // Diagram bands indicate material structure; no undiscovered deposits are invented.
    for(unsigned band=0;band<4;++band){const double y=surface+band*(r.y+r.h-surface)/4;
        box(h.panels,{r.x,y,r.w,(r.y+r.h-surface)/4},glm::mix(tint,cyan,float(band)*.16f),.055f+band*.025f);}
    for(unsigned band=0;band<5;++band){glm::dvec2 last{};
        for(unsigned i=0;i<=48;++i){const double x=r.x+i*r.w/48.;const double dx=(x-r.x)/r.w;
            const double fold=v.geology==GeologyType::MobileLid?std::sin(dx*7)*3:v.geology==GeologyType::IceShell?std::sin(dx*3)*1.5:dx*5;
            const glm::dvec2 p{x,surface+band*23+fold};
            auto open=[&](glm::dvec2 point){return v.hasBore&&((std::abs(point.x-cx)<5&&point.y<=surface+v.reached*scale)||(v.chamberReady&&glm::length(point-chamber)<radius+1));};
            if(i&&!open(p)&&!open(last)&&p.y<r.y+r.h-1&&last.y<r.y+r.h-1)line(h.strokes,last,p,band?glm::mix(dim,tint,.28f):cyan);
            last=p;}}
    for(unsigned i=1;i<8;++i){const double x=r.x+i*r.w/8;line(h.strokes,{x,r.y+22},{x,surface-7},dim*.38f);}
    const glm::dvec2 station{r.x+26,r.y+27};ring(h.strokes,station,8,cyan,4);line(h.strokes,station+glm::dvec2(-14,0),station+glm::dvec2(14,0),cyan);
    line(h.strokes,station+glm::dvec2(0,-12),station+glm::dvec2(0,12),cyan);
    glm::dvec2 last=station;
    for(unsigned i=1;i<=28;++i){const double t=i/28.;const glm::dvec2 p{glm::mix(station.x,cx,t),glm::mix(station.y,surface-10,t)-std::sin(t*std::numbers::pi)*13};
        if(i%3)line(h.strokes,last,p,plan.steps[1].done?green:dim);last=p;}
    if(v.hasBore){
        box(h.panels,{cx-4,surface,8,std::max(0.,v.reached*scale)},{.02f,.07f,.09f});
        line(h.strokes,{cx-5,surface},{cx-5,surface+v.reached*scale},cyan);line(h.strokes,{cx+5,surface},{cx+5,surface+v.reached*scale},cyan);
        if(v.chamberReady){disc(h.panels,chamber,radius,{.02f,.07f,.09f},1);ring(h.strokes,chamber,radius,purple);
            for(unsigned i=0;i<v.deposits.size();++i){const auto& d=v.deposits[i];const auto p=glm::dvec2(cx+d.metres.x*scale,surface+d.metres.y*scale);
                const auto color=d.grade?glm::mix(material(d.mineral).color,glm::vec3(1),.3f):dim;
                disc(h.panels,p,3.2,color,d.grade?.9f:.2f);ring(h.strokes,p,3.2,color,12);
                if(d.marked)ring(h.strokes,p,5.5,green,4);
                if(i==plan.find)ring(h.strokes,p,7.5,cyan,20);}
        }else{dash(h.strokes,{cx,surface+v.reached*scale},{cx,chamber.y},purple);ring(h.strokes,chamber,radius,dim,16);
            if(v.phase!=ProbePhase::Recovered)icon(h.strokes,2,{cx,surface+v.reached*scale},5,v.phase==ProbePhase::Paused?gold:cyan);}
    }else{dash(h.strokes,{cx,surface},{cx,chamber.y},dim);ring(h.strokes,chamber,radius,dim,16);}
    // Depth ruler uses the same SI depth as the operation and excavation.
    const double ruler=r.x+10;line(h.strokes,{ruler,surface},{ruler,surface+120*scale},dim);
    for(unsigned depth=0;depth<=120;depth+=20)line(h.strokes,{ruler,surface+depth*scale},{ruler+4,surface+depth*scale},dim);
    if(v.shipLocal){const glm::dvec2 ship{std::clamp(cx+v.shipLateral*scale,r.x+20,r.x+r.w-20),std::clamp(surface+v.shipDepth*scale,r.y+25,r.y+r.h-15)};
        icon(h.strokes,0,ship,v.shipMetres<.012?3.5:7,green);ring(h.strokes,ship,v.shipMetres<.012?7:11,green,24);}
    // Composition is a normalized inherited bulk estimate, not an ore scan.
    constexpr std::array colors{cyan,gold,purple,glm::vec3(.49f,.62f,.68f)};double x=l.composition.x;
    for(unsigned i=0;i<colors.size();++i){const double width=l.composition.w*std::clamp(v.composition[i],0.,1.);box(h.panels,{x,l.composition.y,width,l.composition.h},colors[i],.75f);x+=width;}
    const auto nose=l.bays[0];const glm::dvec2 bridge{nose.x+nose.w*.5,377};icon(h.strokes,0,bridge,8,cyan);
    for(const auto bay:l.bays){const double cx=bay.x+bay.w*.5;line(h.strokes,{bridge.x,383},{cx,383},dim);line(h.strokes,{cx,383},{cx,bay.y},dim);}
    for(unsigned i=0;i<l.bays.size();++i){const auto bay=l.bays[i];const auto& b=v.bays[i];const bool selected=i==plan.bay;
        const auto color=b.tool==PlannerTool::Empty?dim:b.tool==PlannerTool::Miner?purple:b.tool==PlannerTool::Probe?cyan:gold;
        box(h.panels,bay,color,selected?.12f:.05f);
        const glm::dvec2 centre{bay.x+bay.w*.5,bay.y+31};ring(h.strokes,centre,16,selected?cyan:dim,32);
        if(b.tool==PlannerTool::Empty){line(h.strokes,centre-glm::dvec2(6,0),centre+glm::dvec2(6,0),dim);line(h.strokes,centre-glm::dvec2(0,6),centre+glm::dvec2(0,6),dim);}
        else icon(h.strokes,b.tool==PlannerTool::Probe?2:b.tool==PlannerTool::Miner?8:4,centre,10,color);
        if(selected){line(h.strokes,{bay.x,bay.y},{bay.x+bay.w,bay.y},cyan);line(h.strokes,{bay.x,bay.y},{bay.x,bay.y+bay.h},cyan);}
        const bool staffed=b.operatorReadiness>=(b.tool==PlannerTool::Probe?.25:.0001);
        ring(h.strokes,{bay.x+10,bay.y+65},2.5,staffed?green:gold,12);line(h.strokes,{bay.x+7,bay.y+72},{bay.x+7,bay.y+69},staffed?green:gold);line(h.strokes,{bay.x+7,bay.y+69},{bay.x+13,bay.y+69},staffed?green:gold);
        box(h.panels,{bay.x+20,bay.y+74,bay.w-28,2},dim,.5f);box(h.panels,{bay.x+20,bay.y+74,(bay.w-28)*std::clamp(b.operatorReadiness,0.,1.),2},staffed?green:gold,.9f);
        if(b.tool==PlannerTool::Probe)for(unsigned n=0;n<6;++n)box(h.panels,{bay.x+bay.w-10,bay.y+13+n*6.,3,4},n<b.rounds?cyan:dim,n<b.rounds?1.f:.4f);
    }
    auto ready=[&](unsigned i){return i<plan.checks.size()&&plan.checks[i].ready;};
    const double half=(l.right.w-8)*.5;
    if(plan.details){status(h,{l.right.x,475,half,18},ready(3));status(h,{l.right.x+half+8,475,half,18},ready(4));
        status(h,{l.right.x,498,half,18},ready(5));status(h,{l.right.x+half+8,498,half,18},ready(6),false);
    }else{const double width=(l.right.w-24)/4;for(unsigned i=0;i<4;++i)status(h,{l.right.x+i*(width+8),475,width,18},ready(i+3),i!=3);}
    for(unsigned kg=0;kg<10;++kg){const bool aboard=(kg+1)*plan.requiredKg/10.<=v.sampleKg;
        box(h.panels,{l.right.x+kg*11,531,8,6},aboard?green:dim,aboard?.9f:.35f);}
    // One connected visual journey replaces ten equal-weight prose rows.
    for(unsigned i=0;i<l.steps.size();++i){const auto step=l.steps[i];const auto& state=plan.steps[i];const glm::dvec2 p{step.x+step.w*.5,step.y+17};
        const auto color=state.done?green:state.active?cyan:dim;
        if(i){const auto prev=l.steps[i-1];line(h.strokes,{prev.x+prev.w*.5+17,p.y},{p.x-17,p.y},state.done||state.active?green*.75f:dim*.55f);}
        disc(h.panels,p,15,color,state.done?.16f:state.active?.2f:.04f);ring(h.strokes,p,15,color,32);
        if(state.active)ring(h.strokes,p,19,cyan*.65f,40);
        icon(h.strokes,i,p,8.5,color);
        if(state.done){line(h.strokes,p+glm::dvec2(8,-13),p+glm::dvec2(11,-10),green);line(h.strokes,p+glm::dvec2(11,-10),p+glm::dvec2(16,-17),green);}
    }
    if(plan.primaryEnabled)box(h.panels,l.actions[0],green,.12f);
}
} // namespace infinity
