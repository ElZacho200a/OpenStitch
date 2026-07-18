add_library(openstitch_stitch
    src/stats.cpp
)
add_library(openstitch::stitch ALIAS openstitch_stitch)

target_include_directories(openstitch_stitch PUBLIC include)
target_link_libraries(openstitch_stitch
    PUBLIC openstitch::core
    PRIVATE openstitch::warnings
)
