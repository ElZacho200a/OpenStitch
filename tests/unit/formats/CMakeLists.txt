add_executable(test_formats
    test_dst.cpp
    test_dxf.cpp
    test_svg.cpp
)
target_link_libraries(test_formats
    PRIVATE openstitch::formats openstitch::stitch_generation Catch2::Catch2WithMain
            openstitch::warnings)
catch_discover_tests(test_formats)
