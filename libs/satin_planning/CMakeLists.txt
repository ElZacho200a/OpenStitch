add_library(openstitch_satin_planning
    src/branch_pairing.cpp
    src/region_split.cpp
    src/region_satinability.cpp
    src/region_oracle.cpp
    src/beam_search.cpp
    src/merge_pass.cpp
    src/overlap.cpp
    src/region_routing.cpp
    src/satin_plan.cpp
)
add_library(openstitch::satin_planning ALIAS openstitch_satin_planning)

target_include_directories(openstitch_satin_planning PUBLIC include)
target_link_libraries(openstitch_satin_planning
    PUBLIC openstitch::core openstitch::auto_satin openstitch::satin_coverage openstitch::stitch_generation
    PRIVATE openstitch::warnings
)
