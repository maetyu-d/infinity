#include "Audio.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <numbers>
#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#endif
namespace desert {
struct Audio::Impl {
    std::atomic<float> speed{0},load{0},boost{0},volume{0},countdown{0},rpm{1100},slip{0},thump{0},gunshot{0},winch{0};
    uint64_t impactSerial=0;std::array<uint64_t,4> shots{};
#ifdef __APPLE__
    AudioQueueRef queue=nullptr;
    double phase=0,wind=0,exhaust=0,t=0,crash=0,gun=0;
    uint32_t rng=174713;
    static void callback(void* user,AudioQueueRef q,AudioQueueBufferRef buffer) {
        auto& p=*static_cast<Impl*>(user);
        float* out=static_cast<float*>(buffer->mAudioData);
        int frames=int(buffer->mAudioDataBytesCapacity/(2*sizeof(float)));
        float s=p.speed.load(), throttle=p.load.load(),boost=p.boost.load(),volume=p.volume.load(),count=p.countdown.load();
        double hz=std::max(12.f,p.rpm.load()/60.f*1.5f);
        p.crash=std::max(p.crash,double(p.thump.exchange(0)));float slip=p.slip.load(),winch=p.winch.load();p.gun=std::max(p.gun,double(p.gunshot.exchange(0)));
        for(int i=0;i<frames;++i) {
            p.rng^=p.rng<<13;p.rng^=p.rng>>17;p.rng^=p.rng<<5;
            double noise=(double(p.rng)/4294967295.-.5)*2;
            p.phase+=hz/48000.;p.phase-=std::floor(p.phase);p.t+=1./48000.;
            double pulse=std::sin(p.phase*2*std::numbers::pi)+.45*std::sin(p.phase*4*std::numbers::pi)+.19*std::sin(p.phase*10*std::numbers::pi);
            p.exhaust+=(pulse*.65+noise*.3-p.exhaust)*.11;
            p.wind+=(noise-p.wind)*.025;
            p.crash*=.99955;p.gun*=.9986;
            double rumble=p.exhaust*(p.rpm.load()>0?.055+throttle*.045:0.)+p.wind*(.04+std::abs(s)*.0025)+noise*boost*.025+noise*slip*.055+noise*p.crash*.30+(noise*.30+std::sin(p.t*600)*.20)*p.gun+(std::sin(p.t*1700)*.018+noise*.009)*winch;
            if(count>0 && std::fmod(count,1.)<.13) rumble+=std::sin(p.t*(count<1?880.:520.)*2*std::numbers::pi)*.08;
            float value=float(std::tanh(rumble)*volume);
            out[i*2]=value;out[i*2+1]=value;
        }
        buffer->mAudioDataByteSize=frames*2*sizeof(float);AudioQueueEnqueueBuffer(q,buffer,0,nullptr);
    }
#endif
};
Audio::Audio() : p(std::make_unique<Impl>()) {
#ifdef __APPLE__
    AudioStreamBasicDescription format{};format.mSampleRate=48000;format.mFormatID=kAudioFormatLinearPCM;
    format.mFormatFlags=kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked;format.mBytesPerPacket=8;
    format.mFramesPerPacket=1;format.mBytesPerFrame=8;format.mChannelsPerFrame=2;format.mBitsPerChannel=32;
    if(AudioQueueNewOutput(&format,&Impl::callback,p.get(),nullptr,nullptr,0,&p->queue)==noErr) {
        for(int i=0;i<3;++i) {AudioQueueBufferRef buffer;AudioQueueAllocateBuffer(p->queue,2048*8,&buffer);Impl::callback(p.get(),p->queue,buffer);}
        AudioQueueStart(p->queue,nullptr);
    }
#endif
}
Audio::~Audio() {
#ifdef __APPLE__
    if(p->queue) {AudioQueueStop(p->queue,true);AudioQueueDispose(p->queue,true);}
#endif
}
void Audio::update(const Race& race,bool mute) {
    const std::array<const Car*,4> cars={&race.player,&race.rivals[0],&race.rivals[1],&race.rivals[2]};
    for(int i=0;i<4;++i)if(p->shots[i]!=cars[i]->turret.shots){if(cars[i]->turret.shots>p->shots[i]){double distance=glm::length(cars[i]->position-race.player.position);p->gunshot=std::max(p->gunshot.load(),float(1./(1.+distance*.06)));}p->shots[i]=cars[i]->turret.shots;}
    p->winch=race.player.turret.cable.active()?float(std::clamp(race.player.turret.cable.force/7000.,0.,1.)):0;
    p->rpm=float(race.player.rpm);p->slip=float(race.player.slip);
    if(p->impactSerial!=race.player.impactSerial) {p->impactSerial=race.player.impactSerial;p->thump=float(race.player.impactFlash);}
    p->speed=float(race.player.speed);p->load=float(std::clamp(race.player.speed/35.,0.,1.));p->boost=race.player.boosting?1.f:0.f;
    p->volume=mute || race.state==State::Paused || race.state==State::Menu?0.f:.65f;
    p->countdown=race.state==State::Countdown?float(race.countdown):0.f;
}
}
