cmake_minimum_required(VERSION 3.15)
project(bolt C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands for clang tooling" FORCE)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(CMAKE_EXPORT_COMPILE_COMMANDS)
    set(BOLT_COMPILE_COMMANDS_SOURCE "${CMAKE_BINARY_DIR}/compile_commands.json")
    set(BOLT_COMPILE_COMMANDS_TARGET "${CMAKE_SOURCE_DIR}/compile_commands.json")
    if(EXISTS "${BOLT_COMPILE_COMMANDS_TARGET}" OR IS_SYMLINK "${BOLT_COMPILE_COMMANDS_TARGET}")
        file(REMOVE "${BOLT_COMPILE_COMMANDS_TARGET}")
    endif()
    file(CREATE_LINK "${BOLT_COMPILE_COMMANDS_SOURCE}" "${BOLT_COMPILE_COMMANDS_TARGET}" SYMBOLIC COPY_ON_ERROR)
    message(STATUS "compile_commands.json synced at ${BOLT_COMPILE_COMMANDS_TARGET}")
endif()

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
            "  cmake --preset rg353v-release")
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

add_subdirectory(third_party/raylib)
add_subdirectory(third_party/spdlog)

set(BOLT_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(BOLT_BUILD_NUMBER_FILE "${CMAKE_SOURCE_DIR}/build/build_number.txt")
set(BOLT_BUILD_NUMBER_HEADER "${BOLT_GENERATED_DIR}/BuildNumber.h")

add_custom_target(
    bolt_build_number ALL
    COMMAND ${CMAKE_COMMAND}
        -DBUILD_NUMBER_FILE=${BOLT_BUILD_NUMBER_FILE}
        -DOUTPUT_HEADER=${BOLT_BUILD_NUMBER_HEADER}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateBuildNumber.cmake
    BYPRODUCTS ${BOLT_BUILD_NUMBER_HEADER} ${BOLT_BUILD_NUMBER_FILE}
    COMMENT "Generating auto-incremented build number"
)

add_executable(
    bolt
    src/app/main.cpp
    src/app/AppConfig.cpp
    src/app/AudioEventRouter.cpp
    src/app/BuildInfo.cpp
    src/app/DebugOverlayRenderer.cpp
    src/app/GameApp.cpp
    src/app/MenuMusicGenerator.cpp
    src/core/Log.cpp
    src/core/ResourceLocator.cpp
    src/core/Profiling.cpp
    src/core/Random.cpp
    src/core/Time.cpp
    src/game/GameModeController.cpp
    src/game/Game.cpp
    src/game/geometry/WorldGeometry.cpp
    src/game/navigation/AdjacentCellSegmentPlanner.cpp
    src/game/navigation/CellCoordCache.cpp
    src/game/navigation/PlayerFlowField.cpp
    src/game/spatial/EnemyCellOccupancy.cpp
    src/game/spatial/SweepPruneBroadPhase.cpp
    src/game/systems/CollisionSystem.cpp
    src/game/systems/EnemyAssassin.cpp
    src/game/systems/EnemyDrone.cpp
    src/game/systems/EnemyHunter.cpp
    src/game/systems/EnemySystem.cpp
    src/game/systems/EnemySystemCheapTier.cpp
    src/game/systems/EnemySystemCollision.cpp
    src/game/systems/EnemySystemCombatPhase.cpp
    src/game/systems/EnemySystemHelpers.cpp
    src/game/systems/EnemySystemPathfinding.cpp
    src/game/systems/EnemySystemTelemetry.cpp
    src/game/systems/EnemySystemUncouple.cpp
    src/game/systems/EnemyTorpedo.cpp
    src/game/systems/MazeSystem.cpp
    src/game/systems/PlayerSystem.cpp
    src/game/systems/ProjectileSystem.cpp
    src/game/systems/SpawnerSystem.cpp
    src/platform/Input.cpp
    src/platform/RaylibRenderer.cpp
    src/platform/Renderer2D.cpp
    src/ui/ConfirmationDialog.cpp
    src/ui/HudPanel.cpp
    src/ui/MenuScreen.cpp
    src/ui/UiPrimitives.cpp
    src/ui/RayguiContext.cpp
)

target_include_directories(
    bolt
    PRIVATE
    src
    ${BOLT_GENERATED_DIR}
    third_party/raygui/src
)

add_dependencies(bolt bolt_build_number)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(bolt PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(bolt PRIVATE raylib spdlog::spdlog)
