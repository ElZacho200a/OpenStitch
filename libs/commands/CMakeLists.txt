add_library(openstitch_commands
    src/undo_stack.cpp
)
add_library(openstitch::commands ALIAS openstitch_commands)

target_include_directories(openstitch_commands PUBLIC include)
target_link_libraries(openstitch_commands
    PUBLIC openstitch::document
    PRIVATE openstitch::warnings
)
