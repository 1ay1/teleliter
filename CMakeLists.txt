cmake_minimum_required(VERSION 3.10)
project(teleliter)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find wxWidgets
find_package(wxWidgets REQUIRED COMPONENTS core base richtext)
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

# Source files
set(SOURCES
    src/ui/App.cpp
    src/ui/MainFrame.cpp
    src/ui/MediaPopup.cpp
    src/ui/FileDropTarget.cpp
    src/ui/FileUtils.cpp
    src/ui/MessageFormatter.cpp
    src/ui/WelcomeChat.cpp
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
)

# Link libraries - order matters for static linking
target_link_libraries(teleliter
    ${wxWidgets_LIBRARIES}
    ${TDLIB_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
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
