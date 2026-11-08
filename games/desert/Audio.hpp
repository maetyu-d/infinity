#pragma once
#include "Desert.hpp"
#include <memory>
namespace desert {
class Audio {
 public:
    Audio();
    ~Audio();
    void update(const Race&,bool mute);
 private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
}
