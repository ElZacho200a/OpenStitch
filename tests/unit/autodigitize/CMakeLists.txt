add_executable(test_autodigitize
    test_autodigitize.cpp
    test_thread_limit.cpp
    test_two_color_blend.cpp
    test_line_layer.cpp
)
target_link_libraries(test_autodigitize
    PRIVATE openstitch::autodigitize openstitch::segmentation Catch2::Catch2WithMain
            openstitch::warnings)
catch_discover_tests(test_autodigitize)

add_executable(test_contour
    test_contour_options.cpp
    test_contour_pipeline.cpp
    test_contour_adversarial.cpp
    test_contour_route.cpp
    test_stipple_objects.cpp
)
target_link_libraries(test_contour
    PRIVATE openstitch::autodigitize openstitch::stipple openstitch::segmentation
            openstitch::geometry Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_contour)
