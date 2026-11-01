cmake_minimum_required(VERSION 3.24)
project(Infinite VERSION 0.44.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
option(INFINITE_VIEWER "Build the SDL3/bgfx interactive application" ON)
option(INFINITE_SANITIZE "Enable address and undefined-behaviour sanitizers" OFF)
option(INFINITE_GPU_TESTS "Run the native tour and framebuffer checks (requires a desktop)" OFF)
include(cmake/Dependencies.cmake)
add_library(infinite_core src/Core.cpp src/Universe.cpp src/Processes.cpp src/Planetology.cpp src/StellarEvolution.cpp src/Terrain.cpp src/Crystallography.cpp src/Geometry.cpp src/World.cpp src/Navigation.cpp src/Flight.cpp src/Streaming.cpp src/Continuity.cpp src/Presentation.cpp src/StellarOptics.cpp src/SurfaceShading.cpp src/MaterialTexture.cpp src/Piloting.cpp src/ShipDynamics.cpp src/FlightEnvironment.cpp src/Economy.cpp src/EconomyDevelopment.cpp src/EconomyHauling.cpp src/EconomyServices.cpp src/EconomyWeapons.cpp src/Weapons.cpp src/EconomyPlanning.cpp src/EconomySignals.cpp src/EconomyRecovery.cpp src/EconomyRecoverySave.cpp src/EconomicNetwork.cpp src/EconomyRuntime.cpp src/Cockpit.cpp src/CockpitMotion.cpp src/Activity.cpp src/ActivityMeshes.cpp src/Weather.cpp src/WeatherField.cpp src/SurfaceLife.cpp src/SurfaceLifeCache.cpp src/SurfaceMeshes.cpp src/City.cpp src/CityMeshes.cpp)
target_include_directories(infinite_core PUBLIC include)
target_link_libraries(infinite_core PUBLIC glm::glm FastNoise TBB::tbb SQLite::SQLite3 libzstd_static)
target_compile_features(infinite_core PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(infinite_core PRIVATE /W4 /fp:strict)
else()
  target_compile_options(infinite_core PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
endif()
if(INFINITE_SANITIZE AND NOT MSVC)
  target_compile_options(infinite_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(infinite_core PUBLIC -fsanitize=address,undefined)
endif()
add_executable(infinite_tests tests/Tests.cpp)
target_link_libraries(infinite_tests PRIVATE infinite_core)
add_executable(infinite_benchmark tools/Benchmark.cpp)
target_link_libraries(infinite_benchmark PRIVATE infinite_core)
add_executable(infinite_continuity_tests tests/ContinuityTests.cpp)
target_link_libraries(infinite_continuity_tests PRIVATE infinite_core)
add_executable(infinite_process_tests tests/ProcessTests.cpp)
target_link_libraries(infinite_process_tests PRIVATE infinite_core)
add_executable(infinite_performance_tests tests/PerformanceTests.cpp)
target_link_libraries(infinite_performance_tests PRIVATE infinite_core)
if(MSVC)
  target_compile_options(infinite_performance_tests PRIVATE /fp:strict)
else()
  target_compile_options(infinite_performance_tests PRIVATE -ffp-contract=off)
endif()
enable_testing()
add_executable(infinite_weapons_tests tests/WeaponsTests.cpp)
target_link_libraries(infinite_weapons_tests PRIVATE infinite_core)
add_test(NAME weapons_combat_and_equipment COMMAND infinite_weapons_tests "${CMAKE_BINARY_DIR}/economy/weapons-test")
set_tests_properties(weapons_combat_and_equipment PROPERTIES TIMEOUT 180)
add_executable(infinite_economy_recovery_tests tests/EconomyRecoveryTests.cpp)
target_link_libraries(infinite_economy_recovery_tests PRIVATE infinite_core)
add_test(NAME economic_recovery_and_outcomes COMMAND infinite_economy_recovery_tests)
set_tests_properties(economic_recovery_and_outcomes PROPERTIES TIMEOUT 120)
add_executable(infinite_economy_signals_tests tests/EconomySignalsTests.cpp)
target_link_libraries(infinite_economy_signals_tests PRIVATE infinite_core)
add_test(NAME economic_world_feedback COMMAND infinite_economy_signals_tests)
set_tests_properties(economic_world_feedback PROPERTIES TIMEOUT 120)
add_executable(infinite_economy_services_tests tests/EconomyServicesTests.cpp)
target_link_libraries(infinite_economy_services_tests PRIVATE infinite_core)
add_test(NAME economic_ship_services_and_planning COMMAND infinite_economy_services_tests "${CMAKE_BINARY_DIR}/economy/services-test")
set_tests_properties(economic_ship_services_and_planning PROPERTIES TIMEOUT 180)
add_executable(infinite_cockpit_motion_tests tests/CockpitMotionTests.cpp)
target_link_libraries(infinite_cockpit_motion_tests PRIVATE infinite_core)
add_test(NAME cockpit_motion_and_feedback COMMAND infinite_cockpit_motion_tests)
set_tests_properties(cockpit_motion_and_feedback PROPERTIES TIMEOUT 60)
add_executable(infinite_economy_hauling_tests tests/EconomyHaulingTests.cpp)
target_link_libraries(infinite_economy_hauling_tests PRIVATE infinite_core)
add_test(NAME economic_hauling_and_career COMMAND infinite_economy_hauling_tests "${CMAKE_BINARY_DIR}/economy/haul-test")
set_tests_properties(economic_hauling_and_career PROPERTIES TIMEOUT 180)
add_executable(infinite_economy_development_tests tests/EconomyDevelopmentTests.cpp)
target_link_libraries(infinite_economy_development_tests PRIVATE infinite_core)
add_test(NAME economic_investment_and_trade COMMAND infinite_economy_development_tests "${CMAKE_BINARY_DIR}/economy/development-test")
set_tests_properties(economic_investment_and_trade PROPERTIES TIMEOUT 180)
add_executable(infinite_economy_tests tests/EconomyTests.cpp)
target_link_libraries(infinite_economy_tests PRIVATE infinite_core)
add_test(NAME persistent_economic_supply_chains COMMAND infinite_economy_tests "${CMAKE_BINARY_DIR}/economy/runtime-test")
set_tests_properties(persistent_economic_supply_chains PROPERTIES TIMEOUT 180)
add_executable(infinite_ship_physics_tests tests/ShipPhysicsTests.cpp)
target_link_libraries(infinite_ship_physics_tests PRIVATE infinite_core)
add_test(NAME physical_ship_forces_and_guidance COMMAND infinite_ship_physics_tests)
set_tests_properties(physical_ship_forces_and_guidance PROPERTIES TIMEOUT 120)
add_executable(infinite_stellar_evolution_tests tests/StellarEvolutionTests.cpp)
target_link_libraries(infinite_stellar_evolution_tests PRIVATE infinite_core)
add_test(NAME stellar_evolution_and_inheritance COMMAND infinite_stellar_evolution_tests)
set_tests_properties(stellar_evolution_and_inheritance PROPERTIES TIMEOUT 120)
add_test(NAME stellar_catalogue_preservation COMMAND infinite_population_snapshot --check "${PROJECT_SOURCE_DIR}/tests/data/stellar-catalogue-0.35.txt")
set_tests_properties(stellar_catalogue_preservation PROPERTIES TIMEOUT 120)
add_executable(infinite_planet_diversity_tests tests/PlanetDiversityTests.cpp)
target_link_libraries(infinite_planet_diversity_tests PRIVATE infinite_core)
add_test(NAME planetary_type_diversity COMMAND infinite_planet_diversity_tests)
set_tests_properties(planetary_type_diversity PROPERTIES TIMEOUT 120)

add_executable(infinite_weather_process_tests tests/WeatherProcessTests.cpp)
target_link_libraries(infinite_weather_process_tests PRIVATE infinite_core)
add_test(NAME planetary_weather_processes COMMAND infinite_weather_process_tests)
add_executable(infinite_weather_benchmark tools/WeatherBenchmark.cpp)
target_link_libraries(infinite_weather_benchmark PRIVATE infinite_core)
add_executable(infinite_stellar_optics_tests tests/StellarOpticsTests.cpp)
target_link_libraries(infinite_stellar_optics_tests PRIVATE infinite_core)
add_test(NAME addressed_stellar_optics COMMAND infinite_stellar_optics_tests)
add_executable(infinite_weather_field_tests tests/WeatherFieldTests.cpp)
target_link_libraries(infinite_weather_field_tests PRIVATE infinite_core)
add_test(NAME read_only_weather_optics COMMAND infinite_weather_field_tests)
add_executable(infinite_window_tests tests/WindowLayoutTests.cpp)
target_link_libraries(infinite_window_tests PRIVATE infinite_core)
add_test(NAME window_fits_display_work_area COMMAND infinite_window_tests)
add_executable(infinite_depth_tests tests/WorldDepthTests.cpp)
target_link_libraries(infinite_depth_tests PRIVATE infinite_core)
add_test(NAME multiscale_depth_precision COMMAND infinite_depth_tests)
add_executable(infinite_surface_site_snapshot tools/SurfaceSiteSnapshot.cpp)
target_link_libraries(infinite_surface_site_snapshot PRIVATE infinite_core)
add_test(NAME surface_site_preservation COMMAND infinite_surface_site_snapshot --check "${PROJECT_SOURCE_DIR}/tests/data/surface-sites-0.19.txt")
set_tests_properties(surface_site_preservation PROPERTIES TIMEOUT 90)
add_executable(infinite_city_tests tests/CityTests.cpp)
target_link_libraries(infinite_city_tests PRIVATE infinite_core)
add_test(NAME procedural_city_structure COMMAND infinite_city_tests)
set_tests_properties(procedural_city_structure PROPERTIES TIMEOUT 120)
add_executable(infinite_surface_life_tests tests/SurfaceLifeTests.cpp)
target_link_libraries(infinite_surface_life_tests PRIVATE infinite_core)
add_test(NAME planetary_weather_and_surface_life COMMAND infinite_surface_life_tests)
set_tests_properties(planetary_weather_and_surface_life PROPERTIES TIMEOUT 120)
add_executable(infinite_terrain_tests tests/TerrainTests.cpp)
target_link_libraries(infinite_terrain_tests PRIVATE infinite_core)
add_test(NAME solid_planet_local_terrain COMMAND infinite_terrain_tests)
set_tests_properties(solid_planet_local_terrain PROPERTIES TIMEOUT 90)
add_executable(infinite_cockpit_tests tests/CockpitTests.cpp)
target_link_libraries(infinite_cockpit_tests PRIVATE infinite_core)
add_test(NAME cockpit_layout_and_targeting COMMAND infinite_cockpit_tests)
add_executable(infinite_population_tests tests/PopulationTests.cpp)
target_link_libraries(infinite_population_tests PRIVATE infinite_core)
add_test(NAME planetary_population_and_moons COMMAND infinite_population_tests)
set_tests_properties(planetary_population_and_moons PROPERTIES TIMEOUT 120)
add_executable(infinite_population_snapshot tools/PopulationSnapshot.cpp)
target_link_libraries(infinite_population_snapshot PRIVATE infinite_core)
add_test(NAME legacy_population_preservation COMMAND infinite_population_snapshot --check "${PROJECT_SOURCE_DIR}/tests/data/legacy-0.12.txt")
set_tests_properties(legacy_population_preservation PROPERTIES TIMEOUT 90)
add_test(NAME established_world_preservation COMMAND infinite_population_snapshot --check "${PROJECT_SOURCE_DIR}/tests/data/planet-catalogue-0.34.txt")
set_tests_properties(established_world_preservation PROPERTIES TIMEOUT 120)
add_executable(infinite_activity_tests tests/ActivityTests.cpp)
target_link_libraries(infinite_activity_tests PRIVATE infinite_core)
add_test(NAME persistent_orbital_activity COMMAND infinite_activity_tests)
set_tests_properties(persistent_orbital_activity PROPERTIES TIMEOUT 90)
add_executable(infinite_operating_tests tests/OperatingScaleTests.cpp)
target_link_libraries(infinite_operating_tests PRIVATE infinite_core)
add_test(NAME operating_regimes_and_shrinking COMMAND infinite_operating_tests)
set_tests_properties(operating_regimes_and_shrinking PROPERTIES TIMEOUT 90)
add_executable(infinite_piloting_tests tests/PilotingTests.cpp)
target_link_libraries(infinite_piloting_tests PRIVATE infinite_core)
add_test(NAME ship_dynamics_and_landing COMMAND infinite_piloting_tests)
set_tests_properties(ship_dynamics_and_landing PROPERTIES TIMEOUT 90)
add_executable(infinite_handling_tests tests/HandlingTests.cpp)
target_link_libraries(infinite_handling_tests PRIVATE infinite_core)
add_test(NAME ship_handling_and_roll COMMAND infinite_handling_tests)
set_tests_properties(ship_handling_and_roll PROPERTIES TIMEOUT 90)
add_executable(infinite_streaming_tests tests/StreamingTests.cpp)
target_link_libraries(infinite_streaming_tests PRIVATE infinite_core)
add_test(NAME unbounded_spatial_streaming COMMAND infinite_streaming_tests)
set_tests_properties(unbounded_spatial_streaming PROPERTIES TIMEOUT 120)
add_executable(infinite_flight_tests tests/FlightTests.cpp)
target_link_libraries(infinite_flight_tests PRIVATE infinite_core)
add_test(NAME seamless_scale_flight COMMAND infinite_flight_tests)
set_tests_properties(seamless_scale_flight PROPERTIES TIMEOUT 120)
add_executable(infinite_navigation_tests tests/NavigationTests.cpp)
target_link_libraries(infinite_navigation_tests PRIVATE infinite_core)
add_test(NAME free_flight_navigation COMMAND infinite_navigation_tests)
set_tests_properties(free_flight_navigation PROPERTIES TIMEOUT 30)
add_executable(infinite_presentation_tests tests/PresentationTests.cpp)
target_link_libraries(infinite_presentation_tests PRIVATE infinite_core)
add_executable(infinite_shading_tests tests/SurfaceShadingTests.cpp)
target_link_libraries(infinite_shading_tests PRIVATE infinite_core)
add_executable(infinite_material_tests tests/MaterialTextureTests.cpp)
target_link_libraries(infinite_material_tests PRIVATE infinite_core)
add_test(NAME filtered_material_texture COMMAND infinite_material_tests)
add_test(NAME surface_shading_preserves_mesh COMMAND infinite_shading_tests)
add_test(NAME rendering_preserves_universe COMMAND infinite_presentation_tests)
set_tests_properties(rendering_preserves_universe PROPERTIES TIMEOUT 90)
add_test(NAME performance_invariants COMMAND infinite_performance_tests)
set_tests_properties(performance_invariants PROPERTIES TIMEOUT 90)
add_test(NAME structural_processes COMMAND infinite_process_tests)
set_tests_properties(structural_processes PROPERTIES TIMEOUT 120)
add_test(NAME universe_regressions COMMAND infinite_tests)
set_tests_properties(universe_regressions PROPERTIES TIMEOUT 60)
add_test(NAME persistence_process_write COMMAND infinite_tests --write "${CMAKE_BINARY_DIR}/restart-test.sqlite")
set_tests_properties(persistence_process_write PROPERTIES FIXTURES_SETUP persisted)
add_test(NAME persistence_process_read COMMAND infinite_tests --read "${CMAKE_BINARY_DIR}/restart-test.sqlite")
set_tests_properties(persistence_process_read PROPERTIES FIXTURES_REQUIRED persisted)
add_test(NAME cross_scale_round_trip COMMAND infinite_continuity_tests)
set_tests_properties(cross_scale_round_trip PROPERTIES TIMEOUT 120)
add_test(NAME continuity_restart_write COMMAND infinite_continuity_tests --write "${CMAKE_BINARY_DIR}/continuity-restart.sqlite")
set_tests_properties(continuity_restart_write PROPERTIES FIXTURES_SETUP continuity_restart TIMEOUT 90)
add_test(NAME continuity_restart_read COMMAND infinite_continuity_tests --read "${CMAKE_BINARY_DIR}/continuity-restart.sqlite")
set_tests_properties(continuity_restart_read PROPERTIES FIXTURES_REQUIRED continuity_restart TIMEOUT 90)
if(INFINITE_VIEWER)
  if(WIN32 AND (NOT EXISTS "${PROJECT_SOURCE_DIR}/shaders/bin/dxbc/vs_hyperion_smooth.bin"
      OR NOT EXISTS "${PROJECT_SOURCE_DIR}/shaders/bin/dxbc/fs_hyperion.bin"
      OR NOT EXISTS "${PROJECT_SOURCE_DIR}/shaders/bin/dxbc/fs_hyperion_flight.bin"
      OR NOT EXISTS "${PROJECT_SOURCE_DIR}/shaders/bin/dxbc/fs_hyperion_flight_post.bin"
      OR NOT EXISTS "${PROJECT_SOURCE_DIR}/shaders/bin/dxbc/fs_hyperion_sky.bin"))
    find_program(INFINITE_SHADERC NAMES shaderc REQUIRED)
    find_package(Python3 3.9 COMPONENTS Interpreter REQUIRED)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/compile_shaders.py"
      --shaderc "${INFINITE_SHADERC}" --backends dxbc COMMAND_ERROR_IS_FATAL ANY)
  endif()
  include(cmake/EmbedHyperion.cmake)
  add_executable(infinite src/Main.cpp src/Renderer.cpp src/HyperionRenderer.cpp)
  target_link_libraries(infinite PRIVATE infinite_core SDL3::SDL3 bgfx)
  target_include_directories(infinite PRIVATE "${BGFX_DIR}/examples/common/debugdraw" "${BX_DIR}/include" "${CMAKE_BINARY_DIR}/generated")
  if(APPLE)
    target_link_libraries(infinite PRIVATE "-framework Cocoa" "-framework QuartzCore")
  endif()
  if(INFINITE_GPU_TESTS)
    find_package(Python3 3.9 COMPONENTS Interpreter REQUIRED)
    add_test(NAME native_hyperion_settings COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/hyperion-settings"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunHyperionSettings.cmake")
    set_tests_properties(native_hyperion_settings PROPERTIES TIMEOUT 150)
    add_test(NAME native_cockpit_motion COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/cockpit-motion-native"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunCockpitMotion.cmake")
    set_tests_properties(native_cockpit_motion PROPERTIES FIXTURES_SETUP cockpit_motion_captures TIMEOUT 180)
    add_test(NAME native_cockpit_motion_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_cockpit_motion.py" "${CMAKE_BINARY_DIR}/cockpit-motion-native/captures")
    set_tests_properties(native_cockpit_motion_images PROPERTIES FIXTURES_REQUIRED cockpit_motion_captures)
    add_test(NAME native_economic_loop COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/economy/native"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunEconomy.cmake")
    set_tests_properties(native_economic_loop PROPERTIES FIXTURES_SETUP economic_captures TIMEOUT 420)
    add_test(NAME native_economic_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_economy_captures.py" "${CMAKE_BINARY_DIR}/economy/native")
    set_tests_properties(native_economic_images PROPERTIES FIXTURES_REQUIRED economic_captures)
    add_test(NAME native_stellar_diversity COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/star-diversity/native"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunStellarDiversity.cmake")
    set_tests_properties(native_stellar_diversity PROPERTIES FIXTURES_SETUP stellar_diversity TIMEOUT 450)
    add_test(NAME native_stellar_diversity_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_stellar_diversity.py" "${CMAKE_BINARY_DIR}/star-diversity/native")
    set_tests_properties(native_stellar_diversity_images PROPERTIES FIXTURES_REQUIRED stellar_diversity)
    add_test(NAME native_planet_diversity COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/planet-diversity/native"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunPlanetDiversity.cmake")
    set_tests_properties(native_planet_diversity PROPERTIES FIXTURES_SETUP planet_diversity TIMEOUT 420)
    add_test(NAME native_planet_diversity_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_planet_diversity.py" "${CMAKE_BINARY_DIR}/planet-diversity/native")
    set_tests_properties(native_planet_diversity_images PROPERTIES FIXTURES_REQUIRED planet_diversity)
    add_test(NAME native_weather_visibility COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/weather-visibility"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunWeatherVisibility.cmake")
    set_tests_properties(native_weather_visibility PROPERTIES FIXTURES_SETUP weather_visibility TIMEOUT 270)
    add_test(NAME native_weather_visibility_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_weather_visibility.py" "${CMAKE_BINARY_DIR}/weather-visibility")
    set_tests_properties(native_weather_visibility_images PROPERTIES FIXTURES_REQUIRED weather_visibility)
    add_test(NAME native_landing_light COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/landing-light"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunLandingLight.cmake")
    set_tests_properties(native_landing_light PROPERTIES FIXTURES_SETUP landing_light TIMEOUT 150)
    add_test(NAME native_landing_light_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_landing_light.py" "${CMAKE_BINARY_DIR}/landing-light")
    set_tests_properties(native_landing_light_images PROPERTIES FIXTURES_REQUIRED landing_light)
    add_test(NAME native_celestial_optics COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/celestial-optics"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunCelestialOptics.cmake")
    set_tests_properties(native_celestial_optics PROPERTIES FIXTURES_SETUP celestial_optics TIMEOUT 270)
    add_test(NAME native_celestial_optics_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_celestial_optics.py" "${CMAKE_BINARY_DIR}/celestial-optics")
    set_tests_properties(native_celestial_optics_images PROPERTIES FIXTURES_REQUIRED celestial_optics)
    add_test(NAME native_cloud_volumes COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/cloud-volumes"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunCloudVolume.cmake")
    set_tests_properties(native_cloud_volumes PROPERTIES FIXTURES_SETUP cloud_volumes TIMEOUT 270)
    add_test(NAME native_cloud_volume_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_cloud_volumes.py" "${CMAKE_BINARY_DIR}/cloud-volumes")
    set_tests_properties(native_cloud_volume_images PROPERTIES FIXTURES_REQUIRED cloud_volumes)
    add_test(NAME native_polar_optics COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/polar-optics"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunPolarOptics.cmake")
    set_tests_properties(native_polar_optics PROPERTIES FIXTURES_SETUP polar_optics TIMEOUT 270)
    add_test(NAME native_polar_optics_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_polar_optics.py" "${CMAKE_BINARY_DIR}/polar-optics")
    set_tests_properties(native_polar_optics_images PROPERTIES FIXTURES_REQUIRED polar_optics)
    add_test(NAME native_ui_fit COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/ui-fit-native"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunUiFit.cmake")
    set_tests_properties(native_ui_fit PROPERTIES FIXTURES_SETUP ui_fit TIMEOUT 150)
    add_test(NAME native_ui_fit_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_ui_fit.py" "${CMAKE_BINARY_DIR}/ui-fit-native")
    set_tests_properties(native_ui_fit_images PROPERTIES FIXTURES_REQUIRED ui_fit)
    add_test(NAME native_planet_stability COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/planet-stability"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunPlanetStability.cmake")
    set_tests_properties(native_planet_stability PROPERTIES FIXTURES_SETUP planet_stability TIMEOUT 180)
    add_test(NAME native_planet_stability_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_planet_stability.py" "${CMAKE_BINARY_DIR}/planet-stability/captures")
    set_tests_properties(native_planet_stability_images PROPERTIES FIXTURES_REQUIRED planet_stability)
    add_test(NAME native_weather_and_surface_life COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/surface-native"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunSurfaceLife.cmake")
    set_tests_properties(native_weather_and_surface_life PROPERTIES FIXTURES_SETUP surface_life_captures TIMEOUT 270)
    add_test(NAME native_weather_and_surface_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_surface_captures.py" "${CMAKE_BINARY_DIR}/surface-native")
    set_tests_properties(native_weather_and_surface_images PROPERTIES FIXTURES_REQUIRED surface_life_captures)
    add_test(NAME native_solid_terrain COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/terrain-native"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunTerrain.cmake")
    set_tests_properties(native_solid_terrain PROPERTIES FIXTURES_SETUP terrain_captures TIMEOUT 150)
    add_test(NAME native_solid_terrain_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_terrain_captures.py" "${CMAKE_BINARY_DIR}/terrain-native")
    set_tests_properties(native_solid_terrain_images PROPERTIES FIXTURES_REQUIRED terrain_captures)
    add_test(NAME native_surface_quality COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/surface-quality" "-DQUALITY=ON"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunTerrain.cmake")
    set_tests_properties(native_surface_quality PROPERTIES FIXTURES_SETUP surface_quality_captures TIMEOUT 210)
    add_test(NAME native_surface_quality_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_surface_quality.py" "${CMAKE_BINARY_DIR}/surface-quality")
    set_tests_properties(native_surface_quality_images PROPERTIES FIXTURES_REQUIRED surface_quality_captures)
    add_test(NAME native_surface_frame_and_retina COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/surface-frame-retina"
      "-DSURFACE_FRAME=ON" "-DWIDTH=2840" "-DHEIGHT=1800"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunTerrain.cmake")
    set_tests_properties(native_surface_frame_and_retina PROPERTIES FIXTURES_SETUP surface_retina_captures TIMEOUT 210)
    add_test(NAME native_surface_frame_and_retina_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_surface_quality.py" "${CMAKE_BINARY_DIR}/surface-frame-retina" 2840 1800)
    set_tests_properties(native_surface_frame_and_retina_images PROPERTIES FIXTURES_REQUIRED surface_retina_captures)
    add_test(NAME native_grazing_sun_receivers COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/grazing-surface"
      "-DVIEW_PATH=${PROJECT_SOURCE_DIR}/tests/data/surface-view-0.23.txt"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunGrazingSurface.cmake")
    set_tests_properties(native_grazing_sun_receivers PROPERTIES FIXTURES_SETUP grazing_captures TIMEOUT 210)
    add_test(NAME native_grazing_sun_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_grazing_surface.py" "${CMAKE_BINARY_DIR}/grazing-surface")
    set_tests_properties(native_grazing_sun_images PROPERTIES FIXTURES_REQUIRED grazing_captures)
    add_test(NAME native_terrain_cast_shadows COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/terrain-cast-shadow"
      "-DVIEW_PATH=${PROJECT_SOURCE_DIR}/tests/data/terrain-shadow-view-0.23.txt" "-DWIDTH=1440" "-DHEIGHT=900"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunGrazingSurface.cmake")
    set_tests_properties(native_terrain_cast_shadows PROPERTIES FIXTURES_SETUP terrain_shadow_captures TIMEOUT 210)
    add_test(NAME native_terrain_cast_shadow_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_cast_shadow.py" "${CMAKE_BINARY_DIR}/terrain-cast-shadow")
    set_tests_properties(native_terrain_cast_shadow_images PROPERTIES FIXTURES_REQUIRED terrain_shadow_captures)
    add_test(NAME native_piloting_loop COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/pilot-native"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunPilot.cmake")
    set_tests_properties(native_piloting_loop PROPERTIES FIXTURES_SETUP cockpit_captures TIMEOUT 240)
    add_test(NAME native_cockpit_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_cockpit_captures.py" "${CMAKE_BINARY_DIR}/pilot-native/captures")
    set_tests_properties(native_cockpit_images PROPERTIES FIXTURES_REQUIRED cockpit_captures)
    add_test(NAME native_boundary_streaming COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/streaming-native"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunStreaming.cmake")
    set_tests_properties(native_boundary_streaming PROPERTIES FIXTURES_SETUP streaming_captures TIMEOUT 150)
    add_test(NAME native_boundary_pixels COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_streaming_captures.py" "${CMAKE_BINARY_DIR}/streaming-native/captures")
    set_tests_properties(native_boundary_pixels PROPERTIES FIXTURES_REQUIRED streaming_captures)
    add_test(NAME native_seamless_scale_flight COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/seamless-flight"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunFlightTour.cmake")
    set_tests_properties(native_seamless_scale_flight PROPERTIES TIMEOUT 150)
    add_test(NAME native_cross_scale_round_trip COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/native-continuity"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunNativeTour.cmake")
    set_tests_properties(native_cross_scale_round_trip PROPERTIES FIXTURES_SETUP native_captures TIMEOUT 150)
    add_test(NAME native_atomic_visibility COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_continuity_captures.py" "${CMAKE_BINARY_DIR}/native-continuity/captures"
      "${CMAKE_BINARY_DIR}/native-continuity/native.log")
    set_tests_properties(native_atomic_visibility PROPERTIES FIXTURES_REQUIRED native_captures)
    add_test(NAME native_hyperion_round_trip COMMAND "${CMAKE_COMMAND}"
      "-DINFINITE_EXE=$<TARGET_FILE:infinite>" "-DOUTPUT=${CMAKE_BINARY_DIR}/hyperion-presentation"
      -P "${PROJECT_SOURCE_DIR}/cmake/RunPresentationTour.cmake")
    set_tests_properties(native_hyperion_round_trip PROPERTIES FIXTURES_SETUP hyperion_captures TIMEOUT 180)
    add_test(NAME native_hyperion_images COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/check_presentation_captures.py" "${CMAKE_BINARY_DIR}/hyperion-presentation/captures"
      "${CMAKE_BINARY_DIR}/hyperion-presentation/native.log")
    set_tests_properties(native_hyperion_images PROPERTIES FIXTURES_REQUIRED hyperion_captures)
  endif()
endif()
