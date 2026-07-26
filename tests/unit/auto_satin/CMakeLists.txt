add_executable(test_auto_satin
    test_pipeline.cpp
)
target_link_libraries(test_auto_satin
    PRIVATE openstitch::auto_satin Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_auto_satin)
