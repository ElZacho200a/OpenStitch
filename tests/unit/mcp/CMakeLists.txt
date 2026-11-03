find_package(nlohmann_json CONFIG REQUIRED)

add_executable(test_mcp test_server.cpp test_guide.cpp test_tools_semantic.cpp test_tools_ai.cpp test_live.cpp test_live_agent.cpp
    test_tools_contract.cpp test_fuzz_frames.cpp)
target_link_libraries(test_mcp
    PRIVATE openstitch::mcp openstitch::project_ops openstitch::document openstitch::geometry openstitch::commands openstitch::image openstitch::autodigitize nlohmann_json::nlohmann_json Catch2::Catch2WithMain
            openstitch::warnings)
# Mutateur JSON déterministe partagé avec les tests de lots `apply` (HP-QA-014).
target_include_directories(test_mcp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../project_ops)
target_compile_definitions(test_mcp
    PRIVATE OPENSTITCH_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
            OPENSTITCH_SCHEMAS_DIR="${PROJECT_SOURCE_DIR}/docs/schemas"
            OPENSTITCH_SKILL_DIR="${PROJECT_SOURCE_DIR}/.claude/skills/openstitch-digitize")
catch_discover_tests(test_mcp)
