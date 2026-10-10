add_executable(test_autodigitize
    test_autodigitize.cpp
    test_thread_limit.cpp
)
target_link_libraries(test_autodigitize
    PRIVATE openstitch::autodigitize openstitch::segmentation Catch2::Catch2WithMain
            openstitch::warnings)
catch_discover_tests(test_autodigitize)

add_executable(test_contour
    test_contour_options.cpp
    test_contour_pipeline.cpp
    test_contour_adversarial.cpp
)
target_link_libraries(test_contour
    PRIVATE openstitch::autodigitize openstitch::segmentation Catch2::Catch2WithMain
            openstitch::warnings)
catch_discover_tests(test_contour)
