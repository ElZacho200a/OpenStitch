add_executable(test_geometry
    test_simplify.cpp
    test_clean.cpp
    test_offset.cpp
)
target_link_libraries(test_geometry PRIVATE openstitch::geometry Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_geometry)
