find_package(Clipper2 CONFIG REQUIRED)

add_library(openstitch_geometry
    src/simplify.cpp
    src/clean.cpp
    src/offset.cpp
    src/path.cpp
    src/polyline.cpp
    src/primitives.cpp
)
add_library(openstitch::geometry ALIAS openstitch_geometry)

target_include_directories(openstitch_geometry PUBLIC include)
# Clipper2 en PRIVATE : aucun type Clipper ne sort de cette bibliothèque (ADR-005).
target_link_libraries(openstitch_geometry
    PUBLIC openstitch::core
    PRIVATE Clipper2::Clipper2 openstitch::warnings
)
