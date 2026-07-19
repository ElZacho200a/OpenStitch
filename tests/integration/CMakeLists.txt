add_executable(test_pipeline
    test_pipeline.cpp
)
target_link_libraries(test_pipeline
    PRIVATE openstitch::segmentation openstitch::autodigitize openstitch::stitch_generation
            openstitch::formats openstitch::project_io Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_pipeline)
