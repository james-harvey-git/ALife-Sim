cmake_minimum_required(VERSION 3.20)
project(ALifeSim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_TESTING "Build regression tests" ON)
if(BUILD_TESTING)
    enable_testing()
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)

add_executable(alife_sim
    src/main.cpp
    src/sim/Simulation.cpp
    src/render/Renderer.cpp
    src/render/CreatureSDF.cpp
)

target_include_directories(alife_sim PRIVATE src)
target_compile_definitions(alife_sim PRIVATE ALIFE_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders")
target_link_libraries(alife_sim PRIVATE
    OpenGL::GL
    PkgConfig::SDL2
    PkgConfig::SDL2_TTF
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(alife_sim PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(APPLE)
    target_compile_definitions(alife_sim PRIVATE GL_SILENCE_DEPRECATION)
endif()

if(BUILD_TESTING)
    add_test(
        NAME alife_smoke_seed1
        COMMAND $<TARGET_FILE:alife_sim> --smoke-test --seed 1 --smoke-steps 3600
    )

    add_test(
        NAME alife_benchmark_quick
        COMMAND $<TARGET_FILE:alife_sim> --benchmark --benchmark-preset quick
    )

    add_test(
        NAME alife_round_trip_persistence
        COMMAND ${CMAKE_COMMAND}
            -DALIFE_SIM_EXECUTABLE=$<TARGET_FILE:alife_sim>
            -DSTATE_PATH=${CMAKE_BINARY_DIR}/phase0-roundtrip.bin
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/phase0_roundtrip.cmake
    )

    add_test(
        NAME alife_determinism_configured
        COMMAND ${CMAKE_COMMAND}
            -DALIFE_SIM_EXECUTABLE=$<TARGET_FILE:alife_sim>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/phase0_determinism.cmake
    )
endif()
