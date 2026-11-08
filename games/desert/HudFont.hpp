#pragma once
#include <bgfx/bgfx.h>
#include <array>
#include <string>
#include <vector>
#include <cstdint>

namespace desert {
// Original single-stroke glyphs, emitted as antialiased vector segments.
class HudFont {
public:
    void init();
    ~HudFont();
    bool ready() const {return bgfx::isValid(texture)&&bgfx::isValid(program);}
    void begin(){vertices.clear();indices.clear();}
    double width(const std::string&,double size) const;
    void text(const std::string&,double x,double y,double size,uint32_t color);
    void submit(bgfx::ViewId);
private:
    struct Stroke {float x0,y0,x1,y1;};
    struct Glyph {float advance=.49f;std::vector<Stroke> strokes;};
    struct Vertex {float x,y,z;uint32_t foreground,background;float u,v;};
    std::array<Glyph,128> glyphs{};
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    bgfx::VertexLayout layout;
    bgfx::TextureHandle texture=BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program=BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler=BGFX_INVALID_HANDLE;
};
}
