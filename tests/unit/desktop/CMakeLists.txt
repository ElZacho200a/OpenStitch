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
    add_test(NAME ${name} COMMAND ${name})
    # Qt (hors vcpkg, cf. QT_ROOT) n'est déployé à côté de l'exécutable que
    # pour la cible `openstitch` (windeployqt) ; ici on ajoute simplement son
    # dossier bin au PATH plutôt que de dupliquer les DLL pour chaque test.
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endfunction()

openstitch_add_qt_test(test_canvas_view test_canvas_view.cpp)
openstitch_add_qt_test(test_node_handle test_node_handle.cpp)
openstitch_add_qt_test(test_document_panel test_document_panel.cpp)
openstitch_add_qt_test(test_properties_panel test_properties_panel.cpp)
openstitch_add_qt_test(test_main_window test_main_window.cpp)
openstitch_add_qt_test(test_ui_invariants test_ui_invariants.cpp)
openstitch_add_qt_test(test_ui_adversarial test_ui_adversarial.cpp)
openstitch_add_qt_test(test_ui_characterization test_ui_characterization.cpp)
openstitch_add_qt_test(test_canvas_input test_canvas_input.cpp)
openstitch_add_qt_test(test_interaction_map test_interaction_map.cpp)
openstitch_add_qt_test(test_help_dialogs test_help_dialogs.cpp)
openstitch_add_qt_test(test_selection_hit_test test_selection_hit_test.cpp)
openstitch_add_qt_test(test_wsl_path_converter test_wsl_path_converter.cpp)
openstitch_add_qt_test(test_generation_options test_generation_options.cpp)

# Design system v2 (lot L2) : tokens/contraste (pur), QSS+palette, application réelle.
openstitch_add_qt_test(test_design_tokens test_design_tokens.cpp)
openstitch_add_qt_test(test_app_style test_app_style.cpp)
openstitch_add_qt_test(test_design_system test_design_system.cpp)

# Variantes « thémées » des 4 suites d'interface existantes (plan L2 §5.7) : mêmes
# exécutables, mais Fusion + QSS + palette appliqués par theme_test_support.hpp via
# OPENSTITCH_TEST_APPLY_THEME. Les assertions dépendantes de la géométrie doivent
# rester vertes dans les deux thèmes et les deux densités.
foreach(suite test_main_window test_ui_invariants test_ui_adversarial test_ui_characterization)
    foreach(combo light-comfortable dark-compact)
        string(REPLACE "-" "_" combo_id "${combo}")
        add_test(NAME ${suite}_themed_${combo_id} COMMAND ${suite})
        set_tests_properties(${suite}_themed_${combo_id} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;OPENSTITCH_TEST_APPLY_THEME=${combo}"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
            LABELS themed)
    endforeach()
endforeach()

# Instantanés headless (plan L2 §4) : écrit des PNG pour relecture humaine dans
# OPENSTITCH_UI_SNAPSHOT_DIR (sinon dossier temporaire : simple test de fumée).
# Aucune comparaison de pixels. Une seconde entrée rejoue à l'échelle 2x.
openstitch_add_qt_test(test_ui_snapshot test_ui_snapshot.cpp)
set_property(TEST test_ui_snapshot APPEND PROPERTY LABELS snapshot)
add_test(NAME test_ui_snapshot_hidpi COMMAND test_ui_snapshot)
set_tests_properties(test_ui_snapshot_hidpi PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=2"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    LABELS snapshot)

# Test d'intégration SamWorkerClient <-> faux worker Python (stdlib only,
# aucune dépendance à un vrai environnement SAM 2/torch en CI).
openstitch_add_qt_test(test_sam_worker_client test_sam_worker_client.cpp)
target_compile_definitions(test_sam_worker_client PRIVATE
    OPENSTITCH_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
