#include "Renderer.hpp"
#include "Combat.hpp"
#include "FractalRenderer.hpp"
#include "HudFont.hpp"
#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <vs_debugdraw_lines.bin.h>
#include <fs_debugdraw_lines.bin.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace desert {
using infinity::Mesh;
using infinity::Vertex;
static uint32_t rgba(glm::vec3 c,float a=1) {
    auto byte=[](float v) {return uint32_t(std::clamp(v,0.f,1.f)*255.f+.5f);};
    return byte(c.x)|(byte(c.y)<<8)|(byte(c.z)<<16)|(byte(a)<<24);
}
using V = glm::dvec3;
using C = glm::vec3;
constexpr uint64_t uiBlend=BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,BGFX_STATE_BLEND_INV_SRC_ALPHA,BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA);
namespace ell = infinity::ellipsoid;
constexpr double pi = std::numbers::pi;
static const bgfx::EmbeddedShader shaders[] = {BGFX_EMBEDDED_SHADER(vs_debugdraw_lines),
    BGFX_EMBEDDED_SHADER(fs_debugdraw_lines),BGFX_EMBEDDED_SHADER_END()};
const C ink(0), white(1), yellow(1,.96,.24), cyan(.22,.96,1);
const C red(1,.28,.21), green(.48,1,.22), blue(.38,.28,1), magenta(.98,.25,.92);
const C haze(.82,.69,.51), sunDirection = glm::normalize(C(-.6,.47,.75));
static HudFont* activeFont=nullptr;
static double smooth(double a,double b,double x) { x=std::clamp((x-a)/(b-a),0.,1.); return x*x*(3-2*x); }
static void vertex(Mesh& m, V p, C color, float alpha=1) { m.vertices.push_back({float(p.x),float(p.y),float(p.z),rgba(color,alpha)}); }
static void triangle(Mesh& m, V a,V b,V c,C color, bool lit=true, float alpha=1) {
    auto n=glm::cross(b-a,c-a);
    if(lit && glm::length(n)>.00001) {
        double light=std::max(0.,glm::dot(glm::normalize(n),V(sunDirection)));
        color=color*float(.40+.60*light)+C(.055,.036,.017);
    }
    uint32_t i=uint32_t(m.vertices.size());
    vertex(m,a,color,alpha); vertex(m,b,color,alpha); vertex(m,c,color,alpha);
    m.indices.insert(m.indices.end(),{i,i+1,i+2});
}
static void quad(Mesh& m,V a,V b,V c,V d,C color,bool lit=true,float alpha=1) {
    triangle(m,a,b,c,color,lit,alpha); triangle(m,a,c,d,color,lit,alpha);
}
static void box(Mesh& m,V a,V b,C color) {
    V p[8]={{a.x,a.y,a.z},{b.x,a.y,a.z},{b.x,b.y,a.z},{a.x,b.y,a.z},
            {a.x,a.y,b.z},{b.x,a.y,b.z},{b.x,b.y,b.z},{a.x,b.y,b.z}};
    constexpr int faces[6][4]={{0,3,2,1},{4,5,6,7},{0,4,7,3},{1,2,6,5},{3,7,6,2},{0,1,5,4}};
    for(auto& f:faces) quad(m,p[f[0]],p[f[1]],p[f[2]],p[f[3]],color);
}
static void tube(Mesh& m,V a,V b,double radius,C color,int segments=8) {
    V axis=glm::normalize(b-a);
    V u=glm::normalize(glm::cross(axis,std::abs(axis.y)>.9?V(1,0,0):V(0,1,0)))*radius;
    V v=glm::normalize(glm::cross(axis,u))*radius;
    for(int i=0;i<segments;++i) {
        double t=i*2*pi/segments, t1=(i+1)*2*pi/segments;
        V r=u*std::cos(t)+v*std::sin(t), s=u*std::cos(t1)+v*std::sin(t1);
        quad(m,a+r,a+s,b+s,b+r,color); triangle(m,a,a+s,a+r,color); triangle(m,b,b+r,b+s,color);
    }
}
static void disc(Mesh& m,V p,double radius,C color,float alpha=1,int segments=48) {
    for(int i=0;i<segments;++i) {
        double a=i*2*pi/segments,b=(i+1)*2*pi/segments;
        triangle(m,p,p+V(std::cos(a)*radius,std::sin(a)*radius,0),p+V(std::cos(b)*radius,std::sin(b)*radius,0),color,false,alpha);
    }
}
static void rect(Mesh& m,double x,double y,double w,double h,C c,float a=1) { quad(m,{x,y,0},{x+w,y,0},{x+w,y+h,0},{x,y+h,0},c,false,a); }
static void rounded(Mesh& m,double x,double y,double w,double h,double radius,C c,float alpha=1) {
    double r=std::min({radius,w*.5,h*.5});rect(m,x+r,y,w-2*r,h,c,alpha);rect(m,x,y+r,r,h-2*r,c,alpha);rect(m,x+w-r,y+r,r,h-2*r,c,alpha);
    for(int corner=0;corner<4;++corner){V centre(x+(corner==0||corner==3?r:w-r),y+(corner<2?r:h-r),0);double start=(corner-2)*pi*.5;
        for(int i=0;i<8;++i){double a=start+i*pi/16,b=start+(i+1)*pi/16;triangle(m,centre,centre+V(std::cos(a)*r,std::sin(a)*r,0),centre+V(std::cos(b)*r,std::sin(b)*r,0),c,false,alpha);}}
}
static void vectorFrame(Mesh&,double,double,double,double,C,float=1);
static void card(Mesh& m,double x,double y,double w,double h,float alpha=.84f){rect(m,x,y,w,h,ink,alpha);vectorFrame(m,x,y,w,h,C(.58),alpha);}
static void line(Mesh& m,glm::dvec2 a,glm::dvec2 b,double width,C c,float alpha=1) {
    auto d=glm::normalize(b-a); glm::dvec2 n(-d.y*width*.5,d.x*width*.5);
    quad(m,V(a+n,0),V(b+n,0),V(b-n,0),V(a-n,0),c,false,alpha);
}
static void vectorFrame(Mesh& m,double x,double y,double w,double h,C c,float alpha) {
    double cut=std::min(8.,h*.2);std::array<glm::dvec2,8> points={{{x+cut,y},{x+w-cut,y},{x+w,y+cut},{x+w,y+h-cut},{x+w-cut,y+h},{x+cut,y+h},{x,y+h-cut},{x,y+cut}}};
    for(int i=0;i<8;++i)line(m,points[i],points[(i+1)%8],1.,c,alpha);
}
// A small original 5x7 instrument font, drawn as geometry with the engine's colour shader.
static std::array<uint8_t,7> glyph(char c) {
    if(c>='a'&&c<='z')c=char(c-'a'+'A');
    switch(c) {
#define G(ch,a,b,c,d,e,f,g) case ch: return {a,b,c,d,e,f,g}
    G('A',14,17,17,31,17,17,17); G('B',30,17,17,30,17,17,30); G('C',15,16,16,16,16,16,15);
    G('D',30,17,17,17,17,17,30); G('E',31,16,16,30,16,16,31); G('F',31,16,16,30,16,16,16);
    G('G',15,16,16,23,17,17,15); G('H',17,17,17,31,17,17,17); G('I',31,4,4,4,4,4,31);
    G('J',7,2,2,2,18,18,12); G('K',17,18,20,24,20,18,17); G('L',16,16,16,16,16,16,31);
    G('M',17,27,21,21,17,17,17); G('N',17,25,25,21,19,19,17); G('O',14,17,17,17,17,17,14);
    G('P',30,17,17,30,16,16,16); G('Q',14,17,17,17,21,18,13); G('R',30,17,17,30,20,18,17);
    G('S',15,16,16,14,1,1,30); G('T',31,4,4,4,4,4,4); G('U',17,17,17,17,17,17,14);
    G('V',17,17,17,17,17,10,4); G('W',17,17,17,21,21,27,17); G('X',17,17,10,4,10,17,17);
    G('Y',17,17,10,4,4,4,4); G('Z',31,1,2,4,8,16,31);
    G('0',14,17,19,21,25,17,14); G('1',4,12,4,4,4,4,14); G('2',14,17,1,2,4,8,31);
    G('3',30,1,1,14,1,1,30); G('4',2,6,10,18,31,2,2); G('5',31,16,16,30,1,1,30);
    G('6',14,16,16,30,17,17,14); G('7',31,1,2,4,8,8,8); G('8',14,17,17,14,17,17,14);
    G('9',14,17,17,15,1,1,14); G(':',0,4,4,0,4,4,0); G('.',0,0,0,0,0,6,6);
    G('/',1,1,2,4,8,16,16); G('-',0,0,0,31,0,0,0); G('+',0,4,4,31,4,4,0);
    G('>',16,8,4,2,4,8,16); G('<',1,2,4,8,4,2,1); G('!',4,4,4,4,4,0,4);
    G('%',25,25,2,4,8,19,19); G('(',2,4,8,8,8,4,2); G(')',8,4,2,2,2,4,8);
#undef G
    default:return {};
    }
}
static double textWidth(const std::string& s,double size) {return activeFont?activeFont->width(s,size*9):s.size()*6*size;}
static void text(Mesh& m,const std::string& s,double x,double y,double size,C color=white,float alpha=1) {
    if(activeFont){activeFont->text(s,x,y,size*9,rgba(color,alpha));return;}
    for(char ch:s) {
        auto g=glyph(ch);
        for(int row=0;row<7;++row) for(int col=0;col<5;++col)
            if(g[row] & (1<<(4-col))) rect(m,x+col*size,y+row*size,size*.89,size*.89,color,alpha);
        x+=size*6;
    }
}
static void centerText(Mesh& m,const std::string& s,double x,double y,double size,C color=white) { text(m,s,x-textWidth(s,size)*.5,y,size,color); }
static void label(Mesh& m,const std::string& s,double x,double y,double em,C color=white){text(m,s,x,y,em/9,color);}
static void centered(Mesh& m,const std::string& s,double x,double y,double em,C color=white){centerText(m,s,x,y,em/9,color);}
static void rightText(Mesh& m,const std::string& s,double x,double y,double em,C color=white){label(m,s,x-textWidth(s,em/9),y,em,color);}
static std::string shortClock(double t){int seconds=int(std::ceil(std::max(0.,t-1e-6)));char b[32];std::snprintf(b,sizeof(b),"%02d:%02d",seconds/60,seconds%60);return b;}
static std::string durationText(double seconds){if(int(seconds)%60==0)return std::to_string(int(seconds)/60)+" min";return int(seconds)<60?std::to_string(int(seconds))+" sec":shortClock(seconds);}
static std::string clockText(double t) {
    int ms=int(std::max(0.,t)*100); char b[32]; std::snprintf(b,sizeof(b),"%02d:%02d.%02d",ms/6000,(ms/100)%60,ms%100); return b;
}
struct Callbacks : bgfx::CallbackI {
    void fatal(const char*,uint16_t,bgfx::Fatal::Enum,const char* s) override {std::fprintf(stderr,"bgfx: %s\n",s); std::abort();}
    void traceVargs(const char*,uint16_t,const char*,va_list) override {}
    void profilerBegin(const char*,uint32_t,const char*,uint16_t) override {}
    void profilerBeginLiteral(const char*,uint32_t,const char*,uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t,void*,uint32_t) override {return false;}
    void cacheWrite(uint64_t,const void*,uint32_t) override {}
    void screenShot(const char* path,uint32_t w,uint32_t h,uint32_t pitch,bgfx::TextureFormat::Enum,const void* data,uint32_t,bool flip) override {
        std::ofstream f(path,std::ios::binary); f<<"P6\n"<<w<<' '<<h<<"\n255\n";
        auto bytes=static_cast<const uint8_t*>(data); std::vector<char> row(w*3);
        for(uint32_t y=0;y<h;++y) {auto p=bytes+(flip?h-1-y:y)*pitch;
            for(uint32_t x=0;x<w;++x) {row[x*3]=char(p[x*4+2]);row[x*3+1]=char(p[x*4+1]);row[x*3+2]=char(p[x*4]);}
            f.write(row.data(),row.size());}
    }
    void captureBegin(uint32_t,uint32_t,uint32_t,bgfx::TextureFormat::Enum,bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*,uint32_t) override {}
};
static std::vector<ell::Ellipsoid> authoredProps(const Course& course,int tx,int tz) {
    V origin(tx*96.,0,tz*96.);std::vector<ell::Ellipsoid> out;
    infinity::Random random{course.seed ^ infinity::mix64(uint64_t(int64_t(tx))) ^ infinity::mix64(uint64_t(int64_t(tz))+71)};
    for(int i=0;i<10;++i) {
        double x=origin.x+random.unit(i*13)*96,z=origin.z+random.unit(i*13+1)*96;
        if(std::abs(x-course.center(z))<18) continue;
        V base(x,course.height(x,z),z);double size=.3+random.unit(i*13+2)*1.5;
        if(i<3 && random.unit(i*13+5)>.35) {
            double h=2.1+size*1.6;
            out.push_back(ell::volume(base+V(0,h*.48,0),{.22,h*.53,.24},ell::Material::Cactus));
            out.push_back(ell::segment(base+V(0,h*.5,0),base+V(-.7,h*.6,0),.15,ell::Material::Cactus));
            out.push_back(ell::volume(base+V(-.7,h*.74,0),{.15,h*.18,.16},ell::Material::Cactus));
            out.push_back(ell::segment(base+V(0,h*.65,0),base+V(.65,h*.7,0),.14,ell::Material::Cactus));
            out.push_back(ell::volume(base+V(.65,h*.82,0),{.14,h*.18,.15},ell::Material::Cactus));
        } else if(i<7) {
            out.push_back(ell::volume(base+V(0,size*.26,0),{size,size*.57,size*.75},ell::Material::Stone,glm::angleAxis(random.unit(i+60)*2.,glm::normalize(V(.3,1,.2)))));
            if(size>1) out.push_back(ell::volume(base+V(size*.5,size*.13,size*.35),{size*.55,size*.32,size*.45},ell::Material::Stone));
        } else out.push_back(ell::volume(base+V(0,size*.21,0),{size*.65,size*.34,size*.55},ell::Material::Cactus));
    }
    double x=origin.x+48,z=origin.z+48;
    if(random.unit(100)>.88 && std::abs(x-course.center(z))>120) {
        V base(x,course.height(x,z)-1,z);double r=12+random.unit(102)*20,h=12+random.unit(103)*29;
        out.push_back(ell::volume(base+V(0,h*.28,0),{r,h*.42,r*.8},ell::Material::Stone));
        out.push_back(ell::volume(base+V(-r*.10,h*.62,0),{r*.7,h*.35,r*.58},ell::Material::Stone));
        out.push_back(ell::volume(base+V(0,h*.87,0),{r*.44,h*.19,r*.42},ell::Material::Sand));
    }
    return out;
}
struct Tile { V origin; Mesh terrain,props; std::vector<ell::Ellipsoid> volumes; };
struct Renderer::Impl {
    SDL_Window* window;
    Callbacks callbacks;
    bgfx::SwapChain swap;
    bgfx::VertexLayout layout;
    bgfx::ProgramHandle program=BGFX_INVALID_HANDLE;
    int width=0,height=0;
    double prepareSeconds=0,submitWaitSeconds=0;uint64_t measuredFrames=0;
    std::map<std::pair<int,int>,Tile> tiles;
    uint64_t seed=0;
    uint64_t trailSeed=0;
    double trailBegin=-1e100;
    Mesh trailMesh;
    std::array<float,2048> fogTable{};
    glm::dmat4 view{1},projection{1};
    V eye{};
    Mesh temporary;

