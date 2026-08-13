add_library(openstitch_satin_coverage
    src/coverage.cpp
)
add_library(openstitch::satin_coverage ALIAS openstitch_satin_coverage)

target_include_directories(openstitch_satin_coverage PUBLIC include)
target_link_libraries(openstitch_satin_coverage
    PUBLIC openstitch::core openstitch::geometry openstitch::stitch_generation
    PRIVATE openstitch::warnings
)
