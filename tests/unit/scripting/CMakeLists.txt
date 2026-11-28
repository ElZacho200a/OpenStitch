add_executable(test_scripting test_selection_script.cpp)
target_link_libraries(test_scripting
    PRIVATE openstitch::scripting openstitch::project_ops openstitch::document openstitch::autodigitize
            Catch2::Catch2WithMain openstitch::warnings)
target_compile_definitions(test_scripting
    PRIVATE OPENSTITCH_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures")
openstitch_deploy_python(test_scripting)
catch_discover_tests(test_scripting)
