set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED CMAKE_SYSROOT OR CMAKE_SYSROOT STREQUAL "")
    if(DEFINED ENV{RG353V_SYSROOT} AND NOT "$ENV{RG353V_SYSROOT}" STREQUAL "")
        set(CMAKE_SYSROOT "$ENV{RG353V_SYSROOT}" CACHE PATH "RG353V sysroot" FORCE)
    endif()
endif()

# Accept both repository layouts:
# - sysroot/usr/... (preferred current)
# - sysroot/rg353v/usr/... (legacy)
if(DEFINED CMAKE_SYSROOT AND NOT CMAKE_SYSROOT STREQUAL "")
    if(EXISTS "${CMAKE_SYSROOT}/rg353v/usr/include/SDL2/SDL.h")
        set(CMAKE_SYSROOT "${CMAKE_SYSROOT}/rg353v" CACHE PATH "RG353V sysroot" FORCE)
    endif()
endif()

set(CMAKE_C_COMPILER "zig")
set(CMAKE_C_COMPILER_ARG1 "cc")

set(CMAKE_CXX_COMPILER "zig")
set(CMAKE_CXX_COMPILER_ARG1 "c++")

set(RG353V_GLIBC_VERSION "2.38" CACHE STRING "Target glibc version for Zig cross-compile")
set(_ZIG_TARGET_FLAGS "--target=aarch64-linux-gnu.${RG353V_GLIBC_VERSION}")
set(_SYSROOT_INCLUDE_FLAGS "")
if(DEFINED CMAKE_SYSROOT AND NOT CMAKE_SYSROOT STREQUAL "")
    string(APPEND _ZIG_TARGET_FLAGS " --sysroot=${CMAKE_SYSROOT}")
    string(APPEND _SYSROOT_INCLUDE_FLAGS
        " -isystem ${CMAKE_SYSROOT}/usr/include"
        " -isystem ${CMAKE_SYSROOT}/usr/include/aarch64-linux-gnu"
        " -isystem ${CMAKE_SYSROOT}/usr/include/libdrm")
endif()

set(CMAKE_C_FLAGS_INIT "${_ZIG_TARGET_FLAGS}${_SYSROOT_INCLUDE_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${_ZIG_TARGET_FLAGS}${_SYSROOT_INCLUDE_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_ZIG_TARGET_FLAGS}")

# Make find_library/find_path/find_package resolve in target sysroot.
if(DEFINED CMAKE_SYSROOT AND NOT CMAKE_SYSROOT STREQUAL "")
    list(APPEND CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
    list(APPEND CMAKE_LIBRARY_PATH
        "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu"
        "${CMAKE_SYSROOT}/lib/aarch64-linux-gnu"
        "${CMAKE_SYSROOT}/usr/lib"
        "${CMAKE_SYSROOT}/lib")
    list(APPEND CMAKE_INCLUDE_PATH
        "${CMAKE_SYSROOT}/usr/include"
        "${CMAKE_SYSROOT}/usr/include/aarch64-linux-gnu"
        "${CMAKE_SYSROOT}/usr/include/libdrm")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Optional: if you have a target sysroot copied from device/SDK, pass it at
# configure time with:
#   -DCMAKE_SYSROOT=/path/to/aarch64-sysroot
