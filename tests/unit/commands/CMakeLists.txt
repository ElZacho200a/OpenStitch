add_executable(test_commands
    test_undo_stack.cpp
    test_command_merge.cpp
    test_finishing_commands.cpp
    test_composite_command.cpp
    test_composite_adversarial.cpp
    test_import_machine_design_command.cpp
    test_vector_ops.cpp
)
target_link_libraries(test_commands PRIVATE openstitch::commands Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_commands)
