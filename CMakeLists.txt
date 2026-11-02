cmake_minimum_required(VERSION 3.21)

# Presets select the compiler, Ninja executable, and platform build directory.
# Windows: .\tools\mingw64\bin\cmake.exe --preset windows
# Linux: run bash tools/linux-x64/setup.sh once, then use the bundled CMake with --preset linux
if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(FATAL_ERROR "Use cmake --preset windows (Windows) or cmake --preset linux (Linux).")
endif()

project(RoboticDog LANGUAGES CXX)

# Require standard C++20 for all project targets, without compiler extensions.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep application code in source/ and list additional source files here.
add_executable(RoboticDog
    source/main.cpp
)
