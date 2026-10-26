add_executable(test_ai_segmentation
    test_model_catalog.cpp
    test_error.cpp
    test_mask_result.cpp
    test_label_map.cpp
    test_topology_cleanup.cpp
    test_color_refine.cpp
    test_backend_protocol.cpp
    test_backend_session.cpp
    test_backend_run.cpp
    test_process_transport.cpp
)
target_link_libraries(test_ai_segmentation
    PRIVATE openstitch::ai_segmentation Catch2::Catch2WithMain openstitch::warnings)
# Worker générique `openstitch.ai.backend/1` (backends factices, stdlib Python seule).
target_compile_definitions(test_ai_segmentation PRIVATE
    OPENSTITCH_AI_WORKER="${CMAKE_SOURCE_DIR}/ai-worker/openstitch_ai_worker.py")
catch_discover_tests(test_ai_segmentation)
