add_executable(test_project_ops
    test_factory_inspect.cpp
    test_operations.cpp
    test_param_ops.cpp
    test_group_ops.cpp
    test_geometry_ops.cpp
    test_workflow.cpp
    test_params_geometry.cpp
    test_import_text.cpp
    test_quality.cpp
    test_render.cpp
    test_analysis.cpp
    test_auto_satin_service.cpp
    test_live_bridge.cpp
    test_settings_profile.cpp
)
target_link_libraries(test_project_ops
    PRIVATE openstitch::project_ops openstitch::project_io openstitch::stitch_generation
            openstitch::autodigitize openstitch::segmentation openstitch::image
            Catch2::Catch2WithMain openstitch::warnings)
target_compile_definitions(test_project_ops
    PRIVATE OPENSTITCH_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
            OPENSTITCH_FONTS_DIR="${PROJECT_SOURCE_DIR}/resources/fonts")
catch_discover_tests(test_project_ops)

# Import / remplacement sémantiques : exécutable séparé (petit lien, itération rapide).
add_executable(test_project_ops_semantic test_semantic.cpp)
target_link_libraries(test_project_ops_semantic
    PRIVATE openstitch::project_ops openstitch::project_io openstitch::stitch_generation
            openstitch::autodigitize openstitch::segmentation openstitch::image
            Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_project_ops_semantic)
