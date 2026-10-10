find_package(Threads REQUIRED)

add_library(openstitch_stitch_render
    src/params.cpp
    src/segments.cpp
    src/raster.cpp
)
add_library(openstitch::stitch_render ALIAS openstitch_stitch_render)

target_include_directories(openstitch_stitch_render PUBLIC include)
target_link_libraries(openstitch_stitch_render
    PUBLIC openstitch::core openstitch::stitch
    PRIVATE Threads::Threads openstitch::warnings
)
