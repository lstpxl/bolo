cmake_minimum_required(VERSION 3.15)
project(bolo C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
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

add_executable(
    bolo
    src/app/main.cpp
    src/app/AppConfig.cpp
    src/app/GameApp.cpp
    src/core/Random.cpp
    src/core/Time.cpp
    src/game/GameModeController.cpp
    src/game/systems/CollisionSystem.cpp
    src/game/systems/EnemySystem.cpp
    src/game/systems/MazeSystem.cpp
    src/game/systems/PlayerSystem.cpp
    src/game/systems/ProjectileSystem.cpp
    src/game/systems/SpawnerSystem.cpp
    src/platform/Input.cpp
    src/platform/Renderer2D.cpp
    src/ui/HudPanel.cpp
    src/ui/MenuScreen.cpp
    src/ui/RayguiContext.cpp
)

target_include_directories(
    bolo
    PRIVATE
    src
    third_party/raygui/src
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(bolo PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(bolo PRIVATE raylib)
