add_executable(test_pipeline
    test_pipeline.cpp
)
target_link_libraries(test_pipeline
    PRIVATE openstitch::image openstitch::segmentation openstitch::autodigitize openstitch::stitch_generation
            openstitch::formats openstitch::project_io openstitch::optimization Catch2::Catch2WithMain
            openstitch::warnings)
target_compile_definitions(test_pipeline PRIVATE
    OPENSTITCH_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
catch_discover_tests(test_pipeline)
