add_library(openstitch_stitch_generation
    src/running_stitch.cpp
    src/tatami.cpp
    src/directional_fill.cpp
    src/color_blend.cpp
    src/satin.cpp
    src/satin_guides.cpp
    src/lock.cpp
    src/routing.cpp
    src/finish.cpp
    src/length_bounds.cpp
    src/hidden_travel.cpp
    src/underlay_order.cpp
    src/region_order.cpp
    src/generate.cpp
    src/chunk_cache.cpp
    src/overrides.cpp
    src/underlay_auto.cpp
    src/adaptive_pull.cpp
    src/pull_auto.cpp
    src/pull_field.cpp
    src/pull_map.cpp
    src/predicted_pull.cpp
    src/join.cpp
    src/border_satin.cpp
    src/blanket_stitch.cpp
    src/contour_fill.cpp
    src/motif_run.cpp
    src/motif_fill.cpp
    src/radial_fill.cpp
    src/spiral_fill.cpp
    src/crosshatch_fill.cpp
)
add_library(openstitch::stitch_generation ALIAS openstitch_stitch_generation)

target_include_directories(openstitch_stitch_generation PUBLIC include)
target_link_libraries(openstitch_stitch_generation
    PUBLIC openstitch::core openstitch::geometry openstitch::stitch openstitch::document
    PRIVATE openstitch::auto_satin openstitch::warnings
)
