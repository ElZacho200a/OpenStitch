add_library(openstitch_stitch_analysis
    src/analyze.cpp
    src/limits.cpp
    src/metrics.cpp
    src/object_coverage.cpp
    src/project_metrics.cpp
    src/color_blocks.cpp
    src/production_sheet.cpp
    src/production_pdf.cpp
    src/thread_usage.cpp
    src/thread_consumption.cpp
    src/density_map.cpp
    src/thickness_map.cpp
    src/uncovered_pockets.cpp
    src/text_rules.cpp
    src/project_analysis.cpp
)
add_library(openstitch::stitch_analysis ALIAS openstitch_stitch_analysis)

target_include_directories(openstitch_stitch_analysis PUBLIC include)
target_link_libraries(openstitch_stitch_analysis
    PUBLIC openstitch::core openstitch::stitch openstitch::document openstitch::thread_palette
    PRIVATE openstitch::geometry openstitch::stitch_generation openstitch::warnings
)
