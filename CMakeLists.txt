cmake_minimum_required(VERSION 3.16)

file(READ "${CMAKE_SOURCE_DIR}/VERSION" PROJECT_VERSION)
string(STRIP "${PROJECT_VERSION}" PROJECT_VERSION)

project(EVEAPMPreview VERSION ${PROJECT_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Include FetchContent for downloading dependencies
include(FetchContent)

# Fetch and build libuiohook
FetchContent_Declare(
    libuiohook
    GIT_REPOSITORY https://github.com/kwhat/libuiohook.git
    GIT_TAG        1.3
)

# Configure libuiohook build options
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build libuiohook as static library" FORCE)
set(ENABLE_TEST OFF CACHE BOOL "Disable libuiohook tests" FORCE)
set(ENABLE_DEMO OFF CACHE BOOL "Disable libuiohook demos" FORCE)
set(ENABLE_QUIET OFF CACHE BOOL "Enable libuiohook logging" FORCE)

# Disable Qt processing for libuiohook
set(CMAKE_AUTOMOC_OFF_LIBUIOHOOK ${CMAKE_AUTOMOC})
set(CMAKE_AUTORCC_OFF_LIBUIOHOOK ${CMAKE_AUTORCC})
set(CMAKE_AUTOUIC_OFF_LIBUIOHOOK ${CMAKE_AUTOUIC})
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)

FetchContent_MakeAvailable(libuiohook)

# Restore Qt processing settings
set(CMAKE_AUTOMOC ${CMAKE_AUTOMOC_OFF_LIBUIOHOOK})
set(CMAKE_AUTORCC ${CMAKE_AUTORCC_OFF_LIBUIOHOOK})
set(CMAKE_AUTOUIC ${CMAKE_AUTOUIC_OFF_LIBUIOHOOK})

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Network)

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_BINARY_DIR}/include)  

configure_file(
    "${CMAKE_SOURCE_DIR}/include/version.h.in"
    "${CMAKE_BINARY_DIR}/include/version.h"
    @ONLY
)

set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/windowcapture.cpp
    src/thumbnailwidget.cpp
    src/config.cpp
    src/overlayinfo.cpp
    src/hotkeymanager.cpp
    src/hookthread.cpp
    src/uiohookmanager.cpp
    src/configdialog.cpp
    src/hotkeycapture.cpp
    src/settingbinding.cpp
    src/stylesheet.cpp
    src/chatlogreader.cpp
    src/systemcolorsdialog.cpp
)

set(RESOURCES
    resources/resources.qrc
)

set(HEADERS
    include/mainwindow.h
    include/windowcapture.h
    include/thumbnailwidget.h
    include/config.h
    include/overlayinfo.h
    include/hotkeymanager.h
    include/hookthread.h
    include/uiohookmanager.h
    include/configdialog.h
    include/hotkeycapture.h
    include/settingbinding.h
    include/stylesheet.h
    include/chatlogreader.h
    include/systemcolorsdialog.h
    ${CMAKE_BINARY_DIR}/include/version.h  
)

if(WIN32)
    configure_file(
        "${CMAKE_SOURCE_DIR}/resources/app.rc.in"
        "${CMAKE_BINARY_DIR}/resources/app.rc"
        @ONLY
    )
    set(RESOURCE_FILES ${CMAKE_BINARY_DIR}/resources/app.rc)
endif()

add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS} ${RESOURCES} ${RESOURCE_FILES})

target_link_libraries(${PROJECT_NAME} 
    Qt6::Core 
    Qt6::Widgets 
    Qt6::Gui
    Qt6::Network
    uiohook
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} dwmapi user32 gdi32 advapi32)
endif()

# Disable debug output in release builds
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:QT_NO_DEBUG_OUTPUT>
    $<$<CONFIG:MinSizeRel>:QT_NO_DEBUG_OUTPUT>
    $<$<CONFIG:RelWithDebInfo>:QT_NO_DEBUG_OUTPUT>
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(WIN32)
    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
    
    # Only run windeployqt for dynamic builds (not static)
    if(WINDEPLOYQT_EXECUTABLE AND BUILD_SHARED_LIBS)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" 
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "Deploying Qt dependencies..."
        )
    endif()
endif()
