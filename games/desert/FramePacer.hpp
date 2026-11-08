#pragma once
#include <memory>
struct SDL_Window;
namespace desert {
class FramePacer {
public:
    explicit FramePacer(SDL_Window*);
    ~FramePacer();
    void wait();
    FramePacer(const FramePacer&)=delete;
    FramePacer& operator=(const FramePacer&)=delete;
private:
    struct Impl;std::unique_ptr<Impl> p;
};
}
