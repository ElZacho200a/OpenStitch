add_library(openstitch_stitch_analysis
    src/analyze.cpp
)
add_library(openstitch::stitch_analysis ALIAS openstitch_stitch_analysis)

target_include_directories(openstitch_stitch_analysis PUBLIC include)
target_link_libraries(openstitch_stitch_analysis
    PUBLIC openstitch::core openstitch::stitch
    PRIVATE openstitch::warnings
)
