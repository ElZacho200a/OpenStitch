# Tests du cœur Qt-free de Desktop V2 (Catch2).
add_executable(test_desktop_v2_core
    test_selection_model.cpp
    test_task_manager.cpp
    test_tool_registry.cpp
    test_session_and_pipeline.cpp
    test_preview_service.cpp
    test_hit_and_inspector.cpp
    test_command_catalog.cpp
    test_shape_batch.cpp
    test_transform_batch.cpp
)
target_link_libraries(test_desktop_v2_core
    PRIVATE openstitch::desktop_v2_core openstitch::project_ops openstitch::commands
            Catch2::Catch2WithMain openstitch::warnings)
target_compile_definitions(test_desktop_v2_core
    PRIVATE OPENSTITCH_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
            OPENSTITCH_LEGACY_TOOLS_HEADER="${PROJECT_SOURCE_DIR}/apps/desktop/tools.hpp")
catch_discover_tests(test_desktop_v2_core)

# Tests Qt (QTest, headless) de l'interface Desktop V2.
if(TARGET openstitch::desktop_v2_ui)
    find_package(Qt6 COMPONENTS Test QUIET)
    if(Qt6Test_FOUND)
        set(CMAKE_AUTOMOC ON)
        add_executable(test_desktop_v2_shell test_shell_window.cpp)
        target_link_libraries(test_desktop_v2_shell
            PRIVATE openstitch::desktop_v2_ui Qt6::Test openstitch::warnings)
        target_compile_definitions(test_desktop_v2_shell
            PRIVATE OPENSTITCH_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures")
        add_test(NAME test_desktop_v2_shell COMMAND test_desktop_v2_shell -o -,txt)
        set_tests_properties(test_desktop_v2_shell PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()
