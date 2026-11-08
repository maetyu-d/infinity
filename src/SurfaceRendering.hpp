// Included inside Renderer::Impl. This adapter consumes immutable society and
// climate descriptors; it cannot alter the universe or advance its simulation.
void renderWeather(const Node& planet, glm::dvec3 localEye, const Navigation& nav) {
    if (!hasSolidSurface(planet) || nav.anchor.kind() >= Kind::Rock ||
        (instruments && instruments->shrinking))
        return;
    const auto climate = planetClimate(planet);
    const double seconds = activity ? activity->seconds : visualTime;
    const double radius = std::pow(10., planet.frame.logRadius);
    const double altitude = glm::length(localEye) - radius;
    if (glm::length(localEye) < 1 || glm::length(localEye) > radius * 8)
        return;
    const WeatherSampler sampler(climate, seconds);
    const auto weather = sampler.sample(localEye, altitude);
    if (altitude < weatherPriority) {
        weatherPriority = altitude;
        char report[120], detail[120];
        std::snprintf(report, sizeof(report), "%s  %.0fK  %.2fBAR",
                      std::string(weatherName(weather.type)).c_str(), weather.temperatureK,
                      weather.pressureBar);
        std::snprintf(detail, sizeof(detail), "WIND %.0f  GUST %.0fM/S  VIS %.1fKM",
                      glm::length(weather.wind), weather.gustMetresPerSecond,
                      weather.visibilityMetres / 1000.);
        weatherReport = report;
        weatherDetail = climate.pressureBar == 0     ? "NO ATMOSPHERE"
                        : weather.pressureBar < 1e-5 ? "ABOVE WEATHER / LOCAL VACUUM"
                                                     : detail;
        weatherSeverity = std::max({weather.storm, 1 - weather.visibilityMetres / 5000.,
                                    smoothstep(20., 50., weather.gustMetresPerSecond)});
        hyperion->localWeather(climate, weather, seconds, weatherEffects);
    }
    if (climate.pressureBar == 0)
        return;
    const auto pose = poseFor(planet.address, nav);
    const double units = std::pow(10., nav.logDistance);
    const auto rotationView = glm::mat4_cast(glm::quat(cameraInverse));
    const glm::mat4 proj(projection);
    bgfx::setViewTransform(63, glm::value_ptr(rotationView), glm::value_ptr(proj));
    bgfx::setViewRect(63, 0, 0, uint16_t(width), uint16_t(height));
    bgfx::setViewMode(63, bgfx::ViewMode::Sequential);
    Mesh clouds;
    // Latitude/longitude cells are evaluated in one continuous spherical field.
    // The poles share positions; camera proximity never changes a cell's seed.
    constexpr unsigned rings = 16, sectors = 32;
    const auto layers = weatherLayers(climate);
    const double shellRadius = radius + layers.cloudBaseMetres;
    const auto direction = [](double latitude, double longitude) {
        return glm::dvec3(std::cos(latitude) * std::cos(longitude), std::sin(latitude),
                          std::cos(latitude) * std::sin(longitude));
    };
    const float legacyWeight = cloudVolumes ? 1.f - hyperion->weatherBlend(planet.address) : 0.f;
    if (hyperion->weatherBlend(planet.address) > 0)
        ++stats.weatherVolumes;
    for (unsigned ring = 0; ring < rings; ring++)
        for (unsigned sector = 0; sector < sectors; sector++) {
            const double a = -std::numbers::pi / 2 + ring * std::numbers::pi / rings;
            const double b = a + std::numbers::pi / rings;
            const double x = sector * 2 * std::numbers::pi / sectors, z = x + 2 * std::numbers::pi / sectors;
            const auto d = direction((a + b) * .5, (x + z) * .5);
            const auto centre = d * shellRadius;
            if (glm::length(localEye) > shellRadius && glm::dot(d, localEye - centre) < 0)
                continue;
            const auto viewPoint = cameraInverse * (pose.orientation * (centre - localEye)) / units;
            if (!LOD::visible(viewPoint, shellRadius * .22 / units, double(width) / height, fieldOfView))
                continue;
            ++stats.weatherCells;
            if (legacyWeight < .001f)
                continue;
            const auto first = uint32_t(clouds.vertices.size());
            for (auto q : {direction(a, x), direction(b, x), direction(b, z), direction(a, z)}) {
                const auto sample = sampler.sample(q, 0);
                const float alpha =
                    float(sample.cloud * .54 * std::min(1., climate.pressureBar * 2)) * legacyWeight;
                const auto delta = pose.orientation * (q * shellRadius - localEye) / units;
                const glm::vec3 tint =
                    glm::mix(glm::vec3(.32, .52, .78), glm::vec3(.83, .67, .88), float(sample.storm));
                clouds.vertices.push_back(
                    {float(delta.x), float(delta.y), float(delta.z), rgba(tint, alpha)});
            }
            clouds.indices.insert(clouds.indices.end(),
                                  {first, first + 1, first + 2, first, first + 2, first + 3});
        }
    if (!clouds.vertices.empty()) {
        const auto nv = uint32_t(clouds.vertices.size()), ni = uint32_t(clouds.indexCount());
        if (bgfx::getAvailTransientVertexBuffer(nv, layout) >= nv &&
            bgfx::getAvailTransientIndexBuffer(ni, true) >= ni && bgfx::getAvailInstanceDataBuffer(1, 80)) {
            bgfx::TransientVertexBuffer vertices;
            bgfx::TransientIndexBuffer indices;
            bgfx::InstanceDataBuffer buffer;
            bgfx::allocTransientVertexBuffer(&vertices, nv, layout);
            bgfx::allocTransientIndexBuffer(&indices, ni, true);
            bgfx::allocInstanceDataBuffer(&buffer, 1, 80);
            std::memcpy(vertices.data, clouds.vertices.data(), nv * sizeof(Vertex));
            std::memcpy(indices.data, clouds.indexData(), ni * 4);
            const Instance identity{glm::mat4(1), glm::vec4(1)};
            std::memcpy(buffer.data, &identity, sizeof(identity));
            glm::dmat4 pattern(glm::mat3_cast(glm::inverse(pose.orientation)) * (units / shellRadius));
            pattern[3] = glm::dvec4(localEye / shellRadius, 1);
            Geometry source;
            const ScenePresentation presentation{
                source, planet.address, {}, hyperionPhase(climate.seed), 1.f};
            hyperion->bind(presentation, {}, glm::vec3(lightAt(planet.address, nav)), glm::mat4(pattern), 0,
                           seconds);
            bgfx::setVertexBuffer(0, &vertices);
            bgfx::setIndexBuffer(&indices);
            bgfx::setInstanceDataBuffer(&buffer);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_GEQUAL | BGFX_STATE_BLEND_ALPHA |
                           BGFX_STATE_MSAA);
            bgfx::submit(63, hyperion->cloudProgram());
        }
    }
    if (!weatherEffects || !instruments || instruments->shrinking || altitude > layers.cloudBaseMetres ||
        units < .1 || weather.precipitation < .06)
        return;
    auto& precipitation = weatherRain;
    auto& flakes = weatherFlakes;
    precipitation.vertices.clear();
    precipitation.indices.clear();
    flakes.vertices.clear();
    flakes.indices.clear();
    // Reuse a bounded optical mesh; dense precipitation does not repeatedly
    // grow temporary vertex/index vectors on the rendering thread.
    flakes.vertices.reserve(384 * 9);
    flakes.indices.reserve(384 * 24);
    const double cellSize = weather.type == WeatherType::Dust   ? 12
                            : weather.type == WeatherType::Snow ? 8
                                                                : 6;
    const auto cell = glm::floor(localEye / cellSize);
    const glm::dvec3 down = -glm::normalize(localEye);
    const auto flutterFrame = tangentFrame(-down);
    const unsigned limit = unsigned(160 + std::min(1., weather.precipitation) * 224);
    for (unsigned i = 0; i < 1331 && stats.weatherParticles < limit; i++) {
        const auto grid = cell + glm::dvec3(int(i % 11) - 5, int(i / 11 % 11) - 5, int(i / 121) - 5);
        const auto hash =
            mix64(mix64(mix64(climate.seed ^ uint64_t(int64_t(grid.x))) ^ uint64_t(int64_t(grid.y))) ^
                  uint64_t(int64_t(grid.z)));
        const Random local{hash};
        auto point = (grid + glm::dvec3(local.unit(0), local.unit(1), local.unit(2))) * cellSize;
        const double fallSpeed = weather.type == WeatherType::Snow   ? 1.4
                                 : weather.type == WeatherType::Dust ? 2
                                                                     : 18;
        const double phase = std::fmod(seconds * fallSpeed + local.unit(3) * cellSize, cellSize);
        point += down * phase + weather.wind * (phase / std::max(1., fallSpeed) * .1);
        if (weather.type == WeatherType::Snow || weather.type == WeatherType::Dust) {
            const double sway = weather.type == WeatherType::Snow ? .4 : 1.8;
            point += flutterFrame * glm::dvec3(std::sin(seconds * .8 + local.unit(5) * 6.28) * sway, 0,
                                               std::cos(seconds * .57 + local.unit(6) * 6.28) * sway);
        }
        const double distance = glm::length(point - localEye);
        const float fade = float(1 - smoothstep(cellSize * 4, cellSize * 5, distance));
        if (fade < .01f)
            continue;
        const auto delta = pose.orientation * (point - localEye) / units;
        if ((cameraInverse * delta).z >= -.00005 || glm::length(delta) > 10000 ||
            !LOD::visible(cameraInverse * delta, .2 / units, double(width) / height, fieldOfView))
            continue;
        const auto tint =
            weather.type == WeatherType::Dust ? glm::vec3(.72, .43, .25) : glm::vec3(.54, .75, .86);
        auto tail = pose.orientation *
                    (down * (weather.type == WeatherType::Snow ? .2 : 2.) + weather.wind * .04) / units;
        if (weather.type != WeatherType::Snow && weather.type != WeatherType::Dust) {
            auto motion = (pose.orientation * (down * fallSpeed + weather.wind) -
                           cameraQ * instruments->localVelocity) *
                          .065;
            motion *= std::min(1., 12. / std::max(1e-9, glm::length(motion)));
            tail = motion / units;
        }
        if (weather.type == WeatherType::Snow || weather.type == WeatherType::Dust) {
            const double physicalRadius =
                weather.type == WeatherType::Snow ? .05 + local.unit(4) * .075 : .25 + local.unit(4) * .55;
            const double pixelRadius = weather.type == WeatherType::Snow ? 3.5 : 10.;
            const double r =
                std::min(physicalRadius, glm::length(point - localEye) *
                                             (2 * std::tan(fieldOfView * .5) / height) * pixelRadius);
            const auto right = cameraQ * glm::dvec3(r, 0, 0) / units,
                       up = cameraQ * glm::dvec3(0, r, 0) / units;
            const auto color = weather.type == WeatherType::Snow ? glm::vec3(.9, .96, 1) : tint;
            const float alpha = (weather.type == WeatherType::Snow ? .85f : .22f) * fade;
            const auto first = uint32_t(flakes.vertices.size());
            flakes.vertices.push_back({float(delta.x), float(delta.y), float(delta.z), rgba(color, alpha)});
            for (unsigned vertex = 0; vertex < 8; vertex++) {
                const double angle = vertex * 2 * std::numbers::pi / 8;
                const auto p = delta + right * std::cos(angle) + up * std::sin(angle);
                flakes.vertices.push_back({float(p.x), float(p.y), float(p.z), rgba(color, 0)});
            }
            for (unsigned vertex = 0; vertex < 8; vertex++)
                flakes.indices.insert(flakes.indices.end(),
                                      {first, first + 1 + vertex, first + 1 + (vertex + 1) % 8});
        } else {
            // Motion-blurred rain ribbons stay legible on Retina displays.
            // Their endpoints remain in the same physical, wind-driven cells.
            const double pixel = distance * 2 * std::tan(fieldOfView * .5) / height;
            const double widthMetres = std::clamp(.008, pixel * .50, pixel * 1.15);
            const auto sideways = glm::cross(tail, cameraQ * glm::dvec3(0, 0, 1));
            const auto across =
                (glm::length(sideways) > 1e-15 ? glm::normalize(sideways) : cameraQ * glm::dvec3(1, 0, 0)) *
                (widthMetres / units);
            const uint32_t first = uint32_t(flakes.vertices.size());
            const auto colour =
                rgba(glm::vec3(.56, .78, .94), fade * float(.42 + weather.precipitation * .25));
            for (const auto p :
                 {delta - across, delta + across, delta + tail + across, delta + tail - across})
                flakes.vertices.push_back({float(p.x), float(p.y), float(p.z), colour});
            flakes.indices.insert(flakes.indices.end(),
                                  {first, first + 1, first + 2, first, first + 2, first + 3});
        }
        ++stats.weatherParticles;
    }
    transient(precipitation, 63, true, false, true, false, true);
    transient(flakes, 63, false, false, true, false, true);
}
void drawCityPaving(const Mesh& mesh, const Node& planet, const Navigation& nav) {
    if (mesh.vertices.empty())
        return;
    {
        const auto nv = uint32_t(mesh.vertices.size()), ni = uint32_t(mesh.indexCount());
        if (bgfx::getAvailTransientVertexBuffer(nv, layout) < nv ||
            bgfx::getAvailTransientIndexBuffer(ni, true) < ni || !bgfx::getAvailInstanceDataBuffer(1, 80))
            return;
        bgfx::TransientVertexBuffer vertices;
        bgfx::TransientIndexBuffer indices;
        bgfx::allocTransientVertexBuffer(&vertices, nv, layout);
        bgfx::allocTransientIndexBuffer(&indices, ni, true);
        std::memcpy(vertices.data, mesh.vertices.data(), nv * sizeof(Vertex));
        std::memcpy(indices.data, mesh.indexData(), ni * 4);
        bgfx::InstanceDataBuffer buffer;
        bgfx::allocInstanceDataBuffer(&buffer, 1, 80);
        const Instance identity{glm::mat4(1), glm::vec4(1)};
        std::memcpy(buffer.data, &identity, sizeof(identity));
        bgfx::setVertexBuffer(0, &vertices);
        bgfx::setIndexBuffer(&indices);
        bgfx::setInstanceDataBuffer(&buffer);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_GEQUAL |
                       BGFX_STATE_MSAA);
        Geometry source;
        const auto pose = poseFor(planet.address, nav);
        const double units = std::pow(10., nav.logDistance);
        const auto localEye = universe.pointRelative(
            nav.anchor, eye * std::pow(10., nav.logDistance - anchorFrames.back().logRadius), planet.address,
            0);
        const auto up = glm::normalize(localEye);
        const auto climate = planetClimate(planet);
        const auto light = lightAt(planet.address, nav);
        const ScenePresentation presentation{
            source,
            planet.address,
            {.92, 0, .025, .01},
            hyperionPhase(planet.seed),
            1.f,
            {4, float(SurfaceMesh::Road), float(units), 1},
            {float(glm::dot(up, glm::inverse(pose.orientation) * light)), float(climate.pressureBar), 0, 0},
            glm::vec4(glm::vec3(pose.orientation * up), float(climate.pressureBar))};
        glm::dmat4 material(glm::mat3_cast(glm::inverse(pose.orientation)) * units);
        // This period is an integer number of both 3.6 m and 3.2 m panels.
        material[3] = glm::dvec4(glm::mod(localEye, glm::dvec3(7372.8)), 1);
        hyperion->bind(presentation, {}, glm::vec3(lightAt(planet.address, nav)), glm::mat4(material), 0,
                       activity->seconds);
        bgfx::submit(61, hyperion->geometryProgram());
    }
    stats.surfaceTriangles += mesh.indexCount() / 3;
}
// Draw the same immutable urban parcels at every distance. The descriptor
// contains every home's terrain fit; only subpixel drawing is omitted.
template<class Emit,class Rover>
void renderUrban(const Node& planet,const SurfaceSettlement& city,const Pose& pose,glm::dvec3 planetEye,
                 const Navigation& nav,const SettlementWork& work,Emit&& emit,Rover&& rover,Mesh& paving,size_t usedInstances){
    if(!city.urban)return;const auto& region=*city.urban;++stats.urbanRegions;
    const double units=std::pow(10.,nav.logDistance),pixelScale=height/(2*std::tan(fieldOfView*.5));
    const auto sunlight=glm::inverse(pose.orientation)*lightAt(planet.address,nav);
    struct Visible {const UrbanBlock* block;double distance;glm::dvec3 point;};std::vector<Visible> visible;visible.reserve(region.blocks.size());
    auto roadQuad=[&](const SurfaceRoad& road){
        const auto centre=(road.a+road.b)*.5;const auto delta=pose.orientation*(centre-planetEye);
        if(road.width/std::max(.1,glm::length(delta))*pixelScale<.5||!LOD::visible(cameraInverse*delta/units,(glm::length(road.b-road.a)+road.width)/units,double(width)/height,fieldOfView))return;
        const auto first=uint32_t(paving.vertices.size());
        for(unsigned corner:{1u,2u,6u,5u}){const auto p=pose.orientation*(centre-planetEye+glm::dvec3(road.surface[corner]))/units;
            paving.vertices.push_back({float(p.x),float(p.y),float(p.z),rgba({.16,.23,.28})});}
        paving.indices.insert(paving.indices.end(),{first,first+1,first+2,first,first+2,first+3});
    };
    for(const auto& block:region.blocks){
        const glm::dvec2 local((block.x+.5)*region.cellWidth,(block.z+.5)*region.cellWidth);
        const auto point=urbanPoint(city,region,local,block.ground);const auto up=glm::normalize(point);
        const auto delta=pose.orientation*(point-planetEye);const double distance=glm::length(delta);
        if(glm::dot(up,planetEye-point)<-block.tallest||distance>region.growth.radius+400000||
            !LOD::visible(cameraInverse*delta/units,(region.cellWidth*.73+block.tallest)/units,double(width)/height,fieldOfView))continue;
        ++stats.urbanBlocks;visible.push_back({&block,distance,point});
        const double night=1-smoothstep(-.12,.12,glm::dot(up,sunlight));
        const double power=work.synchronized?std::clamp(work.power,0.,1.):1.;
        if(night*power>.025){
            const auto tint=block.use==BuildingUse::Commerce?glm::vec3(.65,.32,1):block.use==BuildingUse::Industry?glm::vec3(1,.43,.12):block.use==BuildingUse::Utilities?glm::vec3(.15,.85,.87):glm::vec3(1,.76,.42);
            // Junction lights are anchored to actual generated street samples.
            for(unsigned corner:{0u,1u}){
                const auto ground=urbanPoint(city,region,{block.x*region.cellWidth,(block.z+corner)*region.cellWidth},block.roads[0][corner*urbanRoadSteps*2]+2);
                const auto p=pose.orientation*(ground-planetEye);
                if((cameraInverse*p).z>=0)continue;
                addBillboard(eye+p/units,std::max(5.,distance*.0008)/units,tint*float(night*power*.38));++stats.urbanLights;
            }
            if(distance>12000){addBillboard(eye+delta/units,region.cellWidth*.36/units,tint*float(night*power*.035));++stats.urbanLights;}
        }
    }
    std::sort(visible.begin(),visible.end(),[](const auto& a,const auto& b){return a.distance<b.distance;});
    // Reserve a share for every visible block before refining its parcels.
    // A nearer block cannot consume the budget and erase the distant skyline.
    size_t fixed=0,weight=0;
    for(const auto& view:visible){
        if(view.distance<1500)fixed+=size_t(std::popcount(view.block->occupied))*2;
        else weight+=view.distance<2500?2:1;
    }
    const auto budget=6000>usedInstances+fixed+96?6000-usedInstances-fixed-96:0;
    const double quota=std::min(double(urbanPlotCount),double(budget)/std::max(size_t(1),weight));
    unsigned people=0,vehicles=0;
    for(const auto& view:visible){const auto& block=*view.block;
        if(block.tallest/std::max(.1,view.distance)*pixelScale>.8){
            std::array<unsigned,urbanPlotCount> ordered{};unsigned count=0;uint64_t chosen=0;
            const Random selection{urbanBlockId(region,block)};
            for(unsigned group:{4u,0u,8u,2u,6u,1u,7u,3u,5u}){
                const unsigned x=(group%3)*2,z=(group/3)*2,rotation=selection.bits(group)%4;
                for(unsigned pick=0;pick<4;++pick){const unsigned corner=(pick+rotation)%4,plot=(z+corner/2)*urbanPlotsAcross+x+corner%2;
                    if(block.occupied&(uint64_t(1)<<plot)){ordered[count++]=plot;chosen|=uint64_t(1)<<plot;break;}}
            }
            for(unsigned plot=0;plot<urbanPlotCount;++plot)if((block.occupied&~chosen)&(uint64_t(1)<<plot))ordered[count++]=plot;
            const unsigned localQuota=view.distance<1500?urbanPlotCount:unsigned(quota+selection.unit(97));
            for(unsigned sample=0;sample<std::min(localQuota,count);++sample){const auto plot=ordered[sample];
                const auto b=urbanBuilding(city,region,block,plot);
                if(emit(b.kind,b.position,b.orientation,b.dimensions,b.tint)){++stats.surfaceBuildings;++stats.urbanBuildings;}
                if(b.kind!=SurfaceMesh::Tree&&view.distance<2500)emit(SurfaceMesh::Foundation,b.position,b.orientation,{b.dimensions.x,b.foundationMetres,b.dimensions.z},b.tint);
                if(people<48&&view.distance<900&&b.inhabitants&&!(work.synchronized&&work.outdoorHold)){
                    const auto person=urbanResident(city,region,block,plot,unsigned(Random{b.id}.bits(0)%b.inhabitants),activity->seconds);
                    if(person.outside&&emit(person.suited?SurfaceMesh::Suit:SurfaceMesh::Resident,person.position,person.orientation,{1,1,1},{.68,.85,.94})){++people;++stats.surfaceResidents;++stats.urbanResidents;}
                }
            }
        }
        if(view.distance<20000){
            const double stepPixels=region.cellWidth/urbanRoadSteps/std::max(.1,view.distance)*pixelScale;
            const unsigned stride=stepPixels<3?8:stepPixels<8?4:stepPixels<16?2:1;
            for(unsigned edge=0;edge<2;++edge)for(unsigned segment=0;segment<urbanRoadSteps;segment+=stride){
                auto road=urbanRoad(city,region,block,edge,segment);
                if(stride>1){const auto last=urbanRoad(city,region,block,edge,segment+stride-1);const auto oldCentre=(road.a+road.b)*.5,newCentre=(road.a+last.b)*.5,lastCentre=(last.a+last.b)*.5;
                    for(unsigned corner=0;corner<4;++corner)road.surface[corner]+=glm::vec3(oldCentre-newCentre);
                    for(unsigned corner=4;corner<8;++corner)road.surface[corner]=last.surface[corner]+glm::vec3(lastCentre-newCentre);
                    road.b=last.b;}
                roadQuad(road);
            }
        }
        if(vehicles<12&&view.distance<6000&&work.synchronized&&!work.outdoorHold&&work.activity>.05&&work.orderCount){
            const auto& order=work.orders[urbanBlockId(region,block)%work.orderCount];const Random choice{mix64(order.id^urbanBlockId(region,block))};
            const double clock=work.animationSeconds>=0?work.animationSeconds:activity->seconds*work.activity;
            const double journey=std::fmod(clock*(4+choice.unit(0)*4)+choice.unit(1)*region.cellWidth*2,region.cellWidth*2);
            const bool returning=journey>region.cellWidth;const double progress=returning?region.cellWidth*2-journey:journey;
            const unsigned segment=std::min(urbanRoadSteps-1,unsigned(progress/region.cellWidth*urbanRoadSteps));
            const auto road=urbanRoad(city,region,block,choice.bits(2)%2,segment);const auto forward=glm::normalize(road.b-road.a),up=glm::normalize(road.a);
            const auto position=glm::mix(road.a,road.b,std::clamp(progress/region.cellWidth*urbanRoadSteps-segment,0.,1.))+up*.2+glm::normalize(glm::cross(up,forward))*(road.width*.2);
            rover(position,urbanOrientation(returning?-forward:forward,up),{.27,.74,.83});++vehicles;
        }
    }
    for(const auto& road:region.accessRoads)roadQuad(road);
}
void renderSurfaceEnvironment(const std::vector<Node>& nodes, const Navigation& nav, RegionCache& cache) {
    for (const auto& node : nodes)
        if (node.address.kind() == Kind::Planet && hasSolidSurface(node)) {
            const auto localEye = universe.pointRelative(
                nav.anchor, eye * std::pow(10., nav.logDistance - anchorFrames.back().logRadius),
                node.address, 0);
            renderWeather(node, localEye, nav);
        }
    if (!activity || !activity->enabled || nav.anchor.kind() >= Kind::Rock)
        return;
    std::array<std::array<std::vector<Instance>, 3>, size_t(SurfaceMesh::Count)> batches;
    std::array<std::vector<Instance>, size_t(SurfaceMesh::Count)> sunCasters;
    std::array<std::array<std::vector<Instance>,3>,size_t(SurfaceMachine::Count)> workBatches;
    const double units = std::pow(10., nav.logDistance);
    const glm::mat4 rotationView = glm::mat4_cast(glm::quat(cameraInverse)), proj(projection);
    bgfx::setViewTransform(61, glm::value_ptr(rotationView), glm::value_ptr(proj));
    bgfx::setViewRect(61, 0, 0, uint16_t(width), uint16_t(height));
    size_t instances = 0;
    for (const auto& system : nodes) {
        if (system.address.kind() != Kind::System)
            continue;
        const auto planets = cache.catalog(system);
        if (!planets)
            continue;
        for (const auto& planet : *planets) {
            if (!hasSolidSurface(planet))
                continue;
            const double radius = std::pow(10., planet.frame.logRadius);
            const auto planetEye = universe.pointRelative(
                nav.anchor, eye * std::pow(10., nav.logDistance - anchorFrames.back().logRadius),
                planet.address, 0);
            const bool pinned = activity->selected && activity->selected->home == planet.address &&
                                activity->selected->slot >= surfaceActivitySlot;
            if (glm::length(planetEye) > radius * 4 && !pinned)
                continue;
            const auto world = surfaceLife.request(planet);
            if (!world)
                continue;
            const auto pose = poseFor(planet.address, nav);
            bool updateSun =
                terrainSunPlanet && *terrainSunPlanet == planet.address && hyperion->sunUpdated();
            glm::dvec3 sunCentre = terrainSunAnchor;
            double sunRange = terrainSunSpan * .75;
            const SurfaceSettlement* shadowCity = nullptr;
            double nearestCity = 1e30;
            if (sunShadows && instruments && !instruments->shrinking && instruments->altitude < 5000 &&
                units > .01 && !hyperion->sunActive()) {
                for (const auto& city : world->settlements) {
                    const double range = glm::length(city.landingPoint - planetEye);
                    if (range < nearestCity && range < city.radius + 3500 &&
                        glm::dot(city.direction, planetEye - city.landingPoint) > -10) {
                        shadowCity = &city;
                        nearestCity = range;
                    }
                }
                if (shadowCity) {
                    const auto light = glm::inverse(pose.orientation) * lightAt(planet.address, nav);
                    if (glm::dot(shadowCity->direction, light) > .035) {
                        const auto shadow = sunShadowFrame(
                            shadowCity->landingPoint, light, pose.orientation, planetEye, units,
                            std::clamp(shadowCity->radius * 2.2, 512., 4096.),
                            bgfx::getCaps()->homogeneousDepth, bgfx::getCaps()->originBottomLeft);
                        sunCentre = shadowCity->landingPoint;
                        sunRange = shadowCity->radius * 1.5;
                        updateSun = hyperion->prepareSun(shadow, world->descriptor ^ shadowCity->layoutSeed);
                    }
                }
            }
            Mesh paving;
            float economicLighting = 1;
            auto instance = [&](SurfaceMesh kind, glm::dvec3 point, glm::dquat q, glm::dvec3 size,
                                glm::vec3 tint, bool essential = false) {
                if (instances >= 6000)
                    return;
                const auto delta = pose.orientation * (point - planetEye);
                const auto direction = cameraInverse * delta;
                const bool person = kind == SurfaceMesh::Resident || kind == SurfaceMesh::Suit;
                const double distance = glm::length(delta),
                             extent = std::max({size.x, size.y, size.z}) * (person ? 1.8 : 1.);
                const double pixels =
                    extent / std::max(.01, distance) * height / (2 * std::tan(fieldOfView * .5));
                const bool visible =
                    pixels >= (essential ? .35 : .9) && distance / units <= 8000 &&
                    LOD::visible(direction / units, extent / units, double(width) / height, fieldOfView);
                const bool caster = updateSun && !person && kind != SurfaceMesh::Pad &&
                                    kind != SurfaceMesh::Road && kind != SurfaceMesh::Plaza &&
                                    kind != SurfaceMesh::Footway && glm::length(point - sunCentre) < sunRange;
                if (!visible && !caster)
                    return;
                const auto model = glm::translate(glm::dmat4(1), delta / units) *
                                   glm::mat4_cast(pose.orientation * q) *
                                   glm::scale(glm::dmat4(1), size / units);
                // Shadow visibility is independent of the camera frustum:
                // off-screen buildings can still shade the visible street.
                if (caster)
                    sunCasters[size_t(kind)].push_back({glm::mat4(model), glm::vec4(tint, 1)});
                if (!visible)
                    return;
                const unsigned detail = pixels > 60 ? 2 : pixels > 10 ? 1 : 0;
                batches[size_t(kind)][detail].push_back({glm::mat4(model), glm::vec4(tint, economicLighting)});
                ++instances;
            };
            auto animateWork=[&](const ActivityId& id,SettlementWork& work){
                if(work.synchronized){
                    if(workClocks.size()>=128 && !workClocks.contains(id)){
                        const auto old=std::min_element(workClocks.begin(),workClocks.end(),
                            [](const auto& a,const auto& b){return a.second.used<b.second.used;});
                        workClocks.erase(old);
                    }
                    auto& animation=workClocks[id];animation.used=frame;
                    animation.clock.advance(work.activity,activity->seconds);
                    work.animationSeconds=animation.clock.seconds;
                    animation.machinery.advance(work.machineryActivity,activity->seconds);
                    work.machinerySeconds=animation.machinery.seconds;
                }
            };
            struct UrbanDraw {const SurfaceSettlement* city;SettlementWork work;float lighting;};
            std::vector<UrbanDraw> urbanDraws;
            for (const auto& settlement : world->settlements) {
                ++stats.surfaceSites;
                auto body = surfaceContact(*world, settlement, activity->seconds);
                economicLighting = 1;
                if (economy) {
                    annotateEconomicPort(body, *economy);
                    if (lightingNetwork == economy->network)
                        if (auto i = economy->siteIndex(body.id); i && *i < settlementLights.size())
                            economicLighting = settlementLights[*i];
                }
                const auto forecast = sampleWeather(world->climate, settlement.direction,
                    glm::length(settlement.landingPoint)-world->radiusMetres,activity->seconds);
                auto work = settlementWork(*world,settlement,forecast,economy.get());
                body.status += " / " + work.status;
                if(settlement.urban){animateWork(body.id,work);urbanDraws.push_back({&settlement,work,economicLighting});}
                const auto delta = pose.orientation * (body.position - planetEye);
                const double distance = glm::length(delta);
                if (distance < 1e-10)
                    continue;
                const auto direction = cameraInverse * (delta / distance);
                const bool selected = activity->selected && *activity->selected == body.id;
                const bool occulted =
                    glm::dot(settlement.direction, planetEye - settlement.landingPoint) < -10;
                activityContacts.push_back(
                    {body, direction, distance, instruments ? instruments->speed : 0, selected, occulted});
                if (occulted)
                    continue;
                const auto point = eye + delta / units;
                if (auto screen = project(point))
                    activityPicks.emplace_back(body.id, *screen);
                if (distance > 2000) {
                    const double size = std::max(4., distance * .000025) / units;
                    addBillboard(point + pose.orientation * settlement.direction * (3 / units), size,
                                 body.tint * .5f);
                }
                if (distance > settlement.radius + 120000)
                    continue;
                animateWork(body.id,work);
                if (work.machineryActivity>.05 || (work.activity>.05&&work.orderCount)) ++stats.surfaceWorking;
                if (work.synchronized && work.outdoorHold) ++stats.surfaceWeatherHolds;
                if (distance < settlement.radius + 6000) {
                    for (const auto& actor : sampleSurfaceWork(*world,settlement,work,activity->seconds)) {
                        if (instances >= 6000) break;
                        const auto delta=pose.orientation*(actor.position-planetEye);
                        const double extent=actor.kind==SurfaceMachine::CargoRover ? 5. :
                            std::max({actor.dimensions.x,actor.dimensions.y,actor.dimensions.z})*4.;
                        const double pixels=extent/std::max(.01,glm::length(delta))*height/(2*std::tan(fieldOfView*.5));
                        if (pixels < 1 || !LOD::visible(cameraInverse*delta/units,extent/units,
                                                       double(width)/height,fieldOfView)) continue;
                        const auto model=glm::translate(glm::dmat4(1),delta/units)*
                            glm::mat4_cast(pose.orientation*actor.orientation)*glm::scale(glm::dmat4(1),actor.dimensions/units);
                        workBatches[size_t(actor.kind)][pixels>60?2:pixels>10?1:0].push_back(
                            {glm::mat4(model),glm::vec4(actor.tint,1)});
                        ++instances;
                        if(actor.kind==SurfaceMachine::CargoRover)++stats.surfaceRovers;else ++stats.surfaceMachines;
                        if(pixels>4 && work.power>.05) {
                            const auto beacon=delta/units+pose.orientation*(actor.orientation*glm::dvec3(0,2.2,0))/units;
                            const auto tint=work.damaged?glm::vec3(1,.10,.025):work.outdoorHold?glm::vec3(1,.40,.04):glm::vec3(.10,.8,.6);
                            addBillboard(beacon+eye,.14/units,tint*float(.7+.3*std::sin(activity->seconds*3+hyperionPhase(actor.id))));
                        }
                    }
                }
                // A terrain-following pad: small shared quads leave the landing
                // plane and original collision surface in exact agreement.
                for (int x = -2; x < 2; x++)
                    for (int z = -2; z < 2; z++) {
                        const auto local =
                            settlement.direction +
                            settlement.orientation * glm::dvec3((x + .5) * 16, 0, (z + .5) * 16) / radius;
                        const auto d = glm::normalize(local);
                        const auto p = d * (radius + universe.terrainHeight(planet, d) + .12);
                        instance(SurfaceMesh::Pad, p, tangentFrame(d), {16, 1, 16}, glm::vec3(.7, .85, .88),
                                 true);
                    }
                for (const auto& building : settlement.buildings) {
                    const auto before = instances;
                    instance(building.kind, building.position, building.orientation, building.dimensions,
                             building.tint);
                    if (instances > before)
                        ++stats.surfaceBuildings;
                    instance(SurfaceMesh::Foundation, building.position, building.orientation,
                             {building.dimensions.x, building.foundationMetres, building.dimensions.z},
                             building.tint);
                }
                for (const auto& road : settlement.roads) {
                    const auto center = (road.a + road.b) * .5, forward = glm::normalize(road.b - road.a);
                    if (settlement.layoutRecipe) {
                        const auto delta = pose.orientation * (center - planetEye) / units;
                        const double span = glm::length(road.b - road.a) + road.width;
                        if (glm::length(delta) > 8000 || !LOD::visible(cameraInverse * delta, span / units,
                                                                       double(width) / height, fieldOfView))
                            continue;
                        const auto quad = [&](std::array<unsigned, 4> corners, glm::vec3 color) {
                            const auto first = uint32_t(paving.vertices.size());
                            for (auto corner : corners) {
                                const auto p = pose.orientation *
                                               ((center - planetEye) + glm::dvec3(road.surface[corner])) /
                                               units;
                                paving.vertices.push_back({float(p.x), float(p.y), float(p.z), rgba(color)});
                            }
                            paving.indices.insert(paving.indices.end(),
                                                  {first, first + 1, first + 2, first, first + 2, first + 3});
                        };
                        quad({1, 2, 6, 5}, road.kind == StreetClass::Footway ? glm::vec3(.42, .49, .55)
                                                                             : glm::vec3(.16, .22, .25));
                        if (road.kind != StreetClass::Footway &&
                            2. / (glm::length(delta) * units) * height / (2 * std::tan(fieldOfView * .5)) >
                                .4) {
                            quad({0, 1, 5, 4}, {.42, .49, .55});
                            quad({2, 3, 7, 6}, {.42, .49, .55});
                        }
                        continue;
                    }
                    const auto q = glm::quatLookAtRH(forward, glm::normalize(center));
                    instance(road.kind == StreetClass::Footway ? SurfaceMesh::Footway : SurfaceMesh::Road,
                             center, q, {road.width, 1, glm::length(road.b - road.a)}, glm::vec3(.8), true);
                }
                for (const auto& prop : settlement.props)
                    instance(prop.kind, prop.position, prop.orientation, prop.dimensions, prop.tint);
                if (distance < 1400) {
                    const unsigned people = std::min(96u, settlement.population);
                    for (unsigned sample = 0; sample < people; sample++) {
                        const unsigned id = unsigned(uint64_t(sample) * settlement.population / people);
                        const auto person = sampleResident(*world, settlement, id, activity->seconds);
                        if (!person.outside || (work.synchronized && work.outdoorHold))
                            continue;
                        const auto before = instances;
                        const Random palette{person.id};
                        const auto tint =
                            glm::mix(glm::vec3(.65, .85, 1), glm::vec3(1, .70, .45), float(palette.unit(3)));
                        const auto size = glm::dvec3(.90 + palette.unit(4) * .2, .90 + palette.unit(5) * .2,
                                                     .90 + palette.unit(4) * .2);
                        instance(person.suited ? SurfaceMesh::Suit : SurfaceMesh::Resident, person.position,
                                 person.orientation, size, tint, true);
                        if (instances > before)
                            ++stats.surfaceResidents;
                    }
                }
            }
            for(const auto& urban:urbanDraws){economicLighting=urban.lighting;
                auto emit=[&](SurfaceMesh kind,glm::dvec3 point,glm::dquat orientation,glm::dvec3 size,glm::vec3 tint){
                    const auto before=instances;instance(kind,point,orientation,size,tint);return instances>before;};
                auto rover=[&](glm::dvec3 point,glm::dquat orientation,glm::vec3 tint){
                    const auto delta=pose.orientation*(point-planetEye);const double pixels=5/std::max(.1,glm::length(delta))*height/(2*std::tan(fieldOfView*.5));
                    if(instances>=6000||pixels<1||!LOD::visible(cameraInverse*delta/units,5/units,double(width)/height,fieldOfView))return;
                    const auto model=glm::translate(glm::dmat4(1),delta/units)*glm::mat4_cast(pose.orientation*orientation)*glm::scale(glm::dmat4(1),glm::dvec3(1)/units);
                    workBatches[size_t(SurfaceMachine::CargoRover)][pixels>60?2:pixels>10?1:0].push_back({glm::mat4(model),glm::vec4(tint,1)});++instances;++stats.surfaceRovers;};
                renderUrban(planet,*urban.city,pose,planetEye,nav,urban.work,emit,rover,paving,instances);
            }
            drawCityPaving(paving, planet, nav);
            for(size_t role=0;role<workBatches.size();++role)for(unsigned detail=0;detail<3;++detail){
                auto& objects=workBatches[role][detail];if(objects.empty())continue;
                auto& mesh=workMeshes[role][detail];
                if(!bgfx::isValid(mesh.vertices)){const auto source=surfaceMachineMesh(SurfaceMachine(role),detail);
                    mesh=upload(source);surfaceMeshBytes+=source.bytes();}
                const auto count=bgfx::getAvailInstanceDataBuffer(uint32_t(objects.size()),80);
                if(count){bgfx::InstanceDataBuffer buffer;bgfx::allocInstanceDataBuffer(&buffer,count,80);
                    std::memcpy(buffer.data,objects.data(),count*sizeof(Instance));
                    bgfx::setVertexBuffer(0,mesh.vertices);bgfx::setIndexBuffer(mesh.indices);bgfx::setInstanceDataBuffer(&buffer);
                    Geometry source;const ScenePresentation presentation{source,planet.address,{.45,.3,.08,.02},
                        hyperionPhase(planet.seed),1.f,{3,0,0,0}};
                    hyperion->bind(presentation,{},glm::vec3(lightAt(planet.address,nav)),glm::mat4(1),0,activity->seconds);
                    bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_GEQUAL|BGFX_STATE_MSAA);
                    bgfx::submit(61,hyperion->geometryProgram());stats.surfaceTriangles+=count*mesh.count/3;
                    if(updateSun){bgfx::setVertexBuffer(0,mesh.vertices);bgfx::setIndexBuffer(mesh.indices);
                        bgfx::setInstanceDataBuffer(&buffer);bgfx::setState(BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_LESS);
                        bgfx::submit(70,hyperion->sunProgram());}
                }objects.clear();
            }
            for (size_t role = 0; role < batches.size(); role++)
                for (unsigned detail = 0; detail < 3; detail++) {
                    auto& objects = batches[role][detail];
                    if (objects.empty())
                        continue;
                    auto& mesh = surfaceMeshes[role][detail];
                    if (!bgfx::isValid(mesh.vertices)) {
                        const auto source = surfaceMesh(SurfaceMesh(role), detail);
                        mesh = upload(source);
                        surfaceMeshBytes += source.bytes();
                    }
                    const auto count = bgfx::getAvailInstanceDataBuffer(uint32_t(objects.size()), 80);
                    if (count) {
                        bgfx::InstanceDataBuffer buffer;
                        bgfx::allocInstanceDataBuffer(&buffer, count, 80);
                        std::memcpy(buffer.data, objects.data(), count * sizeof(Instance));
                        bgfx::setVertexBuffer(0, mesh.vertices);
                        bgfx::setIndexBuffer(mesh.indices);
                        bgfx::setInstanceDataBuffer(&buffer);
                        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z |
                                       BGFX_STATE_DEPTH_TEST_GEQUAL | BGFX_STATE_MSAA);
                        {
                            Geometry source;
                            const ScenePresentation presentation{
                                source,
                                planet.address,
                                {.42, .6, .025, .01},
                                hyperionPhase(planet.seed) + float(role) * .13f,
                                1.f,
                                {4, float(role), float(units), 0},
                                {float(glm::dot(glm::normalize(planetEye), glm::inverse(pose.orientation) *
                                                                               lightAt(planet.address, nav))),
                                 float(world->climate.pressureBar), 0, 0},
                                glm::vec4(glm::vec3(pose.orientation * glm::normalize(planetEye)),
                                          float(world->climate.pressureBar))};
                            hyperion->bind(presentation, {}, glm::vec3(lightAt(planet.address, nav)),
                                           glm::mat4(1), 0, activity->seconds);
                        }
                        bgfx::submit(61, hyperion->geometryProgram());
                        stats.surfaceTriangles += count * mesh.count / 3;
                    }
                    objects.clear();
                }
            bool completeSun = true;
            if (updateSun)
                for (size_t role = 0; role < sunCasters.size(); role++) {
                    auto& objects = sunCasters[role];
                    if (objects.empty())
                        continue;
                    auto& mesh = surfaceMeshes[role][1];
                    if (!bgfx::isValid(mesh.vertices)) {
                        const auto source = surfaceMesh(SurfaceMesh(role), 1);
                        mesh = upload(source);
                        surfaceMeshBytes += source.bytes();
                    }
                    const auto count = bgfx::getAvailInstanceDataBuffer(uint32_t(objects.size()), 80);
                    completeSun &= count == objects.size();
                    if (count) {
                        bgfx::InstanceDataBuffer buffer;
                        bgfx::allocInstanceDataBuffer(&buffer, count, 80);
                        std::memcpy(buffer.data, objects.data(), count * sizeof(Instance));
                        bgfx::setVertexBuffer(0, mesh.vertices);
                        bgfx::setIndexBuffer(mesh.indices);
                        bgfx::setInstanceDataBuffer(&buffer);
                        bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
                        bgfx::submit(70, hyperion->sunProgram());
                    }
                    objects.clear();
                }
            if (updateSun)
                hyperion->completeSun(completeSun && terrainSunComplete);
        }
    }
}
