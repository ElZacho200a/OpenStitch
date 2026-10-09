add_executable(test_stitch_render
    test_params.cpp
    test_segments.cpp
    test_raster.cpp
)
target_link_libraries(test_stitch_render
    PRIVATE openstitch::stitch_render Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_stitch_render)
