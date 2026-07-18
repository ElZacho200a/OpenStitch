add_executable(test_commands
    test_undo_stack.cpp
)
target_link_libraries(test_commands PRIVATE openstitch::commands Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_commands)
