add_library(openstitch_autodigitize
    src/autodigitize.cpp
    src/contour_options.cpp
    src/contour_analysis.cpp
    src/contour_objects.cpp
    src/contour_route.cpp
    src/line_layer.cpp
    src/stipple_objects.cpp
    src/two_color_blend.cpp
)
add_library(openstitch::autodigitize ALIAS openstitch_autodigitize)

target_include_directories(openstitch_autodigitize PUBLIC include)
target_link_libraries(openstitch_autodigitize
    PUBLIC openstitch::core openstitch::document openstitch::segmentation
           openstitch::image openstitch::geometry openstitch::thread_palette
           openstitch::vectorization openstitch::stitch_generation openstitch::auto_satin
           openstitch::stipple
    PRIVATE openstitch::optimization openstitch::warnings
)
