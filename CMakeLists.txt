cmake_minimum_required(VERSION 3.10)
project(teleliter)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(wxWidgets REQUIRED COMPONENTS core base richtext)
include(${wxWidgets_USE_FILE})

add_executable(teleliter
    src/App.cpp
    src/MainFrame.cpp
    src/MediaPopup.cpp
    src/FileDropTarget.cpp
    src/TransferManager.cpp
    src/main.cpp
)

target_link_libraries(teleliter ${wxWidgets_LIBRARIES})
