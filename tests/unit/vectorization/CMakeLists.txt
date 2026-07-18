add_executable(test_vectorization
    test_vectorize.cpp
)
target_link_libraries(test_vectorization
    PRIVATE openstitch::vectorization Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_vectorization)
