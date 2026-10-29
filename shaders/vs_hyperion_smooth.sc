$input a_normal, a_position, a_color0, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_color0, v_position, v_pattern, v_sphere, v_elevation
#include <bgfx_shader.sh>
uniform mat4 u_pattern;
uniform vec4 u_materialDomain;
void main() {
    mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
    vec4 world = mul(model, vec4(a_position, 1.0));
    gl_Position = mul(u_viewProj, world);
    v_position = world.xyz;
    v_pattern = mul(u_pattern, world).xyz;
    // Interpolate the authored vertex height, not the radius of a chord
    // through the planet. Coarse triangles must not invent lowland bands.
    v_elevation = length(v_pattern) - 1.0;
    if(u_materialDomain.x>3.5 && u_materialDomain.x<4.5)
        v_pattern=a_position*vec3(length(i_data0.xyz),length(i_data1.xyz),length(i_data2.xyz))*u_materialDomain.z;
    vec3 normal = vec3(a_normal, 1. - abs(a_normal.x) - abs(a_normal.y));
    if(normal.z < 0.) normal.xy = (1. - abs(normal.yx)) * step(vec2(0.), normal.xy) * 2. - (1. - abs(normal.yx));
    v_sphere = normalize(mul(mat3(model), normalize(normal)));
    v_color0 = a_color0 * i_data4;
}
