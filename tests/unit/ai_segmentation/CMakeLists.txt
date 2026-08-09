add_executable(test_ai_segmentation
    test_model_catalog.cpp
    test_error.cpp
    test_mask_result.cpp
    test_label_map.cpp
    test_topology_cleanup.cpp
    test_color_refine.cpp
)
target_link_libraries(test_ai_segmentation
    PRIVATE openstitch::ai_segmentation Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_ai_segmentation)
