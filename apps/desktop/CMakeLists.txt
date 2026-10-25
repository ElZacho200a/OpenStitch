find_package(Qt6 COMPONENTS Widgets PrintSupport LinguistTools QUIET)
if(NOT Qt6_FOUND)
    message(WARNING
        "Qt6 introuvable : l'application desktop ne sera pas construite. "
        "Définissez la variable d'environnement QT_ROOT (ex. C:/Qt/6.8.3/msvc2022_64).")
    return()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON) # polices intégrées du lettrage (lettering_fonts.qrc)

# Bibliothèque des widgets desktop (tout sauf main()) : permet de lier les
# mêmes sources dans l'exécutable et dans les tests Qt (tests/unit/desktop),
# sans dupliquer la compilation ni exposer de membres publics rien que pour
# les tests.
add_library(openstitch_desktop_widgets STATIC
    main_window.cpp
    command_palette_dialog.cpp
    command_palette_dialog.hpp
    main_window_directional.cpp
    main_window_contour.cpp
    main_window_color_blend.cpp
    color_blend_dialog.cpp
    color_blend_dialog.hpp
    busy_indicator.hpp
    main_window_engine.cpp
    main_window_satin_auto.cpp
    main_window_regions.cpp
    main_window_shapes.cpp
    main_window_objects.cpp
    simulation_nav_bar.cpp
    stitch_type_catalog.cpp
    main_window_point_edit.cpp
    simulation_nav_bar.hpp
    main_window_rotation.cpp
    main_window_workflow.cpp
    ui_memory.hpp
    main_window_ux.cpp
    main_window_analysis.cpp
    main_window_realistic.cpp
    main_window_production.cpp
    main_window_colorways.cpp
    colorway_dialog.cpp
    colorway_dialog.hpp
    thread_merge_dialog.cpp
    thread_merge_dialog.hpp
    fabric_params.hpp
    # [cablage-A:debut]
    main_window_export_image.cpp
    export_image.cpp
    export_image.hpp
    export_image_dialog.cpp
    export_image_dialog.hpp
    # [cablage-A:fin]
    # [cablage-B:debut]
    main_window_vector_tools.cpp
    # [cablage-B:fin]
    main_window_vector_edit.cpp
    # [cablage-C:debut]
    main_window_measure.cpp
    main_window_reshape.cpp
    main_window_brush.cpp
    main_window_node_select.cpp
    main_window_bucket.cpp
    main_window_click_to_stitch.cpp
    main_window_eyedropper.cpp
    # [cablage-C:fin]
    # [cablage-D:debut]
    # [cablage-D:fin]
    # [cablage-E:debut]
    density_overlay.cpp
    density_overlay.hpp
    main_window_density.cpp
    # [cablage-E:fin]
    # [cablage-HOOP:debut]
    machine_dialog.cpp
    machine_dialog.hpp
    main_window_machine.cpp
    # [cablage-HOOP:fin]
    production_dialog.cpp
    production_dialog.hpp
    realistic_preferences.cpp
    realistic_preferences.hpp
    realistic_render_dialog.cpp
    realistic_render_dialog.hpp
    realistic_view_state.hpp
    main_window_editing.cpp
    main_window_threads.cpp
    thread_panel.cpp
    thread_panel.hpp
    thread_strip.cpp
    thread_strip.hpp
    main_window_lettering.cpp
    font_catalog.cpp
    font_catalog.hpp
    text_dialog.cpp
    text_dialog.hpp
    text_advanced_panel.cpp
    text_advanced_panel.hpp
    lettering_fonts.qrc
    main_window.hpp
    app_settings.cpp
    app_settings.hpp
    app_language.cpp
    app_language.hpp
    settings_dialog.cpp
    settings_dialog.hpp
    app_theme.cpp
    app_theme.hpp
    design_tokens.cpp
    design_tokens.hpp
    canvas_view.cpp
    canvas_view.hpp
    properties_panel.cpp
    properties_panel.hpp
    calque_panel.cpp
    calque_panel.hpp
    document_panel.cpp
    document_panel.hpp
    segmentation_colors_panel.cpp
    segmentation_colors_panel.hpp
    main_window_colors.cpp
    main_window_segment_preview.cpp
    segmentation_dialog.cpp
    segmentation_dialog.hpp
    workflow_panel.cpp
    workflow_panel.hpp
    empty_state_widget.cpp
    empty_state_widget.hpp
    recent_files.cpp
    recent_files.hpp
    autosave.cpp
    autosave.hpp
    tools.hpp
    interaction_map.cpp
    interaction_map.hpp
    help_dialogs.cpp
    help_dialogs.hpp
    selection_hit_test.cpp
    selection_hit_test.hpp
    ui_icons.cpp
    ui_icons.hpp
    ruler.cpp
    ruler.hpp
    import_dialog.cpp
    import_dialog.hpp
    brightness_dialog.cpp
    brightness_dialog.hpp
machine_export_dialog.cpp
machine_export_dialog.hpp
    generation_options_dialog.cpp
    generation_options_dialog.hpp
    node_handle.hpp
    satin_guide_item.hpp
    wsl_path_converter.cpp
    wsl_path_converter.hpp
    sam_worker_client.cpp
    sam_worker_client.hpp
    ai_preferences.cpp
    ai_preferences.hpp
    ai_preferences_dialog.cpp
    ai_preferences_dialog.hpp
    ai_segmentation_dialog.cpp
    ai_segmentation_dialog.hpp
)
add_library(openstitch::desktop_widgets ALIAS openstitch_desktop_widgets)

