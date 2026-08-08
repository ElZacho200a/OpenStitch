find_package(nlohmann_json CONFIG REQUIRED)

add_library(openstitch_ai_segmentation
    src/model_catalog.cpp
    src/error.cpp
    src/mask_result.cpp
    src/label_map.cpp
    src/topology_cleanup.cpp
)
add_library(openstitch::ai_segmentation ALIAS openstitch_ai_segmentation)

target_include_directories(openstitch_ai_segmentation PUBLIC include)
# nlohmann_json en PRIVATE : aucun type JSON ne sort de cette bibliothèque
# (ADR-006, même encapsulation que project_io).
target_link_libraries(openstitch_ai_segmentation
    PUBLIC openstitch::core openstitch::segmentation
    PRIVATE nlohmann_json::nlohmann_json openstitch::warnings
)
