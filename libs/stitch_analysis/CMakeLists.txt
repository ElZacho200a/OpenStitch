add_library(openstitch_stitch_analysis
    src/analyze.cpp
    src/limits.cpp
    src/metrics.cpp
    src/project_metrics.cpp
    src/color_blocks.cpp
)
add_library(openstitch::stitch_analysis ALIAS openstitch_stitch_analysis)

target_include_directories(openstitch_stitch_analysis PUBLIC include)
target_link_libraries(openstitch_stitch_analysis
    PUBLIC openstitch::core openstitch::stitch openstitch::document openstitch::thread_palette
    PRIVATE openstitch::geometry openstitch::warnings
)
