static const char* displayHelpers=R"MSL(
float beam(float3 c){return max(c.r,max(c.g,c.b));}
float2 crt_energy(float4 c){float light=beam(c.rgb),gate=clamp(1.-c.a,0.,1.);return float2(light*(1.-gate),light*gate);}
float3 crt_overlay(float2 uv,bool split) {
 // Fixed acetate zones: cool driver colours, warm gunner colours.
 if(split){
  bool gunner=uv.x>=.5;float x=uv.x*2.-(gunner?1.:0.);
  float3 cyan=float3(.35,.95,1.),green=float3(.55,1.,.44),blue=float3(.42,.45,1.);
  float3 magenta=float3(1.,.36,.90),coral=float3(1.,.36,.28),yellow=float3(1.,.94,.32);
  float3 identity=gunner?magenta:cyan;
  float3 field=mix(gunner?coral:blue,identity,smoothstep(.215,.225,x));
  field=mix(field,gunner?yellow:green,smoothstep(.725,.735,x));
  float3 instruments=mix(gunner?coral:green,gunner?yellow:cyan,smoothstep(.355,.365,x));
  instruments=mix(instruments,gunner?magenta:yellow,smoothstep(.785,.795,x));
  float3 tint=mix(identity,field,smoothstep(.205,.220,uv.y));
  return mix(tint,instruments,smoothstep(.720,.735,uv.y));
 }
 float3 tint=float3(.55,1.,.44),topTint=float3(.40,.96,1.);
 topTint=mix(topTint,float3(1.,.36,.28),1.-smoothstep(.318,.322,uv.x));
 topTint=mix(topTint,float3(.42,.45,1.),smoothstep(.778,.782,uv.x));
 float3 bottomTint=float3(1.,.36,.90);
 bottomTint=mix(bottomTint,float3(1.,.94,.32),1.-smoothstep(.298,.302,uv.x));
 bottomTint=mix(bottomTint,float3(.35,.95,1.),smoothstep(.758,.762,uv.x));
 return mix(mix(tint,topTint,1.-smoothstep(.17,.19,uv.y)),bottomTint,smoothstep(.73,.75,uv.y));
}
float crt_grain(uint2 p){uint h=p.x*1664525u^p.y*1013904223u;h^=h>>16;h*=2246822519u;h^=h>>13;return float(h&1023u)/1023.;}
)MSL";
static const char* presentFragment=R"MSL(
struct Input {float4 position [[position]];float2 uv [[user(locn0)]];};
fragment float4 xlatMtlMain(Input in [[stage_in]],constant Global& _mtl_u [[buffer(0)]],texture2d<float> s_scene [[texture(0)]],sampler s_sceneSampler [[sampler(0)]],texture2d<float> s_depth [[texture(1)]],sampler s_depthSampler [[sampler(1)]],texture2d<float> s_carDepth [[texture(2)]],sampler s_carDepthSampler [[sampler(2)]]) {
 float2 uv=in.uv;bool crt=_mtl_u.u_fractal[72].w>.5;
 float2 signal=crt?v_display_uv(uv)+_mtl_u.u_fractal[73].xy:uv;
 float4 source=s_depth.sample(s_depthSampler,signal),coefficient=s_carDepth.sample(s_carDepthSampler,uv);
 if(_mtl_u.u_fractal[6].y>0)return float4(source.rgb*coefficient.a,1);
 float2 core=crt_energy(source);
 // Only warp, sample and combine here; colour, glass and focus stay in the bounded pass.
 float3 color=s_scene.sample(s_sceneSampler,uv).rgb+coefficient.rgb*core.x+float3(coefficient.a*core.y);
 if(crt)color=color*1.12/(1.+max(color,float3(0))*.12);
 return float4(clamp(color,0.,1.),1);
}
)MSL";
static const char* phosphorFragment=R"MSL(
struct Input {float4 position [[position]];float2 uv [[user(locn0)]];};
float2 crt_energy(float4 c){float light=max(c.r,max(c.g,c.b)),gate=clamp(1.-c.a,0.,1.);return float2(light*(1.-gate),light*gate);}
fragment float4 xlatMtlMain(Input in [[stage_in]],constant Global& _mtl_u [[buffer(0)]],texture2d<float> s_scene [[texture(0)]],sampler s_sceneSampler [[sampler(0)]],texture2d<float> s_depth [[texture(1)]],sampler s_depthSampler [[sampler(1)]]) {
 float2 emission=float2(0),pixel=1./ceil(_mtl_u.u_fractal[90].zw/4.)/4.;
 // Integrate the thin native beams rather than picking one texel per coarse cell.
 for(int y=0;y<4;++y)for(int x=0;x<4;++x)emission+=crt_energy(s_scene.sample(s_sceneSampler,in.uv+(float2(x,y)-1.5)*pixel));
 emission*=1./16.;float4 previous=float4(0);
 if(_mtl_u.u_fractal[72].z>.5)previous=s_depth.sample(s_depthSampler,in.uv);
 float dt=_mtl_u.u_fractal[72].x;
 return float4(max(emission,previous.xy*exp(-dt/.018)),max(emission,previous.zw*exp(-dt/.110)));
}
)MSL";
static const char* displayFragment=R"MSL(
struct Input {float4 position [[position]];float2 uv [[user(locn0)]];};
struct DisplayOutput {float4 glow [[color(0)]];float4 coefficient [[color(1)]];};
fragment DisplayOutput xlatMtlMain(Input in [[stage_in]],constant Global& _mtl_u [[buffer(0)]],texture2d<float> s_scene [[texture(0)]],sampler s_sceneSampler [[sampler(0)]],texture2d<float> s_depth [[texture(1)]],sampler s_depthSampler [[sampler(1)]]) {
 float2 uv=in.uv,pixel=1./_mtl_u.u_fractal[90].zw;
 bool crt=_mtl_u.u_fractal[72].w>.5;float2 q=(uv-.5)/.465;
 float distance=crt?v_display_distance(uv):-1.;float mask=crt?1.-smoothstep(-pixel.y*1.5,pixel.y*1.5,distance):1.;
 float grain=crt_grain(uint2(in.position.xy));
 float lip=crt?.018/(1.+pow((distance-.016)*100.,2.)):0.;
 float3 bezel=float3(.004,.005,.006)+float3(lip)*(.78-.18*clamp(q.y,-1.,1.))+(grain-.5)*.0015;
 if(mask<.001)return DisplayOutput{float4(max(bezel,float3(0)),1),float4(0)};
 float2 signal=crt?v_display_uv(uv)+_mtl_u.u_fractal[73].xy:uv;
 float4 original=s_scene.sample(s_sceneSampler,signal);
 if(_mtl_u.u_fractal[6].y>0)return DisplayOutput{float4(0,0,0,1),float4(mask)};
 const float2 directions[]={float2(1,0),float2(-1,0),float2(0,1),float2(0,-1)};
 float2 core=crt_energy(original),halo=float2(0);
 if(!crt) {
  float2 beamPixel=1./_mtl_u.u_fractal[90].xy;
  for(int i=0;i<4;++i)halo+=crt_energy(s_scene.sample(s_sceneSampler,signal+directions[i]*beamPixel*2.))*.045;
  float3 tint=crt_overlay(uv,_mtl_u.u_fractal[89].z>.5);
  return DisplayOutput{float4(tint*halo.x+float3(1.,.76,.08)*halo.y,1),float4(tint*.96,.96)};
 }
 float2 historyPixel=1./float2(s_depth.get_width(),s_depth.get_height());
 float4 phosphor=s_depth.sample(s_depthSampler,signal),bloom=phosphor*.25;
 for(int i=0;i<4;++i)bloom+=s_depth.sample(s_depthSampler,signal+directions[i]*historyPixel*1.6)*.1875;
 float bloomGain=.32/(1.+(phosphor.x+phosphor.y)*3.);
 halo=phosphor.xy*.18;float2 light=halo*.82+bloom.xy*bloomGain+bloom.zw*.045;
 float3 tint=crt_overlay(q*.5+.5,_mtl_u.u_fractal[89].z>.5);
 float gain=_mtl_u.u_fractal[73].z*(1.+(grain-.5)*.012);
 float3 color=tint*light.x*(1.-.035*dot(q,q))+float3(1.,.76,.08)*(halo.y+bloom.y*.40+bloom.w*.065);
 color+=float3(dot(core,float2(1))*.012);
 gain*=1.-.12*pow(clamp(max(abs(q.x),abs(q.y)),0.,1.),3.);color*=gain;
 float2 lamp=(q+float2(.55,.72))*float2(1.25,2.8);
 float reflection=.009/(1.+pow(dot(lamp,lamp)*2.5,2.));
 float arc=abs(q.y+.84-.18*q.x*q.x);reflection+=.003/(1.+arc*arc*1800.)*(1.-smoothstep(-.7,.1,q.x));
 float3 glass=float3(.0015,.0023,.0025)+float3(reflection);
 // Only diffuse light is downsampled; live vector cores are added at output resolution.
 return DisplayOutput{float4(max((color+glass)*mask+bezel*(1.-mask),float3(0)),1),float4(tint*.98*(1.-.035*dot(q,q))*gain*mask,1.04*gain*mask)};
}
)MSL";
void FractalRenderer::beginDisplay(int width,int height,bool crtEffects) {
 if(bgfx::getRendererType()!=bgfx::RendererType::Metal)return;
 if(p->crtEffects!=crtEffects){p->phosphorValid=false;p->phosphorReset=true;}p->crtEffects=crtEffects;
 if(!bgfx::isValid(p->presentProgram))p->presentProgram=bgfx::createProgram(shader(std::string(prelude)+vertex,false),shader(std::string(prelude)+fractalFieldSource+worldHelpers+vectorStyleSource+displayHelpers+presentFragment,true,3),true);
 if(!bgfx::isValid(p->displayProgram))p->displayProgram=bgfx::createProgram(shader(std::string(prelude)+vertex,false),shader(std::string(prelude)+fractalFieldSource+worldHelpers+vectorStyleSource+displayHelpers+displayFragment,true,2),true);
 if(!bgfx::isValid(p->phosphorProgram))p->phosphorProgram=bgfx::createProgram(shader(std::string(prelude)+vertex,false),shader(std::string(prelude)+phosphorFragment,true,2),true);
 double scale=std::min({1.,1024./width,768./height});
 int cw=std::max(1,int(std::round(width*scale))),ch=std::max(1,int(std::round(height*scale)));
 p->data[90]={width,height,cw,ch};
 if(width!=p->displayWidth||height!=p->displayHeight){
  p->crtWidth=cw;p->crtHeight=ch;
  if(bgfx::isValid(p->presentTarget))bgfx::destroy(p->presentTarget);if(bgfx::isValid(p->presentTexture))bgfx::destroy(p->presentTexture);if(bgfx::isValid(p->presentCoefficients))bgfx::destroy(p->presentCoefficients);
  p->presentTexture=bgfx::createTexture2D(cw,ch,false,1,bgfx::TextureFormat::RGBA16F,BGFX_TEXTURE_RT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP);p->presentCoefficients=bgfx::createTexture2D(cw,ch,false,1,bgfx::TextureFormat::RGBA16F,BGFX_TEXTURE_RT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP);
  bgfx::TextureHandle finishing[]={p->presentTexture,p->presentCoefficients};p->presentTarget=bgfx::createFrameBuffer(2,finishing,false);
  if(bgfx::isValid(p->displayTarget))bgfx::destroy(p->displayTarget);if(bgfx::isValid(p->displayTexture))bgfx::destroy(p->displayTexture);
  if(bgfx::isValid(p->worldTarget))bgfx::destroy(p->worldTarget);if(bgfx::isValid(p->worldColor))bgfx::destroy(p->worldColor);if(bgfx::isValid(p->worldDepth))bgfx::destroy(p->worldDepth);if(bgfx::isValid(p->objectDepth))bgfx::destroy(p->objectDepth);
  const uint64_t objectFlags=BGFX_TEXTURE_RT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT;
  p->worldColor=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA16F,objectFlags);p->worldDepth=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::R32F,objectFlags);
  p->objectDepth=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::D32F,objectFlags);
  bgfx::TextureHandle objectAttachments[]={p->worldColor,p->worldDepth,p->objectDepth};p->worldTarget=bgfx::createFrameBuffer(3,objectAttachments,false);
  p->displayTexture=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP);
  p->displayTarget=bgfx::createFrameBuffer(1,&p->displayTexture,false);p->displayWidth=width;p->displayHeight=height;
  for(int i=0;i<2;++i){
   if(bgfx::isValid(p->phosphorTarget[i]))bgfx::destroy(p->phosphorTarget[i]);if(bgfx::isValid(p->phosphorTexture[i]))bgfx::destroy(p->phosphorTexture[i]);
   p->phosphorTexture[i]=bgfx::createTexture2D(std::max(1,(cw+3)/4),std::max(1,(ch+3)/4),false,1,bgfx::TextureFormat::RGBA16F,BGFX_TEXTURE_RT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP);
   p->phosphorTarget[i]=bgfx::createFrameBuffer(1,&p->phosphorTexture[i],false);
  }
  p->phosphorValid=false;p->phosphorReset=true;
 }
 for(int view:{0,2,3})bgfx::setViewFrameBuffer(view,p->displayTarget);
 bgfx::setViewClear(2,BGFX_CLEAR_COLOR,0x000000ff);
 const bgfx::ViewId order[]={5,6,1,4,0,7,2,3,8,9,10};bgfx::setViewOrder(0,11,order);
}
void FractalRenderer::finishDisplay(int width,int height) {
 if(!bgfx::isValid(p->displayProgram))return;
 auto now=std::chrono::steady_clock::now();double dt=p->displayFrame.time_since_epoch().count()?std::chrono::duration<double>(now-p->displayFrame).count():1./60.;
 if(dt>.25){p->phosphorValid=false;p->phosphorReset=true;}
 p->displayFrame=now;dt=std::clamp(dt,.001,.1);p->displayTime+=dt;
 double t=p->displayTime;
 p->beamJitter=p->crtEffects?glm::vec2((std::sin(t*13.7)*.065+std::sin(t*47.3)*.025)/p->displayWidth,(std::sin(t*17.3+.8)*.060+std::sin(t*41.1)*.025)/p->displayHeight):glm::vec2(0);
 p->data[72]={dt,t,p->phosphorValid&&!p->phosphorReset?1.f:0.f,p->crtEffects?1.f:0.f};
 p->data[73]={p->beamJitter.x,p->beamJitter.y,1.+std::sin(t*37.)*.003+std::sin(t*71.+2)*.002,0};
 bool history=p->crtEffects&&p->lastDebug==0;int write=p->phosphorIndex,read=1-write;
 if(history){
  bgfx::setViewFrameBuffer(8,p->phosphorTarget[write]);bgfx::setViewRect(8,0,0,std::max(1,(p->crtWidth+3)/4),std::max(1,(p->crtHeight+3)/4));bgfx::setViewClear(8,BGFX_CLEAR_NONE);
  bgfx::setUniform(p->uniform,p->data.data(),160);bgfx::setTexture(0,p->sampler,p->displayTexture);bgfx::setTexture(1,p->depthSampler,p->phosphorTexture[read]);
  bgfx::setVertexBuffer(0,p->quad);bgfx::setIndexBuffer(p->indices);bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);bgfx::submit(8,p->phosphorProgram);
  p->phosphorValid=true;p->phosphorReset=false;p->phosphorIndex=read;
 }else p->phosphorValid=false;
 bgfx::setViewFrameBuffer(9,p->presentTarget);bgfx::setViewRect(9,0,0,p->crtWidth,p->crtHeight);bgfx::setViewClear(9,BGFX_CLEAR_NONE);
 bgfx::setUniform(p->uniform,p->data.data(),160);bgfx::setTexture(0,p->sampler,p->displayTexture);bgfx::setTexture(1,p->depthSampler,history?p->phosphorTexture[write]:p->displayTexture);
 bgfx::setVertexBuffer(0,p->quad);bgfx::setIndexBuffer(p->indices);bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);bgfx::submit(9,p->displayProgram);
 bgfx::setViewFrameBuffer(10,BGFX_INVALID_HANDLE);bgfx::setViewRect(10,0,0,width,height);bgfx::setViewClear(10,BGFX_CLEAR_NONE);
 bgfx::setUniform(p->uniform,p->data.data(),160);bgfx::setTexture(0,p->sampler,p->presentTexture);bgfx::setTexture(1,p->depthSampler,p->displayTexture);bgfx::setTexture(2,p->carDepthSampler,p->presentCoefficients);
 bgfx::setVertexBuffer(0,p->quad);bgfx::setIndexBuffer(p->indices);bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);bgfx::submit(10,p->presentProgram);
}
glm::dvec2 FractalRenderer::displayToBeam(glm::dvec2 uv) const {
 if(!p->crtEffects||bgfx::getRendererType()!=bgfx::RendererType::Metal)return uv;
 if(vectorStyle::v_display_distance(glm::vec2(uv))>0)return {-1,-1};
 return glm::dvec2(vectorStyle::v_display_uv(glm::vec2(uv))+p->beamJitter);
}
