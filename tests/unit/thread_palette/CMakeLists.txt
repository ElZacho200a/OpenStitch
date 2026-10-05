add_executable(test_thread_palette
    test_catalog.cpp
    test_color_distance.cpp
)
target_link_libraries(test_thread_palette PRIVATE openstitch::thread_palette Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_thread_palette)
