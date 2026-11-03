find_package(nlohmann_json CONFIG REQUIRED)

add_executable(test_agent test_agent.cpp)
target_link_libraries(test_agent
    PRIVATE openstitch::agent nlohmann_json::nlohmann_json Catch2::Catch2WithMain
            openstitch::warnings)
catch_discover_tests(test_agent)
