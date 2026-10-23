find_package(nlohmann_json CONFIG REQUIRED)

# Serveur MCP (outils pour agents IA) au-dessus de project_ops. Qt-free ; JSON encapsulé :
# l'API publique n'échange que des std::string.
add_library(openstitch_mcp
    src/server.cpp
    src/tools.cpp
)
add_library(openstitch::mcp ALIAS openstitch_mcp)

target_include_directories(openstitch_mcp PUBLIC include PRIVATE src)
target_link_libraries(openstitch_mcp
    PUBLIC openstitch::core
    PRIVATE nlohmann_json::nlohmann_json openstitch::project_ops openstitch::document
            openstitch::project_io openstitch::image openstitch::stitch
            openstitch::autodigitize openstitch::segmentation
            openstitch::stitch_generation openstitch::warnings
)
