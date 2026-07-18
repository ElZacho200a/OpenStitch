add_executable(test_segmentation
    test_segmentation.cpp
)
target_link_libraries(test_segmentation
    PRIVATE openstitch::segmentation Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_segmentation)
