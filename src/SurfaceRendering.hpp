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
    const auto weather = sampleWeather(climate, localEye, altitude, seconds);
    char report[120];
    std::snprintf(report, sizeof(report), "%s  %.0fK  %.2fBAR  WIND %.0fM/S",
                  std::string(weatherName(weather.type)).c_str(), weather.temperatureK, weather.pressureBar,
                  glm::length(weather.wind));
    weatherReport = report;
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
    const double shellRadius = radius + climate.cloudHeightMetres;
    const auto direction = [](double latitude, double longitude) {
        return glm::dvec3(std::cos(latitude) * std::cos(longitude), std::sin(latitude),
                          std::cos(latitude) * std::sin(longitude));
    };
    const auto light = glm::inverse(pose.orientation) * lightAt(planet.address, nav);
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
            const auto first = uint32_t(clouds.vertices.size());
            for (auto q : {direction(a, x), direction(b, x), direction(b, z), direction(a, z)}) {
                const auto sample = sampleWeather(climate, q, 0, seconds);
                const float alpha = float(sample.cloud * .54 * std::min(1., climate.pressureBar * 2));
                const auto delta = pose.orientation * (q * shellRadius - localEye) / units;
                const float sun = float(.22 + .78 * std::max(0., glm::dot(q, light)));
                const glm::vec3 tint =
                    mode == RenderMode::Original
                        ? glm::vec3(.75, .81, .88)
                        : glm::mix(glm::vec3(.32, .52, .78), glm::vec3(.83, .67, .88), float(sample.storm));
                clouds.vertices.push_back({float(delta.x), float(delta.y), float(delta.z),
                                           rgba(tint * (mode == RenderMode::Original ? sun : 1.f), alpha)});
            }
            clouds.indices.insert(clouds.indices.end(),
                                  {first, first + 1, first + 2, first, first + 2, first + 3});
            ++stats.weatherCells;
        }
    if (mode == RenderMode::Original)
        transient(clouds, 63, false, false, true, false, true);
    else if (!clouds.vertices.empty()) {
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
            const ScenePresentation presentation{source,
                                                 planet.address,
                                                 {},
                                                 hyperionPhase(climate.seed),
                                                 mode == RenderMode::Hybrid ? .36f : 1.f};
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
    if (!instruments || instruments->shrinking || altitude > climate.cloudHeightMetres || units < .1 ||
        weather.precipitation < .06)
        return;
    Mesh precipitation, flakes;
    const double cellSize = weather.type == WeatherType::Dust ? 18 : 9;
    const auto cell = glm::floor(localEye / cellSize);
    const glm::dvec3 down = -glm::normalize(localEye);
    for (unsigned i = 0; i < 343 && stats.weatherParticles < 96; i++) {
        const auto grid = cell + glm::dvec3(int(i % 7) - 3, int(i / 7 % 7) - 3, int(i / 49) - 3);
        const auto hash = mix64(climate.seed ^ uint64_t(int64_t(grid.x)) ^ mix64(uint64_t(int64_t(grid.y))) ^
                                mix64(uint64_t(int64_t(grid.z))));
        const Random local{hash};
        auto point = (grid + glm::dvec3(local.unit(0), local.unit(1), local.unit(2))) * cellSize;
        const double fallSpeed = weather.type == WeatherType::Snow   ? 1.4
                                 : weather.type == WeatherType::Dust ? 2
                                                                     : 18;
        const double phase = std::fmod(seconds * fallSpeed + local.unit(3) * cellSize, cellSize);
        point += down * phase + weather.wind * (phase / std::max(1., fallSpeed) * .1);
        const auto delta = pose.orientation * (point - localEye) / units;
        if ((cameraInverse * delta).z >= -.00005 || glm::length(delta) > 10000 ||
            !LOD::visible(cameraInverse * delta, .2 / units, double(width) / height, fieldOfView))
            continue;
        const auto tint =
            weather.type == WeatherType::Dust ? glm::vec3(.72, .43, .25) : glm::vec3(.54, .75, .86);
        const auto tail = pose.orientation *
                          (down * (weather.type == WeatherType::Snow ? .2 : 2.) + weather.wind * .04) / units;
        if (weather.type == WeatherType::Snow || weather.type == WeatherType::Dust) {
            const double physicalRadius =
                weather.type == WeatherType::Snow ? .035 + local.unit(4) * .055 : .12 + local.unit(4) * .18;
            const double pixelRadius = weather.type == WeatherType::Snow ? 2.5 : 7.;
            const double r =
                std::min(physicalRadius, glm::length(point - localEye) *
                                             (2 * std::tan(fieldOfView * .5) / height) * pixelRadius);
            const auto right = cameraQ * glm::dvec3(r, 0, 0) / units,
                       up = cameraQ * glm::dvec3(0, r, 0) / units;
            const auto color = weather.type == WeatherType::Snow ? glm::vec3(.9, .96, 1) : tint;
            const float alpha = weather.type == WeatherType::Snow ? .8f : .18f;
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
        } else
            addLine(precipitation, delta, delta + tail,
                    tint * float(.6 + std::min(1., weather.precipitation) * .4));
        ++stats.weatherParticles;
    }
    transient(precipitation, 63, true, false, true, false, true);
    transient(flakes, 63, false, false, true, false, true);
}
void drawCityPaving(const Mesh& mesh, const Node& planet, const Navigation& nav) {
    if (mesh.vertices.empty())
        return;
    if (mode == RenderMode::Original) {
        transient(mesh, 61, false, false, true, true);
    } else {
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
        const ScenePresentation presentation{source,
                                             planet.address,
                                             {.42, .6, .025, .01},
                                             float(SurfaceMesh::Road) / float(SurfaceMesh::Count),
                                             mode == RenderMode::Hybrid ? .36f : 1.f};
        hyperion->bind(presentation, {}, glm::vec3(lightAt(planet.address, nav)), glm::mat4(1), 0,
                       activity->seconds);
        bgfx::submit(61, hyperion->geometryProgram());
    }
    stats.surfaceTriangles += mesh.indexCount() / 3;
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
            if (sunShadows && mode != RenderMode::Original && instruments && !instruments->shrinking &&
                instruments->altitude < 5000 && units > .01 && !hyperion->sunActive()) {
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
                batches[size_t(kind)][detail].push_back({glm::mat4(model), glm::vec4(tint, 1)});
                ++instances;
            };
            for (const auto& settlement : world->settlements) {
                ++stats.surfaceSites;
                const auto body = surfaceContact(*world, settlement, activity->seconds);
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
                        if (!person.outside)
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
            drawCityPaving(paving, planet, nav);
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
                        if (mode != RenderMode::Original) {
                            Geometry source;
                            const ScenePresentation presentation{
                                source,
                                planet.address,
                                {.42, .6, .025, .01},
                                hyperionPhase(planet.seed) + float(role) * .13f,
                                mode == RenderMode::Hybrid ? .36f : 1.f,
                                {4, float(role), float(units), 0},
                                {float(glm::dot(glm::normalize(planetEye), glm::inverse(pose.orientation) *
                                                                               lightAt(planet.address, nav))),
                                 float(world->climate.pressureBar), 0, 0},
                                glm::vec4(glm::vec3(pose.orientation * glm::normalize(planetEye)),
                                          float(world->climate.pressureBar))};
                            hyperion->bind(presentation, {}, glm::vec3(lightAt(planet.address, nav)),
                                           glm::mat4(1), 0, activity->seconds);
                        }
                        bgfx::submit(61, mode == RenderMode::Original ? instanceProgram
                                                                      : hyperion->geometryProgram());
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
