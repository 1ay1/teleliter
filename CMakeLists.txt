cmake_minimum_required(VERSION 3.10)
project(teleliter)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find wxWidgets
find_package(wxWidgets REQUIRED COMPONENTS core base richtext media)
include(${wxWidgets_USE_FILE})

# Find TDLib - prefer locally built version in ~/.local
set(LOCAL_TDLIB_PATH "$ENV{HOME}/.local")
if(EXISTS "${LOCAL_TDLIB_PATH}/include/td/telegram/Client.h")
    set(TDLIB_ROOT "${LOCAL_TDLIB_PATH}")
    message(STATUS "Found TDLib in ~/.local: ${TDLIB_ROOT}")
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

# Find libvpx for VP9 video decoding (WebM video stickers)
set(VPX_FOUND FALSE)

# Try to find manually first with full path
find_path(VPX_INCLUDE_DIRS vpx/vpx_decoder.h
    PATHS /usr/include /usr/local/include /opt/homebrew/include /opt/local/include
)
find_library(VPX_LIBRARY NAMES vpx libvpx
    PATHS /usr/lib /usr/local/lib /opt/homebrew/lib /opt/local/lib
)
if(VPX_INCLUDE_DIRS AND VPX_LIBRARY)
    set(VPX_FOUND TRUE)
    set(VPX_LIBRARIES ${VPX_LIBRARY})
    # Get the library directory for link_directories
    get_filename_component(VPX_LIBRARY_DIR ${VPX_LIBRARY} DIRECTORY)
endif()

if(VPX_FOUND)
    message(STATUS "Found libvpx: ${VPX_LIBRARIES}")
    message(STATUS "libvpx library dir: ${VPX_LIBRARY_DIR}")
    add_definitions(-DHAVE_VPX)
else()
    message(WARNING "libvpx not found - WebM video stickers will show static thumbnails. Install with: brew install libvpx (macOS) or apt install libvpx-dev (Linux)")
endif()

# Find libwebm for WebM container parsing
set(WEBM_FOUND FALSE)
# Check Homebrew location
set(HOMEBREW_WEBM_PATH "/opt/homebrew/opt/libwebm")
if(EXISTS "${HOMEBREW_WEBM_PATH}/include/mkvparser/mkvparser.h")
    set(WEBM_INCLUDE_DIRS "${HOMEBREW_WEBM_PATH}/include")
    set(WEBM_LIBRARY "${HOMEBREW_WEBM_PATH}/lib/libwebm.a")
    if(EXISTS "${WEBM_LIBRARY}")
        set(WEBM_FOUND TRUE)
        set(WEBM_LIBRARIES ${WEBM_LIBRARY})
        message(STATUS "Found libwebm in Homebrew: ${WEBM_LIBRARY}")
    endif()
endif()

if(NOT WEBM_FOUND)
    find_path(WEBM_INCLUDE_DIRS mkvparser/mkvparser.h
        PATHS /usr/include /usr/local/include /opt/homebrew/include
    )
    find_library(WEBM_LIBRARY webm
        PATHS /usr/lib /usr/local/lib /opt/homebrew/lib
    )
    if(WEBM_INCLUDE_DIRS AND WEBM_LIBRARY)
        set(WEBM_FOUND TRUE)
        set(WEBM_LIBRARIES ${WEBM_LIBRARY})
    endif()
endif()

if(WEBM_FOUND)
    message(STATUS "Found libwebm: ${WEBM_LIBRARIES}")
    add_definitions(-DHAVE_WEBM)
else()
    message(WARNING "libwebm not found - WebM video stickers will show static thumbnails. Install with: brew install libwebm (macOS)")
endif()

# Find rlottie for animated sticker (.tgs) support
set(RLOTTIE_FOUND FALSE)
# Check in ~/.local first
set(LOCAL_RLOTTIE_PATH "$ENV{HOME}/.local")
if(EXISTS "${LOCAL_RLOTTIE_PATH}/include/rlottie/rlottie.h")
    set(RLOTTIE_INCLUDE_DIRS "${LOCAL_RLOTTIE_PATH}/include/rlottie")
    set(RLOTTIE_LIBRARY_DIR "${LOCAL_RLOTTIE_PATH}/lib")
    find_library(RLOTTIE_LIBRARY rlottie PATHS ${RLOTTIE_LIBRARY_DIR} NO_DEFAULT_PATH)
    if(RLOTTIE_LIBRARY)
        set(RLOTTIE_FOUND TRUE)
        set(RLOTTIE_LIBRARIES ${RLOTTIE_LIBRARY})
        message(STATUS "Found rlottie in ~/.local: ${RLOTTIE_LIBRARY}")
    endif()
endif()

# Try pkg-config as fallback
if(NOT RLOTTIE_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(RLOTTIE rlottie)
endif()

# Try to find manually
if(NOT RLOTTIE_FOUND)
    find_path(RLOTTIE_INCLUDE_DIRS rlottie.h
        PATHS /usr/include /usr/local/include /opt/homebrew/include
        PATH_SUFFIXES rlottie
    )
    find_library(RLOTTIE_LIBRARY rlottie
        PATHS /usr/lib /usr/local/lib /opt/homebrew/lib
    )
    if(RLOTTIE_INCLUDE_DIRS AND RLOTTIE_LIBRARY)
        set(RLOTTIE_FOUND TRUE)
        set(RLOTTIE_LIBRARIES ${RLOTTIE_LIBRARY})
    endif()
endif()

if(RLOTTIE_FOUND)
    message(STATUS "Found rlottie: ${RLOTTIE_LIBRARIES}")
    add_definitions(-DHAVE_RLOTTIE)
else()
    message(WARNING "rlottie not found - Animated stickers (.tgs) will show static thumbnails. Build rlottie from: https://github.com/Samsung/rlottie")
endif()

# Source files
set(SOURCES
    src/ui/App.cpp
    src/ui/MainFrame.cpp
    src/ui/MediaPopup.cpp
    src/ui/FileDropTarget.cpp
    src/ui/FileUtils.cpp
    src/ui/MessageFormatter.cpp
    src/ui/WelcomeChat.cpp
    src/ui/ChatListWidget.cpp
    src/ui/ChatViewWidget.cpp
    src/ui/InputBoxWidget.cpp
    src/ui/StatusBarManager.cpp
    src/ui/LottiePlayer.cpp
    src/ui/WebmPlayer.cpp
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
    ${RLOTTIE_INCLUDE_DIRS}
    ${VPX_INCLUDE_DIRS}
    ${WEBM_INCLUDE_DIRS}
)

# Link libraries - order matters for static linking
target_link_libraries(teleliter
    ${wxWidgets_LIBRARIES}
    ${TDLIB_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    ${WEBP_LIBRARIES}
    ${RLOTTIE_LIBRARIES}
    ${VPX_LIBRARIES}
    ${WEBM_LIBRARIES}
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
