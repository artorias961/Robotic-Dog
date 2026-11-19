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

# Headers live in source/include; the code includes them as "quad/<name>.hpp".
target_include_directories(RoboticDog PRIVATE source/include)

# UDP sockets need Winsock on Windows; std::jthread needs the thread library on Linux.
if(WIN32)
    target_link_libraries(RoboticDog PRIVATE ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(RoboticDog PRIVATE Threads::Threads)
endif()

if(NOT MSVC)
    target_compile_options(RoboticDog PRIVATE -Wall -Wextra)
endif()
