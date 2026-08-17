find_package(Qt6 COMPONENTS Widgets QUIET)
if(NOT Qt6_FOUND)
    message(WARNING
        "Qt6 introuvable : l'application desktop ne sera pas construite. "
        "Définissez la variable d'environnement QT_ROOT (ex. C:/Qt/6.8.3/msvc2022_64).")
    return()
endif()

set(CMAKE_AUTOMOC ON)

# Bibliothèque des widgets desktop (tout sauf main()) : permet de lier les
# mêmes sources dans l'exécutable et dans les tests Qt (tests/unit/desktop),
# sans dupliquer la compilation ni exposer de membres publics rien que pour
# les tests.
add_library(openstitch_desktop_widgets STATIC
    main_window.cpp
    main_window.hpp
    app_theme.cpp
    app_theme.hpp
    design_tokens.cpp
    design_tokens.hpp
    canvas_view.cpp
    canvas_view.hpp
    properties_panel.cpp
    properties_panel.hpp
    document_panel.cpp
    document_panel.hpp
    workflow_panel.cpp
    workflow_panel.hpp
    empty_state_widget.cpp
    empty_state_widget.hpp
    tools.hpp
    ui_icons.cpp
    ui_icons.hpp
    ruler.cpp
    ruler.hpp
    import_dialog.cpp
    import_dialog.hpp
    brightness_dialog.cpp
    brightness_dialog.hpp
    node_handle.hpp
    satin_guide_item.hpp
    wsl_path_converter.cpp
    wsl_path_converter.hpp
    sam_worker_client.cpp
    sam_worker_client.hpp
    sam_model_manager.cpp
    sam_model_manager.hpp
    ai_preferences.cpp
    ai_preferences.hpp
    ai_preferences_dialog.cpp
    ai_preferences_dialog.hpp
    ai_segmentation_dialog.cpp
    ai_segmentation_dialog.hpp
)
add_library(openstitch::desktop_widgets ALIAS openstitch_desktop_widgets)
target_include_directories(openstitch_desktop_widgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openstitch_desktop_widgets
    # `satin_planning` en dependance DIRECTE depuis le 2026-08-17 (mission de
    # durcissement du contrat SatinPlanner, §4) : les creations satin
    # manuelles/conversions de type/ligne de coupe appellent desormais
    # `satin_planning::build_satin_sections` directement (deplace depuis
    # `autodigitize`, qui n'a plus de raison architecturale d'etre sur ce
    # chemin) -- `autodigitize` reste lie separement pour sa PROPRE
    # fonctionnalite (auto-numerisation), pas comme detour obligatoire vers
    # un adaptateur generique.
    PUBLIC Qt6::Widgets openstitch::core openstitch::image openstitch::document
           openstitch::commands openstitch::vectorization openstitch::stitch
           openstitch::stitch_generation openstitch::formats openstitch::project_io
           openstitch::stitch_analysis openstitch::optimization openstitch::autodigitize
           openstitch::auto_satin openstitch::satin_planning openstitch::segmentation
           openstitch::ai_segmentation
    PRIVATE openstitch::warnings
)

add_executable(openstitch WIN32 main.cpp)
target_link_libraries(openstitch PRIVATE openstitch_desktop_widgets openstitch::warnings)

# Copie les DLL Qt à côté de l'exécutable pour pouvoir le lancer directement.
# (Les DLL vcpkg — OpenCV, etc. — sont déjà copiées par VCPKG_APPLOCAL_DEPS.)
if(WIN32 AND TARGET Qt6::windeployqt)
    add_custom_command(TARGET openstitch POST_BUILD
        COMMAND Qt6::windeployqt --no-translations --no-system-d3d-compiler
                "$<TARGET_FILE:openstitch>"
        COMMENT "Déploiement des DLL Qt (windeployqt)")
endif()
