add_executable(test_stitch
    test_stats.cpp
    test_running_stitch.cpp
    test_generate.cpp
    test_tatami.cpp
    test_satin.cpp
    test_routing.cpp
    test_overrides.cpp
)
target_link_libraries(test_stitch
    PRIVATE openstitch::stitch openstitch::stitch_generation Catch2::Catch2WithMain
            openstitch::warnings)
catch_discover_tests(test_stitch)
