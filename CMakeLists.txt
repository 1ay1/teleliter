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
