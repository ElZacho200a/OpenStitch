find_package(nlohmann_json CONFIG REQUIRED)

#Agent IA du compagnon : interface fournisseur, fournisseur Anthropic(API Messages), boucle
#d 'agent bornée et interruptible, politique d' outils, stockage de secrets abstrait.Qt - free,
#sans réseau(transport HTTP injecté par l'hôte) ; JSON encapsulé (PRIVATE).
    add_library(openstitch_agent src / anthropic.cpp src / runner.cpp src / secret.cpp src /
                mcp_adapter.cpp src / diagnostics.cpp)
        add_library(openstitch::agent ALIAS openstitch_agent)

            target_include_directories(openstitch_agent PUBLIC include) target_link_libraries(
                openstitch_agent PRIVATE nlohmann_json::nlohmann_json openstitch::warnings)
