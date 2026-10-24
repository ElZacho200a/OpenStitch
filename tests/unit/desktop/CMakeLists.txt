# Tests Qt (QTest) de la couche UI desktop. Ne se construisent que si
# l'application desktop l'a été (Qt6 trouvé, cf. apps/desktop/CMakeLists.txt) ;
# openstitch_desktop_widgets porte tous les widgets sauf main().
if(NOT TARGET openstitch_desktop_widgets)
    return()
endif()

# Dérive documentation <-> table des gestes (lot L5) : la sortie du générateur
# doit être identique, octet pour octet, au bloc GESTURES de user-guide.md.
add_test(NAME docs_gestures_in_sync
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:openstitch_gesture_table>
            -DDOC=${PROJECT_SOURCE_DIR}/docs/source/user-guide.md
            -P ${PROJECT_SOURCE_DIR}/tests/check_gesture_docs.cmake)
set_tests_properties(docs_gestures_in_sync PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

find_package(Qt6 COMPONENTS Test QUIET)
if(NOT Qt6Test_FOUND)
    message(WARNING "Qt6::Test introuvable : tests UI desktop non construits.")
    return()
endif()

set(CMAKE_AUTOMOC ON)

# Un exécutable QTest par suite ; exécution headless forcée (aucun écran requis
# en CI ni en local) via la variable d'environnement QT_QPA_PLATFORM.
function(openstitch_add_qt_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE openstitch::desktop_widgets Qt6::Test openstitch::warnings)
    # Force text output to stdout, including Windows runners without a console.
    add_test(NAME ${name} COMMAND ${name} -o -,txt)
    # Qt (hors vcpkg, cf. QT_ROOT) n'est déployé à côté de l'exécutable que
    # pour la cible `openstitch` (windeployqt) ; ici on ajoute simplement son
    # dossier bin au PATH plutôt que de dupliquer les DLL pour chaque test.
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endfunction()

openstitch_add_qt_test(test_canvas_view test_canvas_view.cpp)
openstitch_add_qt_test(test_node_handle test_node_handle.cpp)
openstitch_add_qt_test(test_segment_reshape test_segment_reshape.cpp)
openstitch_add_qt_test(test_node_selection test_node_selection.cpp)
openstitch_add_qt_test(test_brush_tool test_brush_tool.cpp)
openstitch_add_qt_test(test_document_panel test_document_panel.cpp)
openstitch_add_qt_test(test_properties_panel test_properties_panel.cpp)
openstitch_add_qt_test(test_segmentation_colors_panel test_segmentation_colors_panel.cpp)
openstitch_add_qt_test(test_segmentation_dialog test_segmentation_dialog.cpp)
openstitch_add_qt_test(test_main_window test_main_window.cpp)
openstitch_add_qt_test(test_color_blend test_color_blend.cpp)
openstitch_add_qt_test(test_calques test_calques.cpp)
openstitch_add_qt_test(test_color_blend_dialog test_color_blend_dialog.cpp)
openstitch_add_qt_test(test_tool_workflows test_tool_workflows.cpp)
openstitch_add_qt_test(test_ui_action_workflows test_ui_action_workflows.cpp)
openstitch_add_qt_test(test_ui_invariants test_ui_invariants.cpp)
openstitch_add_qt_test(test_ui_adversarial test_ui_adversarial.cpp)
openstitch_add_qt_test(test_ui_characterization test_ui_characterization.cpp)
openstitch_add_qt_test(test_ux_global test_ux_global.cpp)
openstitch_add_qt_test(test_workspace_presets test_workspace_presets.cpp)
openstitch_add_qt_test(test_command_palette test_command_palette.cpp)
openstitch_add_qt_test(test_creation_flow test_creation_flow.cpp)
openstitch_add_qt_test(test_canvas_input test_canvas_input.cpp)
openstitch_add_qt_test(test_interaction_map test_interaction_map.cpp)
openstitch_add_qt_test(test_app_settings test_app_settings.cpp)
openstitch_add_qt_test(test_app_language test_app_language.cpp)
openstitch_add_qt_test(test_help_dialogs test_help_dialogs.cpp)
openstitch_add_qt_test(test_selection_hit_test test_selection_hit_test.cpp)
openstitch_add_qt_test(test_wsl_path_converter test_wsl_path_converter.cpp)
openstitch_add_qt_test(test_generation_options test_generation_options.cpp)
openstitch_add_qt_test(test_segmentation_workflow test_segmentation_workflow.cpp)
openstitch_add_qt_test(test_realistic_view test_realistic_view.cpp)
openstitch_add_qt_test(test_production_dialog test_production_dialog.cpp)
openstitch_add_qt_test(test_thread_panel test_thread_panel.cpp)
openstitch_add_qt_test(test_lettering_ui test_lettering_ui.cpp)
openstitch_add_qt_test(test_analysis_fix test_analysis_fix.cpp)
openstitch_add_qt_test(test_ui_responsive test_ui_responsive.cpp)
openstitch_add_qt_test(test_workflow_ux test_workflow_ux.cpp)
openstitch_add_qt_test(test_thread_strip test_thread_strip.cpp)

# Test d'intégration SamWorkerClient <-> faux worker Python (stdlib only,
# aucune dépendance à un vrai environnement SAM 2/torch en CI).
openstitch_add_qt_test(test_sam_worker_client test_sam_worker_client.cpp)
target_compile_definitions(test_sam_worker_client PRIVATE
    OPENSTITCH_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
openstitch_add_qt_test(test_machine_export_dialog test_machine_export_dialog.cpp)

# Tests du câblage des entrées livrées en cœur (une zone par lot).
# [cablage-A:debut]
openstitch_add_qt_test(test_export_image test_export_image.cpp)
# [cablage-A:fin]
# [cablage-B:debut]
openstitch_add_qt_test(test_vector_tools test_vector_tools.cpp)
# [cablage-B:fin]
# [cablage-C:debut]
openstitch_add_qt_test(test_measure_tool test_measure_tool.cpp)
openstitch_add_qt_test(test_eyedropper_tool test_eyedropper_tool.cpp)
openstitch_add_qt_test(test_bucket_tool test_bucket_tool.cpp)
openstitch_add_qt_test(test_click_to_stitch_tool test_click_to_stitch_tool.cpp)
# [cablage-C:fin]
# [cablage-D:debut]
openstitch_add_qt_test(test_thread_stock_ui test_thread_stock_ui.cpp)
openstitch_add_qt_test(test_text_typography_ui test_text_typography_ui.cpp)
# [cablage-D:fin]
# [cablage-E:debut]
openstitch_add_qt_test(test_density_map_ui test_density_map_ui.cpp)
# [cablage-E:fin]
