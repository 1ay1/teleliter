cmake_minimum_required(VERSION 3.28)
project(teleliter
    VERSION 0.1.0
    DESCRIPTION "Telegram TUI client built on maya"
    LANGUAGES CXX
)

if(MSVC)
    set(CMAKE_CXX_STANDARD 23)
else()
    set(CMAKE_CXX_STANDARD 26)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# maya — pure view layer. The submodule's PUBLIC compile features
# propagate cxx_std_26 (cxx_std_23 on MSVC) to anyone linking it.
add_subdirectory(third_party/maya EXCLUDE_FROM_ALL)

# Treat maya's includes as SYSTEM from teleliter's perspective: keeps our
# strict warning flags from triggering on maya's internal headers, without
# touching maya itself.
get_target_property(_maya_iface_incs maya INTERFACE_INCLUDE_DIRECTORIES)
if(_maya_iface_incs)
    set_target_properties(maya PROPERTIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_maya_iface_incs}")
endif()

add_executable(teleliter
    src/app/main.cpp
    src/util/stb_impl.cpp
)

target_include_directories(teleliter PRIVATE src third_party/stb)
target_link_libraries(teleliter PRIVATE maya)

# ─── TDLib integration ──────────────────────────────────────────────────────
# When ON (default), link against TDLib (the Telegram Database Library)
# and define TELELITER_HAS_TDLIB. The td/ namespace under src/ wraps
# TDLib's API behind closed-sum types (Event / Command) so the rest of
# the codebase doesn't need to see td_api::* at all. With this OFF, the
# program still builds but the runtime is a stub — useful for sandboxed
# builds where TDLib isn't installed.
option(TELELITER_USE_TDLIB "Link against TDLib for real Telegram connectivity" ON)

if(TELELITER_USE_TDLIB)
    # find_package looks at CMAKE_PREFIX_PATH and the standard system
    # locations. ~/.local is the canonical "user install" prefix; we
    # prepend it so the locally-built TDLib wins over any system pkg.
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{HOME}/.local")
    # No version constraint — TDLib's TdConfigVersion.cmake reports a
    # three-part "1.8.64" which CMake's COMPATIBLE-version logic refuses
    # to consider satisfying a two-part "1.8" request. We pin to the
    # 1.8.x series by virtue of the locally-built install only.
    find_package(Td REQUIRED)

    target_link_libraries(teleliter PRIVATE Td::TdStatic)
    target_compile_definitions(teleliter PRIVATE TELELITER_HAS_TDLIB)

    # TDLib's public headers use plenty of constructs our strict warning
    # flags fire on (old-style casts, conversions, shadows). Treat them
    # as SYSTEM so we don't drown in third-party noise.
    get_target_property(_td_iface_incs Td::TdStatic INTERFACE_INCLUDE_DIRECTORIES)
    if(_td_iface_incs)
        set_target_properties(Td::TdStatic PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_td_iface_incs}")
    endif()

    message(STATUS "teleliter: TDLib enabled (Td::TdStatic)")
else()
    message(STATUS "teleliter: TDLib disabled — building with stub runtime")
endif()

# stb_impl.cpp instantiates header-only C libraries (stb_image,
# stb_image_resize2) that trip every warning we pass on by-default
# user code. Silence the noise on just that file; the rest of the
# project keeps -Wall -Wextra -Wpedantic.
set_source_files_properties(src/util/stb_impl.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU,Clang>:-w>"
)

target_compile_options(teleliter PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:
        -Wall -Wextra -Wpedantic
        -Wconversion -Wshadow -Wnon-virtual-dtor
        -Wold-style-cast -Woverloaded-virtual
        -Wnull-dereference -Wdouble-promotion
    >
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4 /permissive- /utf-8
    >
)
