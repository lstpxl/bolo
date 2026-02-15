find_path(SDL2_INCLUDE_DIR
    NAMES SDL.h
    PATH_SUFFIXES SDL2
    PATHS
      "${CMAKE_SYSROOT}/usr/include"
      "${CMAKE_SYSROOT}/include"
      "/usr/include"
      "/usr/local/include")

set(_SDL2_LIB_SEARCH_DIRS
    "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu"
    "${CMAKE_SYSROOT}/lib/aarch64-linux-gnu"
    "${CMAKE_SYSROOT}/usr/lib"
    "${CMAKE_SYSROOT}/lib"
    "/usr/lib"
    "/usr/local/lib")

find_library(SDL2_LIBRARY
    NAMES SDL2 SDL2-2.0
    PATHS ${_SDL2_LIB_SEARCH_DIRS})

# Prefer a real shared object when sysroot symlinks are absolute and broken.
set(_SDL2_SHARED_LIBRARY "")
foreach(_dir IN LISTS _SDL2_LIB_SEARCH_DIRS)
    file(GLOB _sdl_so_candidates
        "${_dir}/libSDL2.so"
        "${_dir}/libSDL2-2.0.so"
        "${_dir}/libSDL2.so.*"
        "${_dir}/libSDL2-2.0.so.*")
    foreach(_cand IN LISTS _sdl_so_candidates)
        if(EXISTS "${_cand}")
            set(_SDL2_SHARED_LIBRARY "${_cand}")
            break()
        endif()
    endforeach()
    if(NOT _SDL2_SHARED_LIBRARY STREQUAL "")
        break()
    endif()
endforeach()

if(NOT _SDL2_SHARED_LIBRARY STREQUAL "")
    set(SDL2_LIBRARY "${_SDL2_SHARED_LIBRARY}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL2
    REQUIRED_VARS SDL2_LIBRARY SDL2_INCLUDE_DIR)

if(SDL2_FOUND AND NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 UNKNOWN IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
        IMPORTED_LOCATION "${SDL2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}")
endif()
