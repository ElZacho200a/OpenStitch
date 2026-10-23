find_package(nlohmann_json CONFIG REQUIRED)

add_executable(test_mcp test_server.cpp test_guide.cpp test_tools_semantic.cpp)
target_link_libraries(test_mcp
    PRIVATE openstitch::mcp openstitch::image nlohmann_json::nlohmann_json Catch2::Catch2WithMain
            openstitch::warnings)
target_compile_definitions(test_mcp
    PRIVATE OPENSTITCH_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
            OPENSTITCH_SKILL_DIR="${PROJECT_SOURCE_DIR}/.claude/skills/openstitch-digitize")
catch_discover_tests(test_mcp)
