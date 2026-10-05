add_library(openstitch_thread_palette
    src/catalog.cpp
    src/color_distance.cpp
    data/madeira_polyneon.cpp
    data/isacord_40.cpp
)
add_library(openstitch::thread_palette ALIAS openstitch_thread_palette)
target_include_directories(openstitch_thread_palette PUBLIC include)
target_link_libraries(openstitch_thread_palette
    PUBLIC openstitch::core
    PRIVATE openstitch::warnings
)
