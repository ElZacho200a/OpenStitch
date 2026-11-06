# Compagnon IA partagé par les deux applications (bureau V1 et bureau V2) : frontière du fournisseur
# (`CompanionBackend`), fournisseur Claude au-dessus de `libs/agent`, réglages et boîte de dialogue
# des réglages. Qt seulement ; aucune logique du document ici (les outils passent par `libs/mcp`).
# Désactivée comme les applications desktop (OPENSTITCH_BUILD_DESKTOP=OFF, ou Qt6 introuvable).
if(NOT OPENSTITCH_BUILD_DESKTOP)
    return()
endif()
find_package(Qt6 COMPONENTS Widgets Network QUIET)
if(NOT Qt6_FOUND)
    return()
endif()

set(CMAKE_AUTOMOC ON)
add_library(openstitch_companion_backends STATIC
    claude_backend.cpp
    claude_backend.hpp
    companion_backend.hpp
    companion_settings_dialog.cpp
    companion_settings_dialog.hpp
)
add_library(openstitch::companion_backends ALIAS openstitch_companion_backends)
target_include_directories(openstitch_companion_backends PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openstitch_companion_backends
    PUBLIC Qt6::Widgets Qt6::Network openstitch::agent openstitch::mcp
    PRIVATE openstitch::warnings)
