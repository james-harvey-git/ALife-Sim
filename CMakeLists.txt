cmake_minimum_required(VERSION 3.20)
project(ALifeSim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)

add_executable(alife_sim
    src/main.cpp
    src/sim/Simulation.cpp
    src/render/Renderer.cpp
)

target_include_directories(alife_sim PRIVATE src)
target_link_libraries(alife_sim PRIVATE
    PkgConfig::SDL2
    PkgConfig::SDL2_TTF
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(alife_sim PRIVATE -Wall -Wextra -Wpedantic)
endif()

