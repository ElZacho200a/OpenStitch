add_executable(test_project_io
    test_roundtrip.cpp
)
target_link_libraries(test_project_io
    PRIVATE openstitch::project_io Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_project_io)
