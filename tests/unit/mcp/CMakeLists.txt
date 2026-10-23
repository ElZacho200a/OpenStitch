find_package(nlohmann_json CONFIG REQUIRED)

add_executable(test_mcp test_server.cpp)
target_link_libraries(test_mcp
    PRIVATE openstitch::mcp nlohmann_json::nlohmann_json Catch2::Catch2WithMain
            openstitch::warnings)
catch_discover_tests(test_mcp)
