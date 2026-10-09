add_library(openstitch_autodigitize
    src/autodigitize.cpp
    src/contour_options.cpp
    src/contour_analysis.cpp
    src/contour_objects.cpp
)
add_library(openstitch::autodigitize ALIAS openstitch_autodigitize)

target_include_directories(openstitch_autodigitize PUBLIC include)
target_link_libraries(openstitch_autodigitize
    PUBLIC openstitch::core openstitch::document openstitch::segmentation
           openstitch::vectorization openstitch::stitch_generation openstitch::auto_satin
    PRIVATE openstitch::optimization openstitch::warnings
)
