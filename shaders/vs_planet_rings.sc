$input a_position
$output v_pattern
#include <bgfx_shader.sh>
uniform vec4 u_rings[4];
void main(){
 v_pattern=a_position*u_rings[0].y;
 gl_Position=mul(u_modelViewProj,vec4(a_position,1.));
}
