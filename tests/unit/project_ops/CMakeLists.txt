add_executable(test_project_ops
    test_factory_inspect.cpp
    test_operations.cpp
    test_quality.cpp
    test_auto_satin_service.cpp
)
target_link_libraries(test_project_ops
    PRIVATE openstitch::project_ops openstitch::project_io openstitch::stitch_generation
            openstitch::autodigitize openstitch::segmentation openstitch::image
            Catch2::Catch2WithMain openstitch::warnings)
target_compile_definitions(test_project_ops
    PRIVATE OPENSTITCH_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures")
catch_discover_tests(test_project_ops)
