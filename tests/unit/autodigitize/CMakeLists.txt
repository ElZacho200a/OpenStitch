add_executable(test_autodigitize
    test_autodigitize.cpp
)
target_link_libraries(test_autodigitize
    PRIVATE openstitch::autodigitize openstitch::segmentation Catch2::Catch2WithMain
            openstitch::warnings)
catch_discover_tests(test_autodigitize)
