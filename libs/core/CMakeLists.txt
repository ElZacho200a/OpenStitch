find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_library(openstitch_core
    src/log.cpp
)
add_library(openstitch::core ALIAS openstitch_core)

target_include_directories(openstitch_core PUBLIC include)
target_link_libraries(openstitch_core
    PUBLIC fmt::fmt spdlog::spdlog
    PRIVATE openstitch::warnings
)
