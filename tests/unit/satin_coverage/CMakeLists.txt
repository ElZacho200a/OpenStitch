add_executable(test_satin_coverage
    test_coverage.cpp
)
target_link_libraries(test_satin_coverage
    PRIVATE openstitch::satin_coverage openstitch::auto_satin Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_satin_coverage)
