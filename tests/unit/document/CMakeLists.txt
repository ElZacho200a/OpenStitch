add_executable(test_document
    test_image_placement.cpp
)
target_link_libraries(test_document PRIVATE openstitch::document Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_document)
