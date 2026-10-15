find_package(nlohmann_json CONFIG REQUIRED)

# Interface de programmation du moteur pour la CLI (humain, script, agent IA) :
# fabrique de projets, inspection JSON, opérations/lots transactionnels, contrôle
# qualité, service auto-satin. Qt-free ; aucun algorithme n'est réimplémenté ici,
# tout passe par les libs existantes (commands, autodigitize, stitch_analysis…).
# JSON encapsulé : n'apparaît jamais dans l'API publique (std::string en entrée/sortie).
add_library(openstitch_project_ops
    src/factory.cpp
    src/inspect.cpp
    src/json_common.cpp
    src/project_file.cpp
    src/report.cpp
    src/operations.cpp
    src/op_params.cpp
    src/op_geometry.cpp
    src/auto_satin_service.cpp
    src/quality.cpp
)
add_library(openstitch::project_ops ALIAS openstitch_project_ops)

target_include_directories(openstitch_project_ops PUBLIC include)
target_link_libraries(openstitch_project_ops
    PUBLIC openstitch::core openstitch::document openstitch::commands
           openstitch::project_io openstitch::stitch_analysis
    PRIVATE nlohmann_json::nlohmann_json openstitch::image openstitch::segmentation
            openstitch::autodigitize openstitch::stitch_generation openstitch::auto_satin
            openstitch::optimization openstitch::formats openstitch::stitch
            openstitch::geometry openstitch::thread_palette openstitch::warnings
)
