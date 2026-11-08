#include "HudFont.hpp"
#include <bgfx/embedded_shader.h>
#include <vs_debugfont.bin.h>
#include <fs_debugfont.bin.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace desert {
static const bgfx::EmbeddedShader fontShaders[]={BGFX_EMBEDDED_SHADER(vs_debugfont),BGFX_EMBEDDED_SHADER(fs_debugfont),BGFX_EMBEDDED_SHADER_END()};
void HudFont::init(){
    // Each pair is an original pen coordinate; semicolons lift the pen.
    const std::pair<char,const char*> paths[]={
        {'A',"08 02 20 40 62 68;04 64"},{'B',"08 00 40 62 44 04;44 66 48 08"},
        {'C',"60 20 02 06 28 68"},{'D',"08 00 40 62 66 48 08"},
        {'E',"60 00 08 68;04 54"},{'F',"08 00 60;04 54"},
        {'G',"60 20 02 06 28 68 64 34"},{'H',"00 08;60 68;04 64"},
        {'I',"00 20;10 18;08 28"},{'J',"20 60 66 48 28 06"},
        {'K',"00 08;60 04 68"},{'L',"00 08 68"},
        {'M',"08 00 34 60 68"},{'N',"08 00 68 60"},
        {'O',"20 40 62 66 48 28 06 02 20"},{'P',"08 00 40 62 44 04"},
        {'Q',"20 40 62 66 48 28 06 02 20;45 68"},{'R',"08 00 40 62 44 04;34 68"},
        {'S',"60 20 02 24 44 66 48 08"},{'T',"00 60;30 38"},
        {'U',"00 06 28 48 66 60"},{'V',"00 38 60"},
        {'W',"00 18 44 78 80"},{'X',"00 68;60 08"},
        {'Y',"00 34 60;34 38"},{'Z',"00 60 08 68"},
        {'0',"20 40 62 66 48 28 06 02 20;06 62"},
        {'1',"02 20 28;08 48"},{'2',"02 20 40 62 44 08 68"},
        {'3',"00 60 34 66 48 08;34 14"},{'4',"40 04 64;40 48"},
        {'5',"60 00 04 44 66 48 08"},{'6',"60 20 02 06 28 48 66 44 04"},
        {'7',"00 60 28"},{'8',"20 40 62 44 24 02 20;24 06 28 48 66 44"},
        {'9',"64 24 02 20 40 62 66 48 08"},
        {'.',"08 18"},{',',"18 09"},{':',"02 12;06 16"},{';',"02 12;16 08"},
        {'-',"04 54"},{'_',"08 68"},{'/',"08 60"},{'\\',"00 68"},
        {'!',"00 05;08 18"},{'?',"02 20 40 62 44 34 36;38 48"},
        {'(',"30 12 16 38"},{')',"00 22 26 08"},
        {'[',"30 00 08 38"},{']',"00 30 38 08"},
        {'+',"03 63;30 36"},{'=',"02 62;06 66"},
        {'<',"50 04 58"},{'>',"00 54 08"},{'|',"00 08"},
        {'\'',"00 12"},{'\"',"00 02;20 22"},
        {'%',"08 60;00 20 22 02 00;46 66 68 48 46"},
        {'#',"20 08;60 48;02 72;06 76"},{'*',"04 64;10 58;50 18"}
    };
    for(const auto& [code,path]:paths){
        auto& glyph=glyphs[uint8_t(code)];float previousX=0,previousY=0,maxX=0;bool pen=false;
        for(const char* c=path;*c;){
            if(*c==';'){pen=false;++c;continue;}if(*c==' '){++c;continue;}
            float x=float(c[0]-'0'),y=float(c[1]-'0');c+=2;maxX=std::max(maxX,x);
            if(pen)glyph.strokes.push_back({previousX*.07f,previousY*.09f,x*.07f,y*.09f});
            previousX=x;previousY=y;pen=true;
        }
        glyph.advance=(maxX+1.2f)*.07f;
    }
    glyphs[' '].advance=.29f;
    const uint8_t opaque=255;
    texture=bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::R8,0,bgfx::copy(&opaque,1));
    sampler=bgfx::createUniform("s_texColor",bgfx::UniformType::Sampler);
    program=bgfx::createProgram(bgfx::createEmbeddedShader(fontShaders,bgfx::getRendererType(),"vs_debugfont"),bgfx::createEmbeddedShader(fontShaders,bgfx::getRendererType(),"fs_debugfont"),true);
    layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float).add(bgfx::Attrib::Color0,4,bgfx::AttribType::Uint8,true).add(bgfx::Attrib::Color1,4,bgfx::AttribType::Uint8,true).add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float).end();
}
HudFont::~HudFont(){if(bgfx::isValid(texture))bgfx::destroy(texture);if(bgfx::isValid(program))bgfx::destroy(program);if(bgfx::isValid(sampler))bgfx::destroy(sampler);}
double HudFont::width(const std::string& s,double size)const{double result=0;for(unsigned char c:s)if(c<128)result+=glyphs[std::toupper(c)].advance*size;return result;}
void HudFont::text(const std::string& s,double x,double y,double size,uint32_t color){
    uint32_t a=color>>24;color=((color&255)*a/255)|((((color>>8)&255)*a/255)<<8)|((((color>>16)&255)*a/255)<<16)|(a<<24);
    float radius=float(std::clamp(size*.017,.70,1.55)*.5),outer=radius+.55f;
    for(unsigned char c:s){if(c>=128)continue;const auto& glyph=glyphs[std::toupper(c)];
        for(const auto& segment:glyph.strokes){
            if(vertices.size()+8>=65536)return;
            float x0=float(x+segment.x0*size),y0=float(y+(segment.y0+.12)*size),x1=float(x+segment.x1*size),y1=float(y+(segment.y1+.12)*size);
            float dx=x1-x0,dy=y1-y0,length=std::sqrt(dx*dx+dy*dy);if(length<.001)continue;dx/=length;dy/=length;
            x0-=dx*radius*.25f;y0-=dy*radius*.25f;x1+=dx*radius*.25f;y1+=dy*radius*.25f;
            auto first=uint16_t(vertices.size());const float distances[]={-outer,-radius,radius,outer};
            for(int row=0;row<4;++row){float d=distances[row];uint32_t foreground=row==0||row==3?0:color;
                vertices.push_back({x0-dy*d,y0+dx*d,0,foreground,0,.5,.5});vertices.push_back({x1-dy*d,y1+dx*d,0,foreground,0,.5,.5});}
            for(int row=0;row<3;++row){uint16_t i=first+row*2;indices.insert(indices.end(),{i,uint16_t(i+1),uint16_t(i+3),i,uint16_t(i+3),uint16_t(i+2)});}
        }
        x+=glyph.advance*size;
    }
}
void HudFont::submit(bgfx::ViewId view){
    if(!ready()||vertices.empty())return;
    bgfx::TransientVertexBuffer vb;bgfx::TransientIndexBuffer ib;
    if(!bgfx::allocTransientBuffers(&vb,layout,uint32_t(vertices.size()),&ib,uint32_t(indices.size())))return;
    std::memcpy(vb.data,vertices.data(),vertices.size()*sizeof(Vertex));std::memcpy(ib.data,indices.data(),indices.size()*sizeof(uint16_t));
    bgfx::setTexture(0,sampler,texture);bgfx::setVertexBuffer(0,&vb);bgfx::setIndexBuffer(&ib);
    bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA));bgfx::submit(view,program);
}
}
