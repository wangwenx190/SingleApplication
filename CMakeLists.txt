cmake_minimum_required(VERSION 4.0)

project(SingleApplication
    VERSION 3.5.4
    DESCRIPTION "Replacement of QtSingleApplication"
    LANGUAGES CXX
)

if(WIN32)
    enable_language(RC)
endif()
if(NOT DEFINED CMAKE_BUILD_TYPE AND NOT DEFINED CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_CXX_VISIBILITY_PRESET "hidden")
if(NOT "x${CMAKE_CXX_FLAGS}" STREQUAL "x")
    if(MSVC)
        string(REGEX REPLACE " [/-]W[01234] " " " CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
    else()
        string(REGEX REPLACE " -W(all)?(extra)? " " " CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
        string(REGEX REPLACE " -[W]?pedantic " " " CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
    endif()
endif()
string(APPEND CMAKE_CXX_FLAGS " -w ")

set(__top_level ON)
if("x${CMAKE_SOURCE_DIR}" STREQUAL "x${PROJECT_SOURCE_DIR}")
    message("SingleApplication: Building as a top-level project.")
else()
    set(__top_level OFF)
    message("SingleApplication: Building as a subproject.")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(__qt_comps Core Network)
set(__qt_targets Qt6::Core Qt6::Network)
if(DEFINED SINGLEAPPLICATION_QAPP_CLASS)
    if("x${SINGLEAPPLICATION_QAPP_CLASS}" STREQUAL "xQCoreApplication")
    elseif("x${SINGLEAPPLICATION_QAPP_CLASS}" STREQUAL "xQGuiApplication")
        list(APPEND __qt_comps Gui)
        list(APPEND __qt_targets Qt6::Gui)
    elseif("x${SINGLEAPPLICATION_QAPP_CLASS}" STREQUAL "xQApplication")
        list(APPEND __qt_comps Widgets)
        list(APPEND __qt_targets Qt6::Widgets)
    else()
        message(SEND_ERROR "Invalid SINGLEAPPLICATION_QAPP_CLASS: ${SINGLEAPPLICATION_QAPP_CLASS}")
    endif()
else()
    set(SINGLEAPPLICATION_QAPP_CLASS QCoreApplication)
endif()
message("SINGLEAPPLICATION_QAPP_CLASS: ${SINGLEAPPLICATION_QAPP_CLASS}")

find_package(Qt6 REQUIRED COMPONENTS ${__qt_comps})

option(SINGLEAPPLICATION_BUILD_SHARED "Build shared library edition of SingleApplication." ON)
option(SINGLEAPPLICATION_BUILD_STATIC "Build static library edition of SingleApplication." ON)
option(SINGLEAPPLICATION_INSTALL "Enable installation for SingleApplication." OFF)
if(NOT SINGLEAPPLICATION_BUILD_SHARED AND NOT SINGLEAPPLICATION_BUILD_STATIC)
    message(SEND_ERROR "You MUST build either the shared library or the static library or both of them!")
endif()
set(__targets)
if(SINGLEAPPLICATION_BUILD_SHARED)
    add_library(${PROJECT_NAME}_shared SHARED)
    add_library(${PROJECT_NAME}::${PROJECT_NAME}_shared ALIAS ${PROJECT_NAME}_shared)
    set(__dllname ${PROJECT_NAME})
    if(NOT MSVC)
        set(__dllname lib${PROJECT_NAME})
    endif()
    set_target_properties(${PROJECT_NAME}_shared PROPERTIES OUTPUT_NAME ${__dllname})
    target_compile_definitions(${PROJECT_NAME}_shared PRIVATE
        SINGLEAPPLICATION_BUILD_LIBRARY
    )
    if(WIN32)
        target_sources(${PROJECT_NAME}_shared PRIVATE singleapplication.rc)
    endif()
    list(APPEND __targets ${PROJECT_NAME}_shared)
endif()
if(SINGLEAPPLICATION_BUILD_STATIC)
    add_library(${PROJECT_NAME}_static STATIC)
    add_library(${PROJECT_NAME}::${PROJECT_NAME}_static ALIAS ${PROJECT_NAME}_static)
    target_compile_definitions(${PROJECT_NAME}_static PUBLIC
        SINGLEAPPLICATION_BUILD_STATIC
    )
    list(APPEND __targets ${PROJECT_NAME}_static)
endif()
if(SINGLEAPPLICATION_BUILD_SHARED AND NOT SINGLEAPPLICATION_BUILD_STATIC)
    add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME}_shared)
endif()
if(SINGLEAPPLICATION_BUILD_STATIC AND NOT SINGLEAPPLICATION_BUILD_SHARED)
    add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME}_static)
endif()

include(GNUInstallDirs)
foreach(__target IN LISTS __targets)
    target_sources(${__target} PRIVATE
        singleapplication/singleapplication_global.h
        singleapplication/singleapplication.h
        singleapplication/singleapplication.cpp
        singleapplication/singleapplication_p.h
        singleapplication/singleapplication_p.cpp
    )
    target_include_directories(${__target} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/singleapplication
    )
    target_compile_definitions(${__target} PUBLIC
        SINGLEAPPLICATION_QAPP_CLASS=${SINGLEAPPLICATION_QAPP_CLASS}
    )
    if(__top_level)
        if(WIN32)
            target_compile_definitions(${__target} PRIVATE
                UNICODE _UNICODE
                NOMINMAX
                _USE_MATH_DEFINES
                WIN32_LEAN_AND_MEAN
            )
        endif()
        if(MSVC)
            target_compile_options(${__target} PRIVATE
                /utf-8 /Zc:__cplusplus
                $<$<NOT:$<CONFIG:Debug>>:/Gw /Gy /Zc:inline>
            )
            target_link_options(${__target} PRIVATE
                $<$<NOT:$<CONFIG:Debug>>:/OPT:REF /OPT:ICF>
            )
        else()
            target_compile_options(${__target} PRIVATE
                $<$<NOT:$<CONFIG:Debug>>:-ffunction-sections -fdata-sections>
            )
            target_link_options(${__target} PRIVATE
                $<$<NOT:$<CONFIG:Debug>>:-Wl,--gc-sections>
            )
        endif()
    endif()
    set_target_properties(${__target} PROPERTIES
        VERSION ${PROJECT_VERSION}
    )
    set(__dep_visibility PRIVATE)
    get_target_property(__type ${__target} TYPE)
    if("x${__type}" STREQUAL "xSTATIC_LIBRARY")
        set(__dep_visibility PUBLIC)
    else()
        set_target_properties(${__target} PROPERTIES
            SOVERSION ${PROJECT_VERSION_MAJOR}
        )
    endif()
    target_link_libraries(${__target} ${__dep_visibility} ${__qt_targets})
    if(SINGLEAPPLICATION_INSTALL)
        install(TARGETS ${__target}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
        install(
            FILES
                singleapplication/singleapplication_global.h
                singleapplication/singleapplication.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
    endif()
endforeach()