    std::unique_ptr<FractalRenderer> gpu;
    std::unique_ptr<HudFont> font;

    Mesh sceneGround;
    struct Effect {V position,velocity;double age=0,life=1,size=.1;ell::Material material=ell::Material::Sand;double opacity=.2;};
    std::vector<Effect> effects;
    std::array<uint64_t,4> lastImpacts{};
    double effectTime=0,emitTime=0;
    uint64_t effectSeed=0,effectCounter=0;
    explicit Impl(SDL_Window* w) : window(w) {
        for(size_t i=0;i<fogTable.size();++i) fogTable[i]=float(1-std::exp(-std::pow(double(i)/650.,1.65)));
    }
    void submit(const Mesh& mesh,bgfx::ViewId viewId,uint64_t flags) {
        if(mesh.vertices.empty() || mesh.indexCount()==0) return;
        bgfx::TransientVertexBuffer vb; bgfx::TransientIndexBuffer ib;
        bool wide=mesh.wideIndices();
        if(!bgfx::allocTransientBuffers(&vb,layout,uint32_t(mesh.vertices.size()),&ib,uint32_t(mesh.indexCount()),wide)) return;
        std::memcpy(vb.data,mesh.vertices.data(),mesh.vertices.size()*sizeof(Vertex));
        std::memcpy(ib.data,mesh.indexData(),mesh.indexCount()*mesh.indexWidth());
        bgfx::setVertexBuffer(0,&vb); bgfx::setIndexBuffer(&ib); bgfx::setState(flags); bgfx::submit(viewId,program);
    }
    void world(const Mesh& mesh,V offset={},bool transparent=false) {
        temporary=mesh;
        for(auto& vertex:temporary.vertices) {
            V p=V(vertex.x,vertex.y,vertex.z)+offset-eye;
            double d=glm::length(p);
            float fog=fogTable[std::min(size_t(d),fogTable.size()-1)];
            uint32_t c=vertex.color;
            C original(float(c&255)/255,float((c>>8)&255)/255,float((c>>16)&255)/255);
            vertex.color=rgba(glm::mix(original,haze,fog),float((c>>24)&255)/255);
            vertex.x=float(p.x);vertex.y=float(p.y);vertex.z=float(p.z);
        }
        submit(temporary,1,BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|(transparent?0:BGFX_STATE_WRITE_Z)|BGFX_STATE_DEPTH_TEST_LESS|BGFX_STATE_MSAA|uiBlend);
    }
    Tile generate(const Course& course,int tx,int tz) {
        Tile t; t.origin={tx*96.,0,tz*96.};
        constexpr int n=24;
        for(int z=0;z<=n;++z) for(int x=0;x<=n;++x) {
            double px=t.origin.x+x*4,pz=t.origin.z+z*4;
            double h=course.height(px,pz); V normal=course.normal(px,pz);
            double shade=.38+.62*std::max(0.,glm::dot(normal,V(sunDirection)));
            double grain=course.noise(px*.11,pz*.11,5)*.022;
            double strata=.025*std::sin(px*.19+pz*.09+h*.35);
            C sand=C(.88+grain+strata,.64+grain+strata,.36+grain*.5+strata*.7);
            double fromRoad=px-course.center(pz);
            float track=float(1-smooth(7.2,12.,std::abs(fromRoad)));
            sand=glm::mix(sand,C(.56,.40,.25),track*.40f);
            float ruts=float((1-smooth(.22,.8,std::abs(std::abs(fromRoad)-2.1)))*.16);
            sand*=1-ruts;
            C c=sand*float(shade)+C(.04,.035,.028);
            vertex(t.terrain,{x*4.,h,z*4.},c);
        }
        for(int z=0;z<n;++z) for(int x=0;x<n;++x) {
            uint32_t a=z*(n+1)+x,b=a+n+1;
            t.terrain.indices.insert(t.terrain.indices.end(),{a,b,a+1,a+1,b,b+1});
        }
        t.terrain.compact();
        infinity::Random random{course.seed ^ infinity::mix64(uint64_t(int64_t(tx))) ^ infinity::mix64(uint64_t(int64_t(tz))+71)};
        for(int i=0;i<10;++i) {
            double x=random.unit(i*13)*96,z=random.unit(i*13+1)*96;
            double wx=t.origin.x+x,wz=t.origin.z+z;
            if(std::abs(wx-course.center(wz))<18) continue;
            V base(x,course.height(wx,wz),z);
            double s=.3+random.unit(i*13+2)*1.5;
            if(i<3 && random.unit(i*13+5)>.35) {
                C cactus(.22,.31,.20); double h=2.1+s*1.6;
                tube(t.props,base,base+V(0,h,0),.20+s*.04,cactus,7);
                tube(t.props,base+V(0,h*.5,0),base+V(-.7,h*.6,0),.14,cactus,6);
                tube(t.props,base+V(-.7,h*.6,0),base+V(-.7,h*.85,0),.14,cactus,6);
                tube(t.props,base+V(0,h*.68,0),base+V(.65,h*.72,0),.13,cactus,6);
                tube(t.props,base+V(.65,h*.72,0),base+V(.65,h*.96,0),.13,cactus,6);
            } else if(i<7) {
                C rock(.43+random.unit(i+30)*.12,.29,.18);
                // Irregular, layered wind-worn stone.
                constexpr int sides=7;
                V top=base+V(s*.1,s*.8,0);
                for(int k=0;k<sides;++k) {
                    double a=k*2*pi/sides,b=(k+1)*2*pi/sides;
                    V p=base+V(std::cos(a)*s,-.1,std::sin(a)*s*.73);
                    V q=base+V(std::cos(b)*s,-.1,std::sin(b)*s*.73);
                    V r=top+V(std::cos(b)*s*.55,0,std::sin(b)*s*.45);
                    V u=top+V(std::cos(a)*s*.55,0,std::sin(a)*s*.45);
                    quad(t.props,p,u,r,q,rock);triangle(t.props,top,u,r,rock*1.12f);
                }
            } else {
                C shrub(.40,.34,.18);
                for(int k=0;k<5;++k) {
                    double a=k*2*pi/5; V end=base+V(std::cos(a)*s*.5,s*.45,std::sin(a)*s*.5);
                    tube(t.props,base,end,.04,shrub,3);
                }
            }
        }
        // Occasional buttes outside the course corridor.
        double wx=t.origin.x+48,wz=t.origin.z+48;
        if(random.unit(100)>.88 && std::abs(wx-course.center(wz))>120) {
            V base(48,course.height(wx,wz)-1,48); double size=12+random.unit(102)*20,h=12+random.unit(103)*29;
            constexpr int sides=11;
            for(int level=0;level<3;++level) for(int k=0;k<sides;++k) {
                double a=k*2*pi/sides,b=(k+1)*2*pi/sides;
                double r=size*(1-level*.22),r2=size*(.78-level*.22);
                V p=base+V(std::cos(a)*r,h*level/3,std::sin(a)*r*.8);
                V q=base+V(std::cos(b)*r,h*level/3,std::sin(b)*r*.8);
                V u=base+V(std::cos(a)*r2,h*(level+1)/3,std::sin(a)*r2*.8);
                V v=base+V(std::cos(b)*r2,h*(level+1)/3,std::sin(b)*r2*.8);
                C rock(.58+level*.065,.34+level*.045,.21+level*.025);
                quad(t.props,p,u,v,q,rock);
                if(level==2) triangle(t.props,base+V(0,h,0),u,v,rock*1.15f);
            }
        }
        t.props.compact(); t.volumes=authoredProps(course,tx,tz); return t;
    }
    void prepareTiles(const Race& race) {
        if(seed!=race.course.seed) {tiles.clear();seed=race.course.seed;}
        int cx=int(std::floor(eye.x/96)),cz=int(std::floor(eye.z/96));
        for(auto it=tiles.begin();it!=tiles.end();) {
            if(std::abs(it->first.first-cx)>8 || std::abs(it->first.second-cz)>8) it=tiles.erase(it); else ++it;
        }
        for(int z=cz-7;z<=cz+7;++z) for(int x=cx-7;x<=cx+7;++x) {
            auto key=std::pair(x,z);
            if(!tiles.contains(key)) tiles.emplace(key,generate(race.course,x,z));
        }
    }
    void environment(const Race& race) {
        prepareTiles(race);
        V forward=glm::inverse(view)*glm::dvec4(0,0,1,0);forward.y=0;forward=glm::normalize(forward);
        V right(forward.z,0,-forward.x);
        double halfWidth=std::tan(glm::radians(44.))*width/height;
        for(auto& [key,t]:tiles) {
            V delta=t.origin+V(48,0,48)-eye;delta.y=0;
            double along=glm::dot(delta,forward);
            if(along<-120 || std::abs(glm::dot(delta,right))>std::max(0.,along)*halfWidth+120) continue;
            world(t.terrain,t.origin);
            if(glm::length(delta)<520) world(t.props,t.origin);
        }
    }
    void trail(const Course& course) {
        double begin=std::floor((eye.z-70)/30)*30;
        if(begin==trailBegin && trailSeed==course.seed) { world(trailMesh); return; }
        trailBegin=begin;trailSeed=course.seed;
        Mesh mesh;
        // Match the rendered terrain triangles, so the wheel ruts cannot float
        // above analytic dunes or disappear below a coarse triangle.
        auto surface=[&](double x,double z) {
            double gx=std::floor(x/4)*4,gz=std::floor(z/4)*4;
            double u=(x-gx)/4,v=(z-gz)/4;
            double a=course.height(gx,gz),b=course.height(gx+4,gz),c=course.height(gx,gz+4),d=course.height(gx+4,gz+4);
            return u+v<1?a*(1-u-v)+b*u+c*v:b*(1-v)+c*(1-u)+d*(u+v-1);
        };
        for(int i=0;i<180;++i) for(int side:{-1,1}) {
            double z=begin+i*3,z1=z+3;
            auto edge=[&](double pz,double width) {
                double x=course.center(pz)+side*1.75+width;
                return V(x,surface(x,pz)+.025,pz);
            };
            quad(mesh,edge(z,-.22),edge(z1,-.22),edge(z1,.22),edge(z,.22),C(.43,.31,.19),false,.45f);
        }
        trailMesh=std::move(mesh);
        world(trailMesh);
    }
    void gate(const Race& race,int number,double time) {
        double z=number*gateSpacing;V middle=race.course.point(z);double dist=glm::length(middle-eye);
        if(dist>650) return;
        Mesh mesh;bool active=number==race.nextGate;
        C c=number==gateCount?cyan:(active?yellow:C(.68,.61,.44));
        double yaw=race.course.heading(z);V right(std::cos(yaw),0,-std::sin(yaw));
        V left=middle-right*13.,r=middle+right*13.;
        left.y=race.course.height(left.x,left.z);r.y=race.course.height(r.x,r.z);
        tube(mesh,left,left+V(0,9,0),.24,ink,8);tube(mesh,r,r+V(0,9,0),.24,ink,8);
        tube(mesh,left+V(0,8.2,0),r+V(0,8.2,0),.23,c,8);
        for(int i=0;i<7;++i) {
            tube(mesh,left+V(0,i*1.1+.25,0),left+V(0,i*1.1+.75,0),.27,i%2?c:white,6);
            tube(mesh,r+V(0,i*1.1+.25,0),r+V(0,i*1.1+.75,0),.27,i%2?c:white,6);
        }
        // Fabric pennants move in the desert wind.
        for(int side:{-1,1}) {
            V p=side<0?left:r;
            V a=p+V(0,8.6,0), b=p+right*(side*3.2)+V(0,8.1,std::sin(time*2+number)*.5);
            triangle(mesh,a,b,p+V(0,6.9,0),c,false);
        }
        for(int i=0;i<12;++i) {
            V a=left+(r-left)*(double(i)/12)+V(0,7.8,0);
            V b=left+(r-left)*(double(i+1)/12)+V(0,7.8,0);
            quad(mesh,a,b,b+V(0,.6,0),a+V(0,.6,0),i%2?ink:c,false);
        }
        world(mesh);
    }
    void buggy(const Car& car,int index,const Course& course,double time) {
        if(glm::length(car.position-eye)>500) return;
        Mesh m; C paint=index==0?C(.24,.63,.63):index==1?C(.88,.35,.18):C(.76,.73,.61);
        box(m,{-.83,.10,-1.6},{.83,.63,1.5},paint);
        quad(m,{-.84,.6,.2},{.84,.6,.2},{.67,.75,1.4},{-.67,.75,1.4},paint*1.15f);
        tube(m,{-.8,.65,-1.2},{-.75,1.65,-.6},.065,ink);tube(m,{.8,.65,-1.2},{.75,1.65,-.6},.065,ink);
        tube(m,{-.75,1.65,-.6},{.75,1.65,-.6},.065,ink);tube(m,{-.75,1.65,-.6},{-.8,.65,.8},.065,ink);
        tube(m,{.75,1.65,-.6},{.8,.65,.8},.065,ink);tube(m,{-.8,.65,.8},{.8,.65,.8},.065,ink);
        box(m,{-.35,.55,-.7},{.35,1.25,-.4},ink);
        infinity::appendSphere(m,{0,1.4,-.15},.23,{.9,.83,.66},6,8);
        for(int side:{-1,1}) for(int end:{-1,1}) {
            V a(side*.82,.15,end*1.02),b(side*1.25,.15,end*1.02);
            tube(m,a,b,.49,ink,12);tube(m,b,b+V(side*.015,0,0),.24,C(.42,.43,.4),10);
        }
        tube(m,{.9,.6,-1.2},{.9,3.6,-1.2},.025,ink,4);
        triangle(m,{.9,3.6,-1.2},{.9,3.15,-1.2},{.9,3.4,-2.1},paint,false);
        glm::dmat4 transform=glm::translate(glm::dmat4(1),car.position)*glm::rotate(glm::dmat4(1),car.yaw,V(0,1,0))*
            glm::rotate(glm::dmat4(1),car.pitch,V(1,0,0))*glm::rotate(glm::dmat4(1),car.roll,V(0,0,1));
        for(auto& v:m.vertices) {V p=transform*glm::dvec4(v.x,v.y,v.z,1);v.x=float(p.x);v.y=float(p.y);v.z=float(p.z);}
        world(m);
        if(car.speed>3) {
            Mesh dust;
            for(int i=0;i<22;++i) {
                double age=std::fmod(time*.8+i*.173,1.);
                V p=car.position-V(std::sin(car.yaw),0,std::cos(car.yaw))*(3+age*22);
                p.x+=std::sin(i*6.4)*age*4;p.z+=std::cos(i*3.2)*age*3;
                p.y=course.height(p.x,p.z)+.6+age*2;
                V right=V(view[0][0],view[1][0],view[2][0])*(.4+age*2.5);
                V up(0,.4+age*1.7,0);
                uint32_t first=uint32_t(dust.vertices.size());
                vertex(dust,p,C(.77,.59,.37),float((1-age)*.23));
                for(int k=0;k<=12;++k) {
                    double angle=k*2*pi/12;
                    vertex(dust,p+right*std::cos(angle)+up*std::sin(angle),C(.77,.59,.37),0);
                    if(k) dust.indices.insert(dust.indices.end(),{first,first+uint32_t(k),first+uint32_t(k)+1});
                }
            }
            world(dust,{},true);
        }
    }
    std::optional<glm::dvec2> project(V point) const {
        auto p=projection*view*glm::dvec4(point-eye,1);
        if(p.w<=.05) return {};
        return glm::dvec2((p.x/p.w*.5+.5)*canvasWidth(),(.5-p.y/p.w*.5)*canvasHeight());
    }
    void cockpit(const Race& race,double time) {
        const auto& car=race.player;
        Mesh m;
        // The cockpit is 3D and moves with the chassis, independent of free look.
        C paint(.88,.51,.17), frame(.08,.095,.095), metal(.25,.29,.29);
        quad(m,{-1.12,-.67,-.8},{1.12,-.67,-.8},{.83,-.77,-3.0},{-.83,-.77,-3.0},paint);
        quad(m,{-.83,-.77,-3.0},{.83,-.77,-3.0},{.74,-.98,-3.5},{-.74,-.98,-3.5},paint*.8f);
        tube(m,{-.96,-.66,-1.0},{-.73,-.76,-2.95},.045,frame);tube(m,{.96,-.66,-1.0},{.73,-.76,-2.95},.045,frame);
        quad(m,{-.13,-.66,-.82},{.13,-.66,-.82},{.1,-.755,-3.0},{-.1,-.755,-3.0},frame,false);
        for(int i=0;i<6;++i) box(m,{-.38+i*.15,-.766,-2.55},{-.31+i*.15,-.75,-2.20},frame);
        // Cage stays at the edges of the driver's view.
        tube(m,{-1.45,-.7,-.6},{-1.7,.95,-1.4},.030,frame,10);
        tube(m,{1.45,-.7,-.6},{1.7,.95,-1.4},.030,frame,10);
        tube(m,{-1.7,.95,-1.4},{1.7,.95,-1.4},.030,frame,10);
        tube(m,{-1.08,-.7,-.6},{-1.37,-.65,-2.2},.04,metal);
        tube(m,{1.08,-.7,-.6},{1.37,-.65,-2.2},.04,metal);
        box(m,{-1.06,-.87,-.99},{1.06,-.56,-.80},frame);
        // Front tyres and their exposed suspension.
        for(int side:{-1,1}) {
            tube(m,{side*.85,-.91,-2.15},{side*1.15,-.91,-2.15},.37,frame,14);
            tube(m,{side*.65,-.75,-1.6},{side*1.02,-.86,-2.15},.045,metal);
            tube(m,{side*.64,-.85,-2.3},{side*1.02,-.86,-2.15},.04,metal);
        }
        V center(-.25,-.55,-.92);double radius=.27;
        double turn=-car.steering*.7;
        for(int i=0;i<40;++i) {
            double a=i*2*pi/40,b=(i+1)*2*pi/40;
            tube(m,center+V(std::cos(a)*radius,std::sin(a)*radius*.83,0),center+V(std::cos(b)*radius,std::sin(b)*radius*.83,0),.025,frame,5);
        }
        for(int i=0;i<3;++i) {
            double a=i*2*pi/3+turn;
            tube(m,center,center+V(std::cos(a)*radius,std::sin(a)*radius*.83,0),.015,metal,5);
        }
        infinity::appendSphere(m,center,.055,paint,5,8);
        // Driver's gloved hands follow the wheel.
        for(double a:{pi*.87+turn,pi*.13+turn}) {
            V hand=center+V(std::cos(a)*radius,std::sin(a)*radius*.83,.02);
            infinity::appendSphere(m,hand,.059,C(.57,.49,.34),5,8);
            tube(m,hand+V(0,-.03,.02),hand+V(a>1?-.04:.08,-.21,.15),.05,C(.16,.21,.22));
        }
        glm::dmat4 localView=glm::rotate(glm::dmat4(1),-lookPitch,V(1,0,0));
        localView=glm::rotate(localView,lookYaw,V(0,1,0));
        double bounce=std::sin(time*19)*std::min(std::abs(car.speed)*.00025,.009);
        localView=glm::translate(localView,V(0,bounce,0));
        auto v=glm::mat4(localView),proj=glm::perspectiveRH_ZO(glm::radians(80.f),float(width)/height,.03f,30.f);
        bgfx::setViewTransform(2,glm::value_ptr(v),glm::value_ptr(proj));
        submit(m,2,BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_LESS|BGFX_STATE_MSAA);
    }
    double lookYaw=0,lookPitch=0;
#include "Hud.inl"
#include "GunnerMap.inl"
#include "FractalScene.inl"
};
Renderer::Renderer(SDL_Window* window) : p(std::make_unique<Impl>(window)) {
    bgfx::Init init;init.callback=&p->callbacks;init.fallback=false;
    auto props=SDL_GetWindowProperties(window);
#if defined(__APPLE__)
    init.type=bgfx::RendererType::Metal;init.swapChain.nwh=SDL_GetPointerProperty(props,SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,nullptr);
#elif defined(_WIN32)
    init.type=bgfx::RendererType::Direct3D11;init.swapChain.nwh=SDL_GetPointerProperty(props,SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
#else
    init.type=bgfx::RendererType::Vulkan;init.swapChain.nwh=reinterpret_cast<void*>(static_cast<uintptr_t>(SDL_GetNumberProperty(props,SDL_PROP_WINDOW_X11_WINDOW_NUMBER,0)));
    init.swapChain.ndt=SDL_GetPointerProperty(props,SDL_PROP_WINDOW_X11_DISPLAY_POINTER,nullptr);
#endif
    SDL_GetWindowSizeInPixels(window,&p->width,&p->height);
    init.swapChain.width=p->width;init.swapChain.height=p->height;init.swapChain.flags=BGFX_SWAP_CHAIN_NONE;
    init.swapChain.formatDepthStencil=bgfx::TextureFormat::Count;
    init.reset=BGFX_RESET_NONE;init.limits.maxTransientVbSize=24*1024*1024;init.limits.maxTransientIbSize=16*1024*1024;
    if(!bgfx::init(init)) throw std::runtime_error("Infinity graphics initialization failed");

    p->swap=init.swapChain;
    p->layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float).add(bgfx::Attrib::Color0,4,bgfx::AttribType::Uint8,true).end();
    p->program=bgfx::createProgram(bgfx::createEmbeddedShader(shaders,bgfx::getRendererType(),"vs_debugdraw_lines"),bgfx::createEmbeddedShader(shaders,bgfx::getRendererType(),"fs_debugdraw_lines"),true);
    if(!bgfx::isValid(p->program)) throw std::runtime_error("Infinity colour shaders unavailable");
    p->gpu=std::make_unique<FractalRenderer>();
    p->font=std::make_unique<HudFont>();p->font->init();
    for(int i=0;i<4;++i) bgfx::setViewMode(i,bgfx::ViewMode::Sequential);
}
Renderer::~Renderer() {if(p->measuredFrames)std::fprintf(stderr,"RENDER_CPU prepare_ms=%.3f submit_wait_ms=%.3f\n",p->prepareSeconds*1000/p->measuredFrames,p->submitWaitSeconds*1000/p->measuredFrames);p->font.reset();p->gpu.reset();if(bgfx::isValid(p->program)) bgfx::destroy(p->program);bgfx::shutdown();}
UiAction Renderer::actionAt(float windowX,float windowY)const {
    int w,h;SDL_GetWindowSize(p->window,&w,&h);if(w<1||h<1)return UiAction::None;
    auto uv=p->gpu->displayToBeam({windowX/w,windowY/h});
    double x=uv.x*p->canvasWidth(),y=uv.y*p->canvasHeight();
    for(auto it=p->buttons.rbegin();it!=p->buttons.rend();++it)if(x>=it->x&&x<=it->x+it->w&&y>=it->y&&y<=it->y+it->h)return it->action;
    return UiAction::None;
}
void Renderer::moveGunnerCursor(double dx,double dy){int w,h;SDL_GetWindowSize(p->window,&w,&h);auto area=GunnerMap{}.rect;gunnerCursor=glm::clamp(gunnerCursor+glm::dvec2(dx/(std::max(1,w)*area.z),dy/(std::max(1,h)*area.w)),glm::dvec2(.015),glm::dvec2(.985));}
void Renderer::pointGunnerAt(const Race& race,glm::dvec3 point){int w,h;SDL_GetWindowSize(p->window,&w,&h);auto map=makeGunnerMap(race.player,double(w)/std::max(1,h));gunnerCursor=glm::clamp(map.project({point.x,point.z}),glm::dvec2(.015),glm::dvec2(.985));}
void Renderer::resetGunnerAim(const Race& race){auto f=gunDirection(race.player);glm::dvec2 horizontal(f.x,f.z);if(glm::length(horizontal)<.05)horizontal=mapForward(race.player);else horizontal=glm::normalize(horizontal);pointGunnerAt(race,race.player.position+glm::dvec3(horizontal.x*22,0,horizontal.y*22));contactClock=0;}
bool Renderer::updateGunnerAim(Race& race,double dt){int w,h;SDL_GetWindowSize(p->window,&w,&h);auto map=makeGunnerMap(race.player,double(w)/std::max(1,h));auto target=pickGunnerTarget(race,map,gunnerCursor);aimTurret(race.player,target.point,dt);gunnerAim=evaluateGunnerAim(race,target);
    contactClock-=dt;if(contactClock<=0){contactClock=.12;for(int i=0;i<3;++i){auto point=race.rivals[i].position+race.rivals[i].orientation()*glm::dvec3(0,.15,0);auto delta=point-gunCamera(race.player);auto hit=traceBullet(race,0,gunCamera(race.player),delta,glm::length(delta)+2);gunnerVisible[i]=hit.car==i+1;}}
    return gunnerAim.canFire();
}
void Renderer::capture(const std::string& path) { bgfx::requestScreenShot(BGFX_INVALID_HANDLE,path.c_str()); }
void Renderer::draw(const Race& race,double time,double best,bool muted,bool help) {
    auto started=std::chrono::steady_clock::now();
    int w,h;SDL_GetWindowSizeInPixels(p->window,&w,&h);if(w<1 || h<1) return;
    if(w!=p->swap.width || h!=p->swap.height) {p->swap.width=w;p->swap.height=h;bgfx::reset(BGFX_RESET_NONE,&p->swap);}
    // Full-resolution vector cores and HUD; diffuse CRT work stays bounded.
    double beamLimit=highResolution?2048.:1024.;
    double beamScale=std::min({1.,beamLimit/w,beamLimit*.75/h});
    p->width=std::max(1,int(std::round(w*beamScale)));p->height=std::max(1,int(std::round(h*beamScale)));
    p->gpu->beginDisplay(p->width,p->height,crtEffects);p->beginUi();p->font->begin();activeFont=p->font->ready()?p->font.get():nullptr;
    p->fractalDraw(*this,race,time,best,muted,help);p->font->submit(3);p->gpu->finishDisplay(w,h);activeFont=nullptr;
    auto prepared=std::chrono::steady_clock::now();bgfx::frame();auto submitted=std::chrono::steady_clock::now();
    p->prepareSeconds+=std::chrono::duration<double>(prepared-started).count();p->submitWaitSeconds+=std::chrono::duration<double>(submitted-prepared).count();++p->measuredFrames;
}
}
