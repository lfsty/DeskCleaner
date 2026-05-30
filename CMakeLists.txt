cmake_minimum_required(VERSION 3.10)
project(DeskCleaner LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS REQUIRED Core Gui Quick)

set(SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/sources/src/main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/src/desktopIconManager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/sources/src/mainwindowviewmodel.cpp
)

set(HEADERS
    ${CMAKE_CURRENT_LIST_DIR}/sources/include/mainwindowviewmodel.h
    ${CMAKE_CURRENT_LIST_DIR}/sources/src/desktopIconManager.h
)

set(RESOURCES
    ${CMAKE_CURRENT_LIST_DIR}/sources/resources/resource.qrc
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${RESOURCES})

target_include_directories(${PROJECT_NAME} 
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sources/include
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Ole32
    Shell32
    Shlwapi
)
