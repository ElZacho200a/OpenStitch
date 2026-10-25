add_library(openstitch_commands
    src/undo_stack.cpp
    src/composite_command.cpp
    src/vector_ops.cpp
    src/fill_pockets.cpp
    src/transform_ops.cpp
    src/text_commands.cpp
    src/vector_path_commands.cpp
    src/outline_ops.cpp
    src/click_to_stitch_commands.cpp
)
add_library(openstitch::commands ALIAS openstitch_commands)

target_include_directories(openstitch_commands PUBLIC include)
target_link_libraries(openstitch_commands
    PUBLIC openstitch::document openstitch::segmentation
    PRIVATE openstitch::autodigitize openstitch::warnings
)
