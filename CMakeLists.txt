cmake_minimum_required(VERSION 3.10)

# Read version from central VERSION file
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PROJECT_VERSION)
string(STRIP "${PROJECT_VERSION}" PROJECT_VERSION)

project(teleliter VERSION ${PROJECT_VERSION})

message(STATUS "Building teleliter version ${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option for static linking on Linux
option(STATIC_LINK "Link dependencies statically on Linux" OFF)

# Find wxWidgets
find_package(wxWidgets REQUIRED COMPONENTS core base richtext media stc)
include(${wxWidgets_USE_FILE})

# Find TDLib - check if TDLIB_ROOT was passed via -DTDLIB_ROOT=, otherwise auto-detect
if(TDLIB_ROOT)
    message(STATUS "Using TDLib from specified TDLIB_ROOT: ${TDLIB_ROOT}")
else()
    # Auto-detect TDLib location - prefer locally built version in ~/.local
    set(LOCAL_TDLIB_PATH "$ENV{HOME}/.local")
    if(EXISTS "${LOCAL_TDLIB_PATH}/include/td/telegram/Client.h")
        set(TDLIB_ROOT "${LOCAL_TDLIB_PATH}")
        message(STATUS "Found TDLib in ~/.local: ${TDLIB_ROOT}")
    elseif(EXISTS "/usr/include/td/telegram/Client.h")
        set(TDLIB_ROOT "/usr")
        message(STATUS "Found TDLib in /usr: ${TDLIB_ROOT}")
    elseif(APPLE)
        # Fall back to Homebrew on macOS
        execute_process(
            COMMAND brew --prefix tdlib
            OUTPUT_VARIABLE HOMEBREW_TDLIB_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(HOMEBREW_TDLIB_PREFIX)
            set(TDLIB_ROOT "${HOMEBREW_TDLIB_PREFIX}")
            message(STATUS "Found TDLib via Homebrew: ${TDLIB_ROOT}")
        endif()
    endif()
endif()

# Set up TDLib paths
if(TDLIB_ROOT)
    set(TDLIB_INCLUDE_DIRS "${TDLIB_ROOT}/include")
    set(TDLIB_LIBRARY_DIR "${TDLIB_ROOT}/lib")

    # Determine library prefix and suffix based on platform
    if(WIN32)
        set(TDLIB_LIB_PREFIX "")
        set(TDLIB_LIB_SUFFIX ".lib")
    else()
        set(TDLIB_LIB_PREFIX "lib")
        set(TDLIB_LIB_SUFFIX ".a")
    endif()

    # Find all TDLib static libraries for C++ interface
    # Order matters for static linking - dependencies must come after dependents
    # Check for newer TDLib structure (has tdmtproto, tde2e)
    if(EXISTS "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tdmtproto${TDLIB_LIB_SUFFIX}")
        # Newer TDLib (1.8.x+)
        set(TDLIB_STATIC_LIBS
            "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tdclient${TDLIB_LIB_SUFFIX}"
            "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tdcore${TDLIB_LIB_SUFFIX}"
            "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tdapi${TDLIB_LIB_SUFFIX}"
            "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tdmtproto${TDLIB_LIB_SUFFIX}"
            "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tddb${TDLIB_LIB_SUFFIX}"
            "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tdsqlite${TDLIB_LIB_SUFFIX}"
            "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tdnet${TDLIB_LIB_SUFFIX}"
            "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tde2e${TDLIB_LIB_SUFFIX}"
            "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tdactor${TDLIB_LIB_SUFFIX}"
            "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tdutils${TDLIB_LIB_SUFFIX}"
        )
    else()
        # Older TDLib
        set(TDLIB_STATIC_LIBS
            "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tdclient${TDLIB_LIB_SUFFIX}"
            "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tdcore${TDLIB_LIB_SUFFIX}"
            "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tdapi${TDLIB_LIB_SUFFIX}"
            "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tddb${TDLIB_LIB_SUFFIX}"
            "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tdsqlite${TDLIB_LIB_SUFFIX}"
            "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tdnet${TDLIB_LIB_SUFFIX}"
            "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tdactor${TDLIB_LIB_SUFFIX}"
            "${TDLIB_LIBRARY_DIR}/${TDLIB_LIB_PREFIX}tdutils${TDLIB_LIB_SUFFIX}"
        )
    endif()

    # Check that libraries exist
    set(TDLIB_FOUND TRUE)
    foreach(lib ${TDLIB_STATIC_LIBS})
        if(NOT EXISTS "${lib}")
            message(WARNING "TDLib library not found: ${lib}")
            set(TDLIB_FOUND FALSE)
        endif()
    endforeach()

    if(TDLIB_FOUND)
        set(TDLIB_LIBRARIES ${TDLIB_STATIC_LIBS})
        message(STATUS "Found TDLib static libraries")
    endif()
else()
    # Try to find TDLib in standard locations
    find_path(TDLIB_INCLUDE_DIRS
        NAMES td/telegram/Client.h
        PATHS
            /usr/local/include
            /usr/include
            /opt/homebrew/include
            $ENV{HOME}/.local/include
    )

    if(TDLIB_INCLUDE_DIRS)
        get_filename_component(TDLIB_ROOT "${TDLIB_INCLUDE_DIRS}" DIRECTORY)
        set(TDLIB_LIBRARY_DIR "${TDLIB_ROOT}/lib")
    endif()
endif()

if(NOT TDLIB_FOUND)
    message(FATAL_ERROR "TDLib not found. Please install TDLib:

On macOS with Homebrew:
    brew install tdlib

On Linux, build from source:
    https://github.com/tdlib/td#building

Or specify path:
    cmake -DTDLIB_ROOT=/path/to/tdlib ..")
endif()

message(STATUS "TDLib include: ${TDLIB_INCLUDE_DIRS}")
message(STATUS "TDLib libraries: ${TDLIB_LIBRARIES}")

# Find required system libraries for TDLib
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

# Find libwebp for WebP image support (Telegram stickers)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(WEBP libwebp)
endif()

if(NOT WEBP_FOUND)
    # Try to find manually
    find_path(WEBP_INCLUDE_DIRS webp/decode.h
        PATHS /usr/include /usr/local/include /opt/homebrew/include
    )
    find_library(WEBP_LIBRARY webp
        PATHS /usr/lib /usr/local/lib /opt/homebrew/lib
    )
    if(WEBP_INCLUDE_DIRS AND WEBP_LIBRARY)
        set(WEBP_FOUND TRUE)
        set(WEBP_LIBRARIES ${WEBP_LIBRARY})
    endif()
endif()

if(WEBP_FOUND)
    message(STATUS "Found libwebp: ${WEBP_LIBRARIES}")
    add_definitions(-DHAVE_WEBP)
else()
    message(WARNING "libwebp not found - WebP sticker images will not be displayed. Install with: apt install libwebp-dev (Linux) or brew install webp (macOS)")
endif()

# Find FFmpeg libraries for all media playback (single source of truth)
set(FFMPEG_FOUND FALSE)

if(WIN32)
    # Windows - use vcpkg
    find_path(FFMPEG_INCLUDE_DIRS libavformat/avformat.h)
    find_library(AVFORMAT_LIBRARY avformat)
    find_library(AVCODEC_LIBRARY avcodec)
    find_library(SWSCALE_LIBRARY swscale)
    find_library(SWRESAMPLE_LIBRARY swresample)
    find_library(AVUTIL_LIBRARY avutil)

    if(AVFORMAT_LIBRARY AND AVCODEC_LIBRARY AND SWSCALE_LIBRARY AND SWRESAMPLE_LIBRARY AND AVUTIL_LIBRARY)
        set(FFMPEG_FOUND TRUE)
        set(FFMPEG_LIBRARIES ${AVFORMAT_LIBRARY} ${AVCODEC_LIBRARY} ${SWSCALE_LIBRARY} ${SWRESAMPLE_LIBRARY} ${AVUTIL_LIBRARY})
        message(STATUS "Found FFmpeg libraries: ${FFMPEG_LIBRARIES}")
        add_definitions(-DHAVE_FFMPEG)
    else()
        message(WARNING "FFmpeg not found on Windows")
    endif()
elseif(APPLE)
    # Try Homebrew first on macOS
    execute_process(
        COMMAND brew --prefix ffmpeg
        OUTPUT_VARIABLE FFMPEG_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(FFMPEG_PREFIX)
        message(STATUS "Found FFmpeg in Homebrew: ${FFMPEG_PREFIX}")
        set(FFMPEG_INCLUDE_DIRS "${FFMPEG_PREFIX}/include")
        find_library(AVFORMAT_LIBRARY avformat PATHS "${FFMPEG_PREFIX}/lib" NO_DEFAULT_PATH)
        find_library(AVCODEC_LIBRARY avcodec PATHS "${FFMPEG_PREFIX}/lib" NO_DEFAULT_PATH)
        find_library(SWSCALE_LIBRARY swscale PATHS "${FFMPEG_PREFIX}/lib" NO_DEFAULT_PATH)
        find_library(SWRESAMPLE_LIBRARY swresample PATHS "${FFMPEG_PREFIX}/lib" NO_DEFAULT_PATH)
        find_library(AVUTIL_LIBRARY avutil PATHS "${FFMPEG_PREFIX}/lib" NO_DEFAULT_PATH)

        if(AVFORMAT_LIBRARY AND AVCODEC_LIBRARY AND SWSCALE_LIBRARY AND SWRESAMPLE_LIBRARY AND AVUTIL_LIBRARY)
            set(FFMPEG_FOUND TRUE)
            set(FFMPEG_LIBRARIES ${AVFORMAT_LIBRARY} ${AVCODEC_LIBRARY} ${SWSCALE_LIBRARY} ${SWRESAMPLE_LIBRARY} ${AVUTIL_LIBRARY})
            message(STATUS "Found FFmpeg libraries: ${FFMPEG_LIBRARIES}")
            add_definitions(-DHAVE_FFMPEG)
        endif()
    endif()

    if(NOT FFMPEG_FOUND)
        message(FATAL_ERROR "FFmpeg not found - FFmpeg is required for media playback. Install with: brew install ffmpeg")
    endif()
elseif(UNIX)
    # Linux
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG libavformat libavcodec libswscale libswresample libavutil)
        if(FFMPEG_FOUND)
            message(STATUS "Found FFmpeg: ${FFMPEG_LIBRARIES}")
            add_definitions(-DHAVE_FFMPEG)
        endif()
    endif()

    if(NOT FFMPEG_FOUND)
        # Try to find manually
        find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h
            PATHS /usr/include /usr/local/include
        )
        find_library(AVFORMAT_LIBRARY avformat
            PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
        )
        find_library(AVCODEC_LIBRARY avcodec
            PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
        )
        find_library(SWSCALE_LIBRARY swscale
            PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
        )
        find_library(SWRESAMPLE_LIBRARY swresample
            PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
        )
        find_library(AVUTIL_LIBRARY avutil
            PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
        )

        if(AVFORMAT_INCLUDE_DIR AND AVFORMAT_LIBRARY AND AVCODEC_LIBRARY AND SWSCALE_LIBRARY AND SWRESAMPLE_LIBRARY AND AVUTIL_LIBRARY)
            set(FFMPEG_FOUND TRUE)
            set(FFMPEG_INCLUDE_DIRS ${AVFORMAT_INCLUDE_DIR})
            set(FFMPEG_LIBRARIES ${AVFORMAT_LIBRARY} ${AVCODEC_LIBRARY} ${SWSCALE_LIBRARY} ${SWRESAMPLE_LIBRARY} ${AVUTIL_LIBRARY})
            message(STATUS "Found FFmpeg (manual): ${FFMPEG_LIBRARIES}")
            add_definitions(-DHAVE_FFMPEG)
        endif()
    endif()

    if(NOT FFMPEG_FOUND)
        message(FATAL_ERROR "FFmpeg not found - FFmpeg is required for media playback. Install with: apt install libavformat-dev libavcodec-dev libswscale-dev libswresample-dev libavutil-dev")
    endif()
endif()

# Find SDL2 for audio playback
set(SDL2_FOUND FALSE)

if(WIN32)
    # Windows - use vcpkg
    find_path(SDL2_INCLUDE_DIRS SDL.h PATH_SUFFIXES SDL2)
    find_library(SDL2_LIBRARY SDL2)
    find_library(SDL2MAIN_LIBRARY SDL2main)
    if(SDL2_INCLUDE_DIRS AND SDL2_LIBRARY)
        set(SDL2_FOUND TRUE)
        set(SDL2_LIBRARIES ${SDL2MAIN_LIBRARY} ${SDL2_LIBRARY})
        message(STATUS "Found SDL2: ${SDL2_LIBRARIES}")
        add_definitions(-DHAVE_SDL2)
    else()
        message(WARNING "SDL2 not found on Windows")
    endif()
elseif(APPLE)
    execute_process(
        COMMAND brew --prefix sdl2
        OUTPUT_VARIABLE SDL2_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(SDL2_PREFIX)
        message(STATUS "Found SDL2 in Homebrew: ${SDL2_PREFIX}")
        set(SDL2_INCLUDE_DIRS "${SDL2_PREFIX}/include/SDL2")
        find_library(SDL2_LIBRARY SDL2 PATHS "${SDL2_PREFIX}/lib" NO_DEFAULT_PATH)
        if(SDL2_LIBRARY)
            set(SDL2_FOUND TRUE)
            set(SDL2_LIBRARIES ${SDL2_LIBRARY})
        endif()
    endif()
elseif(UNIX)
    if(PkgConfig_FOUND)
        if(STATIC_LINK AND UNIX AND NOT APPLE)
            pkg_check_modules(SDL2 STATIC sdl2)
        else()
            pkg_check_modules(SDL2 sdl2)
        endif()
    endif()
    if(NOT SDL2_FOUND)
        find_path(SDL2_INCLUDE_DIRS SDL.h
            PATHS /usr/include/SDL2 /usr/local/include/SDL2
        )
        find_library(SDL2_LIBRARY SDL2
            PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
        )
        if(SDL2_INCLUDE_DIRS AND SDL2_LIBRARY)
            set(SDL2_FOUND TRUE)
            set(SDL2_LIBRARIES ${SDL2_LIBRARY})
        endif()
    endif()
endif()

if(SDL2_FOUND)
    message(STATUS "Found SDL2: ${SDL2_LIBRARIES}")
    add_definitions(-DHAVE_SDL2)
else()
    message(WARNING "SDL2 not found - audio playback will be disabled. Install with: brew install sdl2 (macOS) or apt install libsdl2-dev (Linux)")
endif()

# Find rlottie for TGS (Lottie) sticker animation support
set(RLOTTIE_FOUND FALSE)

if(PkgConfig_FOUND)
    if(STATIC_LINK AND UNIX AND NOT APPLE)
        pkg_check_modules(RLOTTIE STATIC rlottie)
    else()
        pkg_check_modules(RLOTTIE rlottie)
    endif()
endif()

if(NOT RLOTTIE_FOUND AND APPLE)
    # Try Homebrew on macOS
    execute_process(
        COMMAND brew --prefix rlottie
        OUTPUT_VARIABLE RLOTTIE_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(RLOTTIE_PREFIX)
        message(STATUS "Found rlottie in Homebrew: ${RLOTTIE_PREFIX}")
        set(RLOTTIE_INCLUDE_DIRS "${RLOTTIE_PREFIX}/include")
        find_library(RLOTTIE_LIBRARY rlottie
            PATHS "${RLOTTIE_PREFIX}/lib"
            NO_DEFAULT_PATH
        )
        if(RLOTTIE_LIBRARY)
            set(RLOTTIE_FOUND TRUE)
            set(RLOTTIE_LIBRARIES ${RLOTTIE_LIBRARY})
        endif()
    endif()
endif()

if(NOT RLOTTIE_FOUND)
    # Try to find manually - use $ENV{HOME} since ~ doesn't expand in CMake
    find_path(RLOTTIE_INCLUDE_DIRS rlottie.h
        PATHS /usr/include /usr/local/include /opt/homebrew/include "$ENV{HOME}/.local/include"
        PATH_SUFFIXES rlottie
    )
    find_library(RLOTTIE_LIBRARY rlottie
        PATHS /usr/lib /usr/local/lib /opt/homebrew/lib /usr/lib/x86_64-linux-gnu "$ENV{HOME}/.local/lib"
    )
    if(RLOTTIE_INCLUDE_DIRS AND RLOTTIE_LIBRARY)
        set(RLOTTIE_FOUND TRUE)
        set(RLOTTIE_LIBRARIES ${RLOTTIE_LIBRARY})
        message(STATUS "Found rlottie manually: includes=${RLOTTIE_INCLUDE_DIRS} libs=${RLOTTIE_LIBRARIES}")
    endif()
endif()

if(RLOTTIE_FOUND)
    message(STATUS "Found rlottie: ${RLOTTIE_LIBRARIES}")
    add_definitions(-DHAVE_RLOTTIE)
else()
    message(WARNING "rlottie not found - TGS sticker animations will not be displayed. Install from source or check https://github.com/AyushKumar-AK/rlottie-mac for macOS")
endif()

# Source files
set(SOURCES
    src/ui/App.cpp
    src/ui/MainFrame.cpp
    src/ui/MediaPopup.cpp
    src/ui/UserInfoPopup.cpp
    src/ui/FileDropTarget.cpp
    src/ui/FileUtils.cpp
    src/ui/ChatArea.cpp
    src/ui/MessageFormatter.cpp
    src/ui/StatusBarManager.cpp
    src/ui/ServiceMessageLog.cpp
    src/ui/WelcomeChat.cpp
    src/ui/ChatListWidget.cpp
    src/ui/ChatViewWidget.cpp
    src/ui/InputBoxWidget.cpp
    src/ui/FFmpegPlayer.cpp
    src/ui/LottiePlayer.cpp
    src/telegram/TransferManager.cpp
    src/telegram/TelegramClient.cpp
    src/main.cpp
)

# Create executable
add_executable(teleliter ${SOURCES})

# Include directories
target_include_directories(teleliter PRIVATE
    ${TDLIB_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${WEBP_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
)

if(RLOTTIE_FOUND)
    target_include_directories(teleliter PRIVATE ${RLOTTIE_INCLUDE_DIRS})
endif()

# Link libraries - order matters for static linking
if(STATIC_LINK AND UNIX AND NOT APPLE)
    # For static linking, we need to link whole archives and resolve all dependencies
    target_link_libraries(teleliter
        ${wxWidgets_LIBRARIES}
        ${TDLIB_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
        ZLIB::ZLIB
        ${WEBP_LIBRARIES}
        ${FFMPEG_LIBRARIES}
        ${SDL2_LIBRARIES}
        # Additional dependencies for static FFmpeg
        -lx264 -lx265 -lvpx -lmp3lame -lopus -lvorbis -lvorbisenc -logg
        -lva -lva-drm -lva-x11 -ldrm
        -lXext -lX11 -lxcb -lXau -lXdmcp
        -lbz2 -llzma
        m
    )
else()
    target_link_libraries(teleliter
        ${wxWidgets_LIBRARIES}
        ${TDLIB_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
        ZLIB::ZLIB
        ${WEBP_LIBRARIES}
        ${FFMPEG_LIBRARIES}
        ${SDL2_LIBRARIES}
    )
    # Link math library only on Unix (not needed on Windows)
    if(UNIX)
        target_link_libraries(teleliter m)
    endif()
endif()

if(RLOTTIE_FOUND)
    target_link_libraries(teleliter ${RLOTTIE_LIBRARIES})
endif()

# On macOS, link required system frameworks
if(APPLE)
    find_library(SECURITY_FRAMEWORK Security)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(IOKIT_FRAMEWORK IOKit)
    target_link_libraries(teleliter
        ${SECURITY_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
        ${COCOA_FRAMEWORK}
        ${IOKIT_FRAMEWORK}
        "-framework SystemConfiguration"
    )
endif()

# On Linux, link additional libraries
if(UNIX AND NOT APPLE)
    target_link_libraries(teleliter
        pthread
        dl
    )
endif()

# On Windows, link required system libraries
if(WIN32)
    target_link_libraries(teleliter
        Crypt32
        Ws2_32
        Normaliz
        Psapi
        Shlwapi
        Bcrypt
        Mswsock
        Userenv
    )
    # Set Windows subsystem to avoid console window
    set_target_properties(teleliter PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Enable warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(teleliter PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
