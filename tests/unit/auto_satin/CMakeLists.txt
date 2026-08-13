add_executable(test_auto_satin
    test_pipeline.cpp
    test_columns.cpp
    test_coverage_regression.cpp
)
target_link_libraries(test_auto_satin
    PRIVATE openstitch::auto_satin openstitch::stitch_generation openstitch::satin_coverage
            Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_auto_satin)
