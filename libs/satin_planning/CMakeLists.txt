add_library(openstitch_satin_planning
    src/branch_pairing.cpp
    src/region_split.cpp
    src/region_satinability.cpp
    src/region_oracle.cpp
    src/beam_search.cpp
    src/merge_pass.cpp
    src/overlap.cpp
    src/region_routing.cpp
    src/concavity_cuts.cpp
    src/satin_plan.cpp
    src/satin_sections.cpp
)
add_library(openstitch::satin_planning ALIAS openstitch_satin_planning)

target_include_directories(openstitch_satin_planning PUBLIC include)
target_link_libraries(openstitch_satin_planning
    # `document` PUBLIC depuis le 2026-08-17 (mission de durcissement du
    # contrat, §4) : `satin_sections.hpp` expose `BuiltSatinSection`/
    # `SatinBuildReport` en termes de `document::SatinParams` -- l'adaptateur
    # generique `SatinPlan -> sections satin` deplace ici depuis
    # `autodigitize` (qui n'avait aucune raison architecturale de le
    # posseder, cf. audit Phase 1).
    PUBLIC openstitch::core openstitch::auto_satin openstitch::satin_coverage openstitch::stitch_generation
           openstitch::document
    PRIVATE openstitch::warnings
)