# Traductions (HP-I18N-001) : le français est la langue source (chaînes de `tr()`), donc seul
# l'anglais a un fichier. `.ts` -> `.qm` embarqués sous `:/i18n/`. Mise à jour des `.ts` depuis
# les sources : `cmake --build <dir> --target update_translations`.
if(NOT Qt6LinguistTools_FOUND)
    message(FATAL_ERROR "Qt6 LinguistTools introuvable (lrelease/lupdate) : requis pour les traductions.")
endif()
qt_add_translations(openstitch_desktop_widgets
    TS_FILES i18n/openstitch_en.ts
    RESOURCE_PREFIX /i18n)
target_include_directories(openstitch_desktop_widgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openstitch_desktop_widgets
    PUBLIC Qt6::Widgets Qt6::PrintSupport openstitch::core openstitch::image openstitch::document
           openstitch::commands openstitch::vectorization openstitch::stitch openstitch::stitch_render
           openstitch::stitch_generation openstitch::formats openstitch::project_io
           openstitch::stitch_analysis openstitch::optimization openstitch::autodigitize
           openstitch::auto_satin openstitch::segmentation openstitch::lettering
           openstitch::ai_segmentation openstitch::project_ops
    PRIVATE openstitch::warnings
)

add_executable(openstitch WIN32 main.cpp)
target_link_libraries(openstitch PRIVATE openstitch_desktop_widgets openstitch::warnings)

# Copie les DLL Qt à côté de l'exécutable pour pouvoir le lancer directement.
# (Les DLL vcpkg — OpenCV, etc. — sont déjà copiées par VCPKG_APPLOCAL_DEPS.)
if(WIN32 AND TARGET Qt6::windeployqt)
    add_custom_command(TARGET openstitch POST_BUILD
        COMMAND Qt6::windeployqt --translations fr,en --no-system-d3d-compiler
                "$<TARGET_FILE:openstitch>"
        COMMENT "Déploiement des DLL Qt (windeployqt)")
endif()

# Générateur de la table des gestes pour docs/source/user-guide.md (test de
# dérive `docs_gestures_in_sync`). Qt Core + Gui seulement, lié à
# interaction_map.cpp seul (jamais à Widgets, aucun QApplication).
add_executable(openstitch_gesture_table gesture_table_tool.cpp interaction_map.cpp interaction_map.hpp)
target_include_directories(openstitch_gesture_table PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openstitch_gesture_table PRIVATE Qt6::Core Qt6::Gui openstitch::warnings)
