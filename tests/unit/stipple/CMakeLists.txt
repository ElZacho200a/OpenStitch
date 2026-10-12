add_executable(test_stipple
    test_tour.cpp
    test_stipple.cpp
)
target_link_libraries(test_stipple
    PRIVATE openstitch::stipple Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_stipple)
