add_library(openstitch_autodigitize
    src/autodigitize.cpp
)
add_library(openstitch::autodigitize ALIAS openstitch_autodigitize)

target_include_directories(openstitch_autodigitize PUBLIC include)
target_link_libraries(openstitch_autodigitize
    PUBLIC openstitch::core openstitch::document openstitch::segmentation
           openstitch::vectorization openstitch::stitch_generation openstitch::auto_satin
           openstitch::satin_coverage
    PRIVATE openstitch::satin_planning openstitch::warnings
)
