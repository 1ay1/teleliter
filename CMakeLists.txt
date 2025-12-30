cmake_minimum_required(VERSION 3.10)
project(teleliter)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find wxWidgets
find_package(wxWidgets REQUIRED COMPONENTS core base richtext media stc)
include(${wxWidgets_USE_FILE})

# Find TDLib - prefer locally built version in ~/.local
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

# Set up TDLib paths
if(TDLIB_ROOT)
    set(TDLIB_INCLUDE_DIRS "${TDLIB_ROOT}/include")
    set(TDLIB_LIBRARY_DIR "${TDLIB_ROOT}/lib")

    # Find all TDLib static libraries for C++ interface
    # Order matters for static linking - dependencies must come after dependents
    # Check for newer TDLib structure (has libtdmtproto, libtde2e)
    if(EXISTS "${TDLIB_LIBRARY_DIR}/libtdmtproto.a")
        # Newer TDLib (1.8.x+)
        set(TDLIB_STATIC_LIBS
            "${TDLIB_LIBRARY_DIR}/libtdclient.a"
            "${TDLIB_LIBRARY_DIR}/libtdcore.a"
            "${TDLIB_LIBRARY_DIR}/libtdapi.a"
            "${TDLIB_LIBRARY_DIR}/libtdmtproto.a"
            "${TDLIB_LIBRARY_DIR}/libtddb.a"
            "${TDLIB_LIBRARY_DIR}/libtdsqlite.a"
            "${TDLIB_LIBRARY_DIR}/libtdnet.a"
            "${TDLIB_LIBRARY_DIR}/libtde2e.a"
            "${TDLIB_LIBRARY_DIR}/libtdactor.a"
            "${TDLIB_LIBRARY_DIR}/libtdutils.a"
        )
    else()
        # Older TDLib
        set(TDLIB_STATIC_LIBS
            "${TDLIB_LIBRARY_DIR}/libtdclient.a"
            "${TDLIB_LIBRARY_DIR}/libtdcore.a"
            "${TDLIB_LIBRARY_DIR}/libtdapi.a"
            "${TDLIB_LIBRARY_DIR}/libtddb.a"
            "${TDLIB_LIBRARY_DIR}/libtdsqlite.a"
            "${TDLIB_LIBRARY_DIR}/libtdnet.a"
            "${TDLIB_LIBRARY_DIR}/libtdactor.a"
            "${TDLIB_LIBRARY_DIR}/libtdutils.a"
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

if(APPLE)
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
        find_library(AVUTIL_LIBRARY avutil PATHS "${FFMPEG_PREFIX}/lib" NO_DEFAULT_PATH)

        if(AVFORMAT_LIBRARY AND AVCODEC_LIBRARY AND SWSCALE_LIBRARY AND AVUTIL_LIBRARY)
            set(FFMPEG_FOUND TRUE)
            set(FFMPEG_LIBRARIES ${AVFORMAT_LIBRARY} ${AVCODEC_LIBRARY} ${SWSCALE_LIBRARY} ${AVUTIL_LIBRARY})
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
        pkg_check_modules(FFMPEG libavformat libavcodec libswscale libavutil)
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
        find_library(AVUTIL_LIBRARY avutil
            PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
        )

        if(AVFORMAT_INCLUDE_DIR AND AVFORMAT_LIBRARY AND AVCODEC_LIBRARY AND SWSCALE_LIBRARY AND AVUTIL_LIBRARY)
            set(FFMPEG_FOUND TRUE)
            set(FFMPEG_INCLUDE_DIRS ${AVFORMAT_INCLUDE_DIR})
            set(FFMPEG_LIBRARIES ${AVFORMAT_LIBRARY} ${AVCODEC_LIBRARY} ${SWSCALE_LIBRARY} ${AVUTIL_LIBRARY})
            message(STATUS "Found FFmpeg (manual): ${FFMPEG_LIBRARIES}")
            add_definitions(-DHAVE_FFMPEG)
        endif()
    endif()

    if(NOT FFMPEG_FOUND)
        message(FATAL_ERROR "FFmpeg not found - FFmpeg is required for media playback. Install with: apt install libavformat-dev libavcodec-dev libswscale-dev libavutil-dev")
    endif()
endif()

# Source files
set(SOURCES
    src/ui/App.cpp
    src/ui/MainFrame.cpp
    src/ui/MediaPopup.cpp
    src/ui/FileDropTarget.cpp
    src/ui/FileUtils.cpp
    src/ui/ChatArea.cpp
    src/ui/MessageFormatter.cpp
    src/ui/StatusBarManager.cpp
    src/ui/WelcomeChat.cpp
    src/ui/ChatListWidget.cpp
    src/ui/ChatViewWidget.cpp
    src/ui/InputBoxWidget.cpp
    src/ui/FFmpegPlayer.cpp
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
)

# Link libraries - order matters for static linking
target_link_libraries(teleliter
    ${wxWidgets_LIBRARIES}
    ${TDLIB_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    ${WEBP_LIBRARIES}
    ${FFMPEG_LIBRARIES}
    m
)

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

# Enable warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(teleliter PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
