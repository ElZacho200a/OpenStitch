add_library(openstitch_stitch_generation
    src/running_stitch.cpp
    src/tatami.cpp
    src/satin.cpp
    src/satin_guides.cpp
    src/lock.cpp
    src/routing.cpp
    src/generate.cpp
    src/overrides.cpp
)
add_library(openstitch::stitch_generation ALIAS openstitch_stitch_generation)

target_include_directories(openstitch_stitch_generation PUBLIC include)
target_link_libraries(openstitch_stitch_generation
    PUBLIC openstitch::core openstitch::geometry openstitch::stitch openstitch::document
    PRIVATE openstitch::warnings
)
