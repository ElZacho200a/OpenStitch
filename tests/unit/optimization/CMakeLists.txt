add_executable(test_optimization
    test_order.cpp
)
target_link_libraries(test_optimization
    PRIVATE openstitch::optimization Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_optimization)
