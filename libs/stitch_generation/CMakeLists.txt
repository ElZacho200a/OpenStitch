add_library(openstitch_stitch_generation
    src/running_stitch.cpp
    src/generate.cpp
)
add_library(openstitch::stitch_generation ALIAS openstitch_stitch_generation)

target_include_directories(openstitch_stitch_generation PUBLIC include)
target_link_libraries(openstitch_stitch_generation
    PUBLIC openstitch::core openstitch::geometry openstitch::stitch openstitch::document
    PRIVATE openstitch::warnings
)
