find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(openstitch_ai_segmentation
    src/model_catalog.cpp
    src/error.cpp
    src/mask_result.cpp
    src/label_map.cpp
    src/topology_cleanup.cpp
    src/color_refine.cpp
    src/backend_protocol.cpp
    src/backend_session.cpp
    src/backend_registry.cpp
    src/process_transport.cpp
)
add_library(openstitch::ai_segmentation ALIAS openstitch_ai_segmentation)

target_include_directories(openstitch_ai_segmentation PUBLIC include)
# nlohmann_json en PRIVATE : aucun type JSON ne sort de cette bibliothèque
# (ADR-006, même encapsulation que project_io).
target_link_libraries(openstitch_ai_segmentation
    PUBLIC openstitch::core openstitch::image openstitch::segmentation
    PRIVATE nlohmann_json::nlohmann_json openstitch::warnings
    # fil de lecture du transport processus
    Threads::Threads
)
