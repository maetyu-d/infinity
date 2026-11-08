// Shared C++ / Metal vector coverage. All positions are hits on the real field.
inline float2 v_display_uv(float2 uv) {
    float2 q=(uv-float2(.5f))/.465f;
    return float2(.5f)+q*((1.0f+.045f*dot(q,q))/1.09f)*.5f;
}
inline float v_display_distance(float2 uv) {
    float2 q=abs((uv-float2(.5f))/.465f)-float2(.90f);
    return length(max(q,float2(0)))+min(max(q.x,q.y),0.0f)-.10f;
}
inline float v_stroke(float distance,float footprint) {
    float pixel=max(.0003f,footprint);
    return 1.0f-smoothstep(pixel*.22f,pixel*.95f,abs(distance));
}
inline float v_periodic(float position,float spacing,float footprint) {
    float distance=abs(position/spacing-floor(position/spacing+.5f))*spacing;
    float resolve=1.0f-smoothstep(spacing*.12f,spacing*.42f,footprint);
    return v_stroke(distance,footprint)*resolve;
}
inline float3 v_footprint(float3 normal,float3 ray,float3 right,float3 up,float pixel) {
    float facing=dot(normal,ray);
    facing=facing<0?min(facing,-.025f):max(facing,.025f);
    return (abs(right-ray*(dot(right,normal)/facing))+abs(up-ray*(dot(up,normal)/facing)))*pixel;
}
inline float v_terrain(float3 p,float3 footprint,float rutDepth,float compaction) {
    float fine=max(v_periodic(p.x,2.0f,footprint.x),v_periodic(p.z,2.0f,footprint.z))*.36f;
    float middle=max(v_periodic(p.x,8.0f,footprint.x),v_periodic(p.z,8.0f,footprint.z))*.64f;
    float large=max(v_periodic(p.x,32.0f,footprint.x),v_periodic(p.z,32.0f,footprint.z))*.48f;
    // Height sections reveal small ridges which an X/Z net alone could miss.
    float contours=max(v_periodic(p.y,.20f,footprint.y)*.26f,v_periodic(p.y,1.0f,footprint.y)*.70f);
    float ruts=compaction>0?v_periodic(rutDepth,.025f,max(.0003f,footprint.y))*.72f*min(1.0f,compaction*5.0f):0;
    return clamp(max(max(fine,middle),max(large,contours))+ruts,0.0f,1.0f);
}
inline float3 v_direction(float3 p,Vehicle car) {
    return fr_rotate_z(fr_rotate_x(fr_rotate_y(p,-car.placement.w),-car.attitude.x),-car.attitude.y);
}
inline float v_vehicle(float3 p,float3 normal,float3 ray,Vehicle car,int material,float pixel,float3 right,float3 up) {
    float3 local=fr_local(p,car);
    if(material>=7&&material<=9){int part=material-7;
        float3 q=fr_turret_turn(local-fr_turret_center(car,part),car,part,true),b=fr_turret_bounds(part);
        float3 n=fr_turret_turn(v_direction(normal,car),car,part,true),r=fr_turret_turn(v_direction(ray,car),car,part,true);
        float3 rt=fr_turret_turn(v_direction(right,car),car,part,true),u=fr_turret_turn(v_direction(up,car),car,part,true);
        if(part==1){float3 gap=max(b-abs(q),float3(0))/max(v_footprint(n,r,rt,u,pixel),float3(.0003f));
            return v_stroke(min(max(gap.x,gap.y),min(max(gap.x,gap.z),max(gap.y,gap.z))),.85f);}
        float axial=part==0?q.y:q.z,radial=part==0?length(float2(q.x,q.z)):length(float2(q.x,q.y)),halfLength=part==0?b.y:b.z;
        bool cap=abs(abs(axial)-halfLength)<pixel*1.5f+.001f;
        float rim=cap?v_stroke(b.x-radial,pixel*.85f):v_stroke(abs(axial)-halfLength,pixel*.85f);
        float muzzle=part==2&&cap&&axial>0?v_stroke(radial-b.x*.50f,pixel*.65f)*.7f:0;
        return max(max(rim,muzzle),(1.0f-smoothstep(.02f,.18f,abs(dot(normal,ray))))*.8f);
    }
    if(material==5||(material>=20&&material<24)) {
        float3 footprint=v_footprint(v_direction(normal,car),v_direction(ray,car),v_direction(right,car),v_direction(up,car),pixel);
        float3 gap=max(fr_block_bounds(car)-abs(local-fr_block_center(car)),float3(0))/max(footprint,float3(.0003f));
        float edge=min(max(gap.x,gap.y),min(max(gap.x,gap.z),max(gap.y,gap.z)));
        return v_stroke(edge,.85f);
    }
    float best=1e8f;int index=0;float3 q(0);
    if(material>=50&&material<54){index=material-50;q=fr_rotate_y(local-fr_wheel_center(car,index),index<2?-car.steering[index]:0.0f);}
    else for(int i=0;i<4;++i) {
        float3 candidate=fr_rotate_y(local-fr_wheel_center(car,i),i<2?-car.steering[i]:0.0f);
        float distance=abs(fr_cylinder_x(candidate,fr_wheel_radius(car,i),.20f));
        if(distance<best){best=distance;index=i;q=candidate;}
    }
    float radius=fr_wheel_radius(car,index),radial=length(float2(q.y,q.z));
    float ring=v_stroke(radius-radial,pixel*.85f);
    bool cap=abs(abs(q.x)-.20f)<pixel*1.5f+.001f;
    float hub=cap?v_stroke(radial-radius*.35f,pixel*.65f)*.65f:0;
    float spokes=cap?v_stroke(sin((atan2(q.y,q.z)+car.rotation[index])*3.0f)*radial,pixel*.6f)*.45f:0;
    float silhouette=1.0f-smoothstep(.02f,.18f,abs(dot(normal,ray)));
    float rim=cap?ring:v_stroke(abs(q.x)-.20f,pixel*.85f);
    return max(max(rim,hub),max(spokes,silhouette*.80f));
}
