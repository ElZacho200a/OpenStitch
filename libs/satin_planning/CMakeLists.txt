add_library(openstitch_satin_planning
    src/branch_pairing.cpp
)
add_library(openstitch::satin_planning ALIAS openstitch_satin_planning)

target_include_directories(openstitch_satin_planning PUBLIC include)
target_link_libraries(openstitch_satin_planning
    PUBLIC openstitch::core openstitch::auto_satin
    PRIVATE openstitch::warnings
)
