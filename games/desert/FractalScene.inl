// A single fullscreen sphere-traced field supplies every visible 3D surface.
void presentSoftware() {
    Mesh screen;const auto& pixels=gpu->pixels();
    double sx=1600./gpu->frameWidth(),sy=900./gpu->frameHeight();
    for(int y=0;y<gpu->frameHeight();++y) {
        for(int x=0;x<gpu->frameWidth();) {
            uint32_t color=pixels[y*gpu->frameWidth()+x];int end=x+1;
            while(end<gpu->frameWidth()&&pixels[y*gpu->frameWidth()+end]==color) ++end;
            C c(float(color&255)/255,float((color>>8)&255)/255,float((color>>16)&255)/255);
            rect(screen,x*sx,y*sy,(end-x)*sx,sy,c,float(color>>24)/255);x=end;
        }
    }
    submit(screen,0,BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);
}
void fractalDraw(const Renderer& settings,const Race& race,double time,double best,bool muted,bool help) {
    bool vista=settings.vista&&settings.exhibit;bool split=!settings.exhibit;
    const auto& car=race.player;
    auto body=car.orientation();double shock=race.state==State::Racing?car.impactFlash*std::exp(-car.hitAge*3.):0;
    auto rotation=body*glm::angleAxis(-settings.lookYaw,V(0,1,0))*glm::angleAxis(settings.lookPitch+.10,V(1,0,0));
    ell::Camera camera{car.position+body*V(0,1.10+std::sin(time*65)*shock*.015,-.35),rotation,glm::radians(75.),double(width)/(height*(split?2:1)),.06,550};
    bool externalView=settings.chase||settings.exhibit||(body*V(0,1,0)).y<.2;
    if(externalView) {
        V forward=body*V(0,0,1),cameraEye=car.position-forward*6.7+V(0,2.5,0);
        if(settings.exhibit) {
            double horizontal=std::cos(settings.orbitPitch)*settings.orbitDistance;
            cameraEye=car.position+V(std::sin(settings.orbitYaw)*horizontal,std::sin(settings.orbitPitch)*settings.orbitDistance+1.,std::cos(settings.orbitYaw)*horizontal);
        }
        cameraEye.y=std::max(cameraEye.y,race.course.height(cameraEye.x,cameraEye.z)+1.2);
        auto view=glm::lookAtLH(cameraEye,car.position+V(0,.45,0),V(0,1,0));
        camera.eye=cameraEye;camera.orientation=glm::quat_cast(glm::transpose(glm::dmat3(view)));camera.verticalFov=glm::radians(66.);
    }
    if(vista) {
        auto cameraEye=race.course.point(12,-30)+V(0,3.5,0);
        auto target=race.course.point(38,-30);
        auto vistaView=glm::lookAtLH(cameraEye,target,V(0,1,0));
        camera.eye=cameraEye;camera.orientation=glm::quat_cast(glm::transpose(glm::dmat3(vistaView)));camera.verticalFov=glm::radians(72.);
    }
    eye=camera.eye;rotation=camera.orientation;
    view=glm::lookAtLH(V(0),rotation*V(0,0,1),rotation*V(0,1,0));projection=glm::perspectiveLH_ZO(camera.verticalFov,camera.aspect,camera.near,camera.far);
    auto ortho=glm::orthoRH_ZO(0.f,1600.f,900.f,0.f,-1.f,1.f);
    for(int i=0;i<4;++i) {bgfx::setViewRect(i,0,0,width,height);bgfx::setViewTransform(i,nullptr,glm::value_ptr(ortho));bgfx::setViewClear(i,i==0?BGFX_CLEAR_COLOR:BGFX_CLEAR_NONE,0x141416ff);}
    auto hudOrtho=glm::orthoRH_ZO(0.f,float(canvasWidth()),float(canvasHeight()),0.f,-1.f,1.f);
    bgfx::setViewTransform(3,nullptr,glm::value_ptr(hudOrtho));
    std::array<infinity::fractal::Vehicle,4> vehicles={armedVehicle(car),armedVehicle(race.rivals[0]),armedVehicle(race.rivals[1]),armedVehicle(race.rivals[2])};
    ell::Camera gunner{gunCamera(car),body*glm::angleAxis(car.turret.yaw,V(0,1,0))*glm::angleAxis(-car.turret.pitch,V(1,0,0)),glm::radians(68.),double(width)/(height*2),.06,550};
    std::array<glm::vec4,8> shots{};const std::array<const Car*,4> crew={&car,&race.rivals[0],&race.rivals[1],&race.rivals[2]};
    for(int i=0;i<4;++i){const auto& gun=crew[i]->turret;
        if(gun.cable.active()){auto cable=cableView(race,i);shots[i*2]=glm::vec4(cable.start,2);shots[i*2+1]=glm::vec4(cable.end,cable.sag);}
        else {shots[i*2]=glm::vec4(gun.shotStart,gun.shotLife);shots[i*2+1]=glm::vec4(gun.shotEnd,-int(gun.lastShot));}}
    std::vector<glm::vec4> pickups;for(auto& pickup:race.pickupViews)if(glm::length(pickup.position-car.position)<190)pickups.emplace_back(pickup.position,int(pickup.weapon));
    bool native=gpu->available()&&settings.gpuEnabled;
    int logicalWidth,logicalHeight;SDL_GetWindowSize(window,&logicalWidth,&logicalHeight);
    double pixelDensity=settings.retinaTerrain?1.:double(width)/std::max(1,std::min(split?640:512,logicalWidth));
    auto tacticalMap=makeGunnerMap(car,double(width)/height);
    bool tactical=split&&settings.tacticalGunner;
    gpu->draw(camera,race.course.field(),vehicles,width,height,time,native,settings.highResolution,int(settings.fractalDebug),externalView?-1:0,pixelDensity,settings.frameWorkSeconds,split?&gunner:nullptr,shots,pickups,tactical?&tacticalMap:nullptr);
    if(!native)presentSoftware();
    Mesh overlay;
    bool showMap=tactical&&!help&&race.state!=State::Menu&&race.state!=State::Paused&&!(race.state==State::Finished&&race.resultsVisible);
    if(showMap){Mesh mapObjects;gunnerMapOverlay(mapObjects,settings,race,time);submit(mapObjects,3,BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|uiBlend);}
    if(!settings.exhibit||help)hud(race,best,muted,help,time,!externalView,settings.recordSaveFailed,split,tactical);
    const double W=canvasWidth(),H=canvasHeight();
    if(showMap)gunnerMapCursor(overlay,settings,race);
    if(!help&&!settings.exhibit&&(race.state==State::Racing||race.state==State::Countdown))for(const auto& pickup:race.pickupViews){
        if(glm::length(pickup.position-car.position)>50)continue;
        for(int pane=0;pane<(tactical?1:2);++pane){const auto& c=pane?gunner:camera;auto q=glm::conjugate(c.orientation)*(pickup.position+V(0,1.0,0)-c.eye);if(q.z<.5)continue;
            double x=q.x/(q.z*std::tan(c.verticalFov*.5)*c.aspect),y=q.y/(q.z*std::tan(c.verticalFov*.5));
            if(std::abs(x)<.90&&y>-.35&&y<.56)centered(overlay,weaponName(pickup.weapon),(pane+(x*.5+.5))*W*.5,(.5-y*.5)*H,16,white);
        }
    }

    if(settings.exhibit&&!help) {
        card(overlay,28,28,510,88,.86f);label(overlay,vista?"Terrain study":"Vehicle study",48,40,30,yellow);
        label(overlay,vista?"Enter returns to your race":"Drag to orbit / mouse wheel to zoom",48,84,19,white);
        button(overlay,W-262,28,234,50,"Back to race",UiAction::Continue,"Enter");
        card(overlay,28,H-74,W-56,46,.82f);
        label(overlay,"F2 Vehicle   F7 Terrain   F3 Renderer   F4 Orbit   F5 Quality   F6 Diagnostics   F8 CRT",48,H-61,19,white);
    }
    if(!settings.exhibit&&race.state==State::Racing&&car.impactFlash>.015){
        double flash=car.impactFlash;C warning=red;
        rect(overlay,0,0,W,9,warning,float(flash*.65));rect(overlay,0,H-9,W,9,warning,float(flash*.65));
        rect(overlay,0,9,9,H-18,warning,float(flash*.65));rect(overlay,W-9,9,9,H-18,warning,float(flash*.65));
    }
    if(!help&&int(settings.fractalDebug)>0){card(overlay,W*.5-230,H-122,460,38,.86f);centered(overlay,int(settings.fractalDebug)==1?"Surface normals":"Ray-march iterations",W*.5,H-115,20,cyan);}
    submit(overlay,3,BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|uiBlend);
}
