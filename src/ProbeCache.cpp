#include "infinity/World.hpp"
namespace infinity {
std::shared_ptr<const Geometry> RegionCache::requestBore(const Node& planet, const Borehole& hole,
                                                       bool chamber) {
    return requestBore(planet,hole,chamber,0);
}
std::shared_ptr<const Geometry> RegionCache::requestBore(const Node& planet, const Borehole& hole,
                                                       bool chamber,double minimumDepth) {
    const auto depth=unsigned(std::llround(hole.finalDepth()*1000000));
    const auto prefix="bore-"+std::to_string(hole.id)+"-g"+std::to_string(hole.geologyVersion)+(chamber?"-room-":"-shaft-");
    return schedule(planet,prefix,int(depth),
        [this,hole,chamber](const std::atomic<bool>* cancel){return boreGeometry(universe_,hole,chamber,cancel);},
        [chamber,depth,minimumDepth](const Geometry& g){
            // A longer ready shaft can be clipped to the actual drilled tip.
            // A chamber at another depth cannot be substituted safely.
            if(chamber)return g.detail==depth?0.:1e9;
            return g.detail*1e-6+1e-6>=minimumDepth?std::abs(double(g.detail)-depth):1e9;
        },0);
}
std::shared_ptr<const Geometry> RegionCache::requestExcavatedTerrain(std::shared_ptr<const Geometry> base,
                                                                  std::vector<Borehole> holes) {
    uint64_t hash=0;
    for (const auto& b:holes) hash=mix64(hash^b.id^uint64_t(b.shaftRadius*100));
    const auto planet=universe_.describe(base->address);
    const auto key="exc-terrain-"+std::to_string(base->terrainTile?encodeTile(*base->terrainTile):0)+"-"+
                   std::to_string(base->terrainStitch)+"-"+std::to_string(hash);
    return schedule(planet,key,0,[base,holes=std::move(holes)](const std::atomic<bool>* cancel){
        return excavatedTerrain(*base,holes,true,cancel);
    },[](const Geometry&){return 1.;},0);
}
} // namespace infinity
