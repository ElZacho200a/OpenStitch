cmake_minimum_required(VERSION 3.27)

project(OpenStitchStudio
    VERSION 0.1.0
    DESCRIPTION "Logiciel libre de numérisation pour broderie machine"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OPENSTITCH_BUILD_DESKTOP "Build the Qt desktop application" ON)
option(OPENSTITCH_BUILD_TESTS "Build unit tests" ON)

# Cible d'interface portant les options de warnings communes.
# Liée en PRIVATE par chaque cible du projet (jamais par third_party).
add_library(openstitch_warnings INTERFACE)
add_library(openstitch::warnings ALIAS openstitch_warnings)
if(MSVC)
    target_compile_options(openstitch_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(openstitch_warnings INTERFACE -Wall -Wextra -Wconversion)
endif()

if(OPENSTITCH_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)
endif()

add_subdirectory(libs/core)
add_subdirectory(libs/geometry)
add_subdirectory(libs/image)
add_subdirectory(libs/segmentation)
add_subdirectory(libs/vectorization)
add_subdirectory(libs/document)
add_subdirectory(libs/stitch)
add_subdirectory(libs/stitch_generation)
add_subdirectory(libs/commands)
add_subdirectory(apps/cli)

if(OPENSTITCH_BUILD_DESKTOP)
    add_subdirectory(apps/desktop)
endif()

if(OPENSTITCH_BUILD_TESTS)
    add_subdirectory(tests)
endif()
