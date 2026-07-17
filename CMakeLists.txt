cmake_minimum_required(VERSION 3.24)
project(PulpPhysmodInstruments LANGUAGES CXX)

# Consumes the Pulp SDK (find_package(Pulp)); the reusable DSP primitives
# (pulp::signal modal bank, bridged-T resonator, square-osc bank, modal spec)
# live in the Pulp core and are pulled in from the installed SDK. This repo is
# the instruments only.
find_package(Pulp REQUIRED)

add_subdirectory(va-drum)          # bass drum (shared voice header for the kit)
add_subdirectory(pulp-kit)         # 13-voice drum machine
add_subdirectory(modal-instrument) # mallets + strings
add_subdirectory(prepared-piano)   # struck string + collision preparation
add_subdirectory(bowed-string)     # waveguide + friction bow
add_subdirectory(gong)             # inharmonic plate + nonlinear bloom

option(PHYSMOD_BUILD_TESTS "Build instrument smoke tests" ON)
if(PHYSMOD_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1)
    FetchContent_MakeAvailable(Catch2)
    enable_testing()
    add_executable(physmod-instrument-tests test/test_instruments.cpp)
    target_include_directories(physmod-instrument-tests PRIVATE
        ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/va-drum ${CMAKE_SOURCE_DIR}/test)
    target_link_libraries(physmod-instrument-tests PRIVATE
        pulp::format pulp::signal Catch2::Catch2WithMain)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(physmod-instrument-tests)
endif()

# Diagnostic: A/B render each instrument at a low vs default Tone setting.
add_executable(ab-tone tools/ab_tone.cpp)
target_include_directories(ab-tone PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/va-drum ${CMAKE_SOURCE_DIR}/test)
target_link_libraries(ab-tone PRIVATE pulp::format pulp::signal)

add_executable(knob-sweep tools/knob_sweep.cpp)
target_include_directories(knob-sweep PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/va-drum ${CMAKE_SOURCE_DIR}/test)
target_link_libraries(knob-sweep PRIVATE pulp::format pulp::signal)
