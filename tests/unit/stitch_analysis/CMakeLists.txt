add_executable(test_stitch_analysis
    test_analyze.cpp
    test_metrics.cpp
    test_color_blocks.cpp
    test_production_sheet.cpp
    test_thread_usage.cpp
    test_engine_limits.cpp
    test_text_rules.cpp
)
target_link_libraries(test_stitch_analysis
    PRIVATE openstitch::stitch_analysis Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_stitch_analysis)
