cmake_minimum_required(VERSION 3.18)

project(SingleApplication LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(WIN32)
    set(CMAKE_DEBUG_POSTFIX d)
else()
    set(CMAKE_DEBUG_POSTFIX _debug)
endif()

find_package(Qt5 COMPONENTS Widgets Network REQUIRED)

set(SOURCES
    singleapplication_global.h
    singleapplication.h
    singleapplication_p.h
    singleapplication_p.cpp
    singleapplication.cpp
)

if(WIN32 AND BUILD_SHARED_LIBS)
    enable_language(RC)
    list(APPEND SOURCES singleapplication.rc)
endif()

add_library(${PROJECT_NAME} ${SOURCES})

if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(${PROJECT_NAME} PUBLIC SINGLEAPPLICATION_STATIC)
endif()

target_compile_definitions(${PROJECT_NAME} PUBLIC
    QAPPLICATION_CLASS=QApplication
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    SINGLEAPPLICATION_LIBRARY
)
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE Qt::Widgets Qt::Network)
target_include_directories(${PROJECT_NAME} PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>"
)
