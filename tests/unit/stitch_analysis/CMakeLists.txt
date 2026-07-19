add_executable(test_stitch_analysis
    test_analyze.cpp
)
target_link_libraries(test_stitch_analysis
    PRIVATE openstitch::stitch_analysis Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_stitch_analysis)
