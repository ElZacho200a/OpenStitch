add_executable(test_core
    test_units.cpp
    test_ids.cpp
)
target_link_libraries(test_core PRIVATE openstitch::core Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_core)
