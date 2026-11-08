// Both player views fill half the display; map strokes stay in the right half.
static void mapLine(Mesh& m,glm::dvec2 lo,glm::dvec2 hi,glm::dvec2 a,glm::dvec2 b,double width,C color,float alpha=1.f){
    auto d=b-a;double first=0,last=1;
    auto edge=[&](double p,double q){if(std::abs(p)<1e-10)return q>=0;double t=q/p;if(p<0)first=std::max(first,t);else last=std::min(last,t);return first<=last;};
    if(!edge(-d.x,a.x-lo.x)||!edge(d.x,hi.x-a.x)||!edge(-d.y,a.y-lo.y)||!edge(d.y,hi.y-a.y))return;
    b=a+d*last;a+=d*first;if(glm::length(b-a)>.01)line(m,a,b,width,color,alpha);
}
static C gunnerAimColor(const GunnerAim& aim){return aim.canFire()?white:aim.status==AimStatus::Slewing||aim.status==AimStatus::Reloading?C(.65):yellow;}
// Close-range targeting: cars, cursor and the active weapon on black.
void gunnerMapOverlay(Mesh& m,const Renderer& settings,const Race& race,double){
    using P=glm::dvec2;const double W=canvasWidth(),H=canvasHeight();
    auto map=makeGunnerMap(race.player,W/H);P lo(map.rect.x*W,map.rect.y*H),span(map.rect.z*W,map.rect.w*H),hi=lo+span;
    auto pixel=[&](P p){return lo+map.project(p)*span;};
    auto stroke=[&](P a,P b,double width,C color,float alpha=1.f){mapLine(m,lo,hi,a,b,width,color,alpha);};
    auto beam=[&](P a,P b,double width,C c,float alpha=1.f){stroke(pixel(a),pixel(b),width,c,alpha);};
    auto dashed=[&](P a,P b,double width,C c){auto x=pixel(a),y=pixel(b);double length=glm::length(y-x);if(length<.01)return;for(double t=0;t<length;t+=10)stroke(x+(y-x)*(t/length),x+(y-x)*(std::min(length,t+4)/length),width,c);};
    const auto& aim=settings.gunnerAim;const auto& gun=race.player.turret;
    C status=gunnerAimColor(aim);
    const char* message="DISABLED";
    switch(aim.status){
        case AimStatus::Ready:message="READY";break;case AimStatus::Slewing:message="AIMING";break;
        case AimStatus::Blocked:message="BLOCKED";break;case AimStatus::OutOfRange:message="OUT OF RANGE";break;
        case AimStatus::TooClose:message="TOO CLOSE";break;case AimStatus::Elevation:message="OUT OF REACH";break;
        case AimStatus::NoRoom:message="NO ROOM";break;case AimStatus::Reloading:message="RELOADING";break;default:break;
    }
    label(m,gun.cable.active()?"TOW ATTACHED":message,W*.5+30,82,21,status);
    const std::array<const Car*,4> cars={&race.player,&race.rivals[0],&race.rivals[1],&race.rivals[2]};
    auto endpoint=[&](V p,int car){return car>=0?mapCarPoint(*cars[car],p):P(p.x,p.z);};
    // Hide other battles; keep every cable that actually acts on this buggy.
    for(int i=0;i<4;++i){const auto& cable=cars[i]->turret.cable;if(!cable.active()||(i!=0&&cable.target!=0))continue;
        auto view=cableView(race,i);auto a=endpoint(view.start,i),b=endpoint(view.end,cable.target);double load=std::clamp(cable.force/9000.,0.,1.);
        if(cable.force<30)dashed(a,b,1.4,C(.65));else beam(a,b,1.6+load*1.4,white);
    }
    // Reuse the physical placement helpers, with no decorative inner contours.
    if(gun.weapon!=Weapon::TowHarpoon){P origin(aim.origin.x,aim.origin.z),target(aim.target.point.x,aim.target.point.z);
        if(aim.status==AimStatus::Ready||aim.status==AimStatus::Reloading)target={aim.actual.point.x,aim.actual.point.z};
        P forward(target-origin);forward=glm::length(forward)<.01?mapForward(race.player):glm::normalize(forward);
        auto preview=[&](P a,P b){beam(a,b,1.8,aim.canFire()?white:C(.65));};
        if(gun.weapon==Weapon::RutStitcher){for(int strand=-1;strand<=1;++strand)for(int i=0;i<24;++i)if(aim.canFire()||i%2==0)preview(rutPathPoint(target,forward,strand,i/24.),rutPathPoint(target,forward,strand,(i+1)/24.));}
        else for(int i=0;i<64;++i)if(aim.canFire()||(i/3)%2==0)preview(jackFootprintPoint(target,forward,i*2*pi/64),jackFootprintPoint(target,forward,(i+1)*2*pi/64));
    }
    for(int i=0;i<4;++i){const auto& car=*cars[i];P p(car.position.x,car.position.z),f=mapForward(car),r(f.y,-f.x);C c=i==0?white:settings.gunnerVisible[i-1]?C(.85):C(.35);
        // These bounds still match the existing enlarged mouse hit area.
        // Open outlines keep the actual weapon footprint and attachment visible.
        if(i==0){beam(p-f*3.5-r*2.,p+f*3.5,2.4,c);beam(p+f*3.5,p-f*3.5+r*2.,2.4,c);beam(p-f*3.5+r*2.,p-f*2.,2.4,c);beam(p-f*2.,p-f*3.5-r*2.,2.4,c);}
        else {beam(p-r*2.-f*3.5,p+r*2.-f*3.5,1.8,c);beam(p+r*2.-f*3.5,p+r*2.+f*3.5,1.8,c);beam(p+r*2.+f*3.5,p-r*2.+f*3.5,2.6,c);beam(p-r*2.+f*3.5,p-r*2.-f*3.5,1.8,c);}
    }
}
// Keep the cursor visible above the instrument panels while map objects sit below.
void gunnerMapCursor(Mesh& m,const Renderer& settings,const Race& race){
    using P=glm::dvec2;const double W=canvasWidth(),H=canvasHeight();auto map=makeGunnerMap(race.player,W/H);
    P lo(map.rect.x*W,map.rect.y*H),span(map.rect.z*W,map.rect.w*H),hi=lo+span;
    auto pixel=[&](P p){return lo+map.project(p)*span;};
    auto stroke=[&](P a,P b,double width,C c){mapLine(m,lo,hi,a,b,width,c);};
    auto q=lo+settings.gunnerCursor*span;C status=gunnerAimColor(settings.gunnerAim);
    for(int x:{-1,1})for(int y:{-1,1}){stroke(q+P(x*11,y*5),q+P(x*11,y*11),1.8,status);stroke(q+P(x*11,y*11),q+P(x*5,y*11),1.8,status);}
    const auto& gun=race.player.turret;
    if(gun.hitMarker>0){const std::array<const Car*,4> cars={&race.player,&race.rivals[0],&race.rivals[1],&race.rivals[2]};
        P point=gun.lastTarget>=0?mapCarPoint(*cars[gun.lastTarget],gun.shotEnd):P(gun.shotEnd.x,gun.shotEnd.z);auto hit=pixel(point);
        for(int x:{-1,1})for(int y:{-1,1})stroke(hit+P(x*7,y*7),hit+P(x*10,y*10),2,white);
    }
}
