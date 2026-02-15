cmake_minimum_required(VERSION 3.15)
project(rg353v-demo C)

set(CMAKE_C_STANDARD 99)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(TARGET_RG353V "Build for RG353V handheld (Linux ARM64)" OFF)

# Keep raylib lean inside this app project.
set(BUILD_EXAMPLES OFF CACHE BOOL "Build raylib examples" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

if(TARGET_RG353V)
    # RG353V portable setup: SDL + GLES2 is the most reliable runtime path.
    set(PLATFORM SDL CACHE STRING "raylib platform backend" FORCE)
    set(OPENGL_VERSION "ES 2.0" CACHE STRING "Use OpenGL ES 2.0 on RG353V" FORCE)

    if(NOT DEFINED CMAKE_SYSROOT OR CMAKE_SYSROOT STREQUAL "")
        message(FATAL_ERROR
            "CMAKE_SYSROOT is not set for RG353V cross-build.\n"
            "Recommended permanent setup:\n"
            "  export RG353V_SYSROOT=/absolute/path/to/sysroot/rg353v\n"
            "  cmake --preset rg353v-final")
    endif()

    if(NOT EXISTS "${CMAKE_SYSROOT}/usr/include/SDL2/SDL.h")
        message(FATAL_ERROR
            "Missing SDL2 headers in sysroot: ${CMAKE_SYSROOT}/usr/include/SDL2/SDL.h\n"
            "Resync sysroot or install SDL2 development headers on device.")
    endif()

    # Point find_package(SDL2) to the sysroot config package.
    if(EXISTS "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/cmake/SDL2/SDL2Config.cmake")
        set(SDL2_DIR "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/cmake/SDL2" CACHE PATH "RG353V SDL2 config dir" FORCE)
    elseif(EXISTS "${CMAKE_SYSROOT}/lib/aarch64-linux-gnu/cmake/SDL2/SDL2Config.cmake")
        set(SDL2_DIR "${CMAKE_SYSROOT}/lib/aarch64-linux-gnu/cmake/SDL2" CACHE PATH "RG353V SDL2 config dir" FORCE)
    else()
        message(FATAL_ERROR
            "Missing SDL2Config.cmake in sysroot.\n"
            "Expected in:\n"
            "  ${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/cmake/SDL2\n"
            "or\n"
            "  ${CMAKE_SYSROOT}/lib/aarch64-linux-gnu/cmake/SDL2")
    endif()
endif()

add_subdirectory(raylib)

add_executable(rg353v-demo main.c)
target_link_libraries(rg353v-demo PRIVATE raylib)
