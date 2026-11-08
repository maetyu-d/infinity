#include "FramePacer.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#if defined(__APPLE__)
// CoreVideo also supports the game's macOS 14 deployment target.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <CoreVideo/CoreVideo.h>
#include <CoreGraphics/CoreGraphics.h>
#endif
namespace desert {
struct FramePacer::Impl {
    SDL_Window* window;SDL_DisplayID display=0;
    std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now();
#if defined(__APPLE__)
    CVDisplayLinkRef link=nullptr;
    std::mutex mutex;std::condition_variable wake;
    uint64_t sequence=0,consumed=0;
    double frequency=CVGetHostClockFrequency(),period=1./60.,next=0;
    static CVReturn tick(CVDisplayLinkRef,const CVTimeStamp*,const CVTimeStamp* time,CVOptionFlags,CVOptionFlags*,void* context){
        auto& self=*static_cast<Impl*>(context);double now=double(time->hostTime)/self.frequency;
        std::lock_guard lock(self.mutex);
        if(now+self.period*.03>=self.next){
            self.next=self.next==0||now-self.next>self.period*2?now+self.period:self.next+self.period;
            ++self.sequence;self.wake.notify_one();
        }
        return kCVReturnSuccess;
    }
    void selectDisplay(){
        auto current=SDL_GetDisplayForWindow(window);if(display==current)return;display=current;
        CGDirectDisplayID native=CGMainDisplayID();SDL_Rect bounds{};uint32_t count=0;
        if(SDL_GetDisplayBounds(current,&bounds)){
            CGDirectDisplayID found=0;
            if(CGGetDisplaysWithPoint(CGPointMake(bounds.x+bounds.w*.5,bounds.y+bounds.h*.5),1,&found,&count)==kCGErrorSuccess&&count)native=found;
        }
        if(!link){if(CVDisplayLinkCreateWithCGDisplay(native,&link)!=kCVReturnSuccess){link=nullptr;return;}}
        else CVDisplayLinkSetCurrentCGDisplay(link,native);
        auto nominal=CVDisplayLinkGetNominalOutputVideoRefreshPeriod(link);
        double hz=nominal.timeValue>0&&nominal.timeScale>0?double(nominal.timeScale)/nominal.timeValue:60.;
        double divisor=std::max(1.,std::round(hz/60.)),divided=hz/divisor;
        std::lock_guard lock(mutex);period=divided>=58&&divided<=61?1./divided:1./60.;next=0;
        std::fprintf(stderr,"FRAME_CLOCK display_hz=%.3f target_hz=%.3f\n",hz,1./period);
    }
#endif
    explicit Impl(SDL_Window* w):window(w){
#if defined(__APPLE__)
        selectDisplay();
        if(link){
            CVDisplayLinkSetOutputCallback(link,tick,this);
            if(CVDisplayLinkStart(link)!=kCVReturnSuccess){CVDisplayLinkRelease(link);link=nullptr;}
        }
#endif
    }
    ~Impl(){
#if defined(__APPLE__)
        if(link){CVDisplayLinkStop(link);CVDisplayLinkRelease(link);}
#endif
    }
    void wait(){
#if defined(__APPLE__)
        selectDisplay();
        if(link){std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(100),[&]{return sequence>consumed;});consumed=sequence;return;}
#endif
        const auto interval=std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1./60.));
        deadline+=interval;auto now=std::chrono::steady_clock::now();
        if(now<deadline){constexpr auto margin=std::chrono::microseconds(1500);
            if(deadline-now>margin)std::this_thread::sleep_until(deadline-margin);
            while(std::chrono::steady_clock::now()<deadline)SDL_CPUPauseInstruction();
        }else if(now-deadline>interval)deadline=now;
    }
};
FramePacer::FramePacer(SDL_Window* window):p(std::make_unique<Impl>(window)){}
FramePacer::~FramePacer()=default;
void FramePacer::wait(){p->wait();}
}
#if defined(__APPLE__)
#pragma clang diagnostic pop
#endif
