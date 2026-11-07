# Tests du cœur Qt-free de Desktop V2 (Catch2).
add_executable(test_desktop_v2_core
    test_selection_model.cpp
    test_task_manager.cpp
    test_tool_registry.cpp
    test_session_and_pipeline.cpp
    test_preview_service.cpp
    test_hit_and_inspector.cpp
    test_command_catalog.cpp
    test_context_commands.cpp
    test_spool_rail.cpp
    test_scrub.cpp
    test_radial_menu.cpp
    test_shape_batch.cpp
    test_transform_batch.cpp
    test_edit_batch.cpp
    test_diagnostics.cpp
    test_export_formats.cpp
    test_param_spec.cpp
    test_param_completeness.cpp
    test_action_catalog.cpp
    test_view_tools.cpp
    test_document_services.cpp
    test_fade_service.cpp
    test_workspace_state.cpp
    test_arrange_batch.cpp
    test_image_prep.cpp
    test_production_sheet.cpp
    test_thread_plan.cpp
    test_node_edit.cpp
    test_live_companion.cpp
    test_fade_axis.cpp
    test_path_optimizer.cpp
    test_fabric_risk.cpp
    test_segmentation_preview.cpp
    test_snap.cpp
    test_region_edit.cpp
    test_pen_shape.cpp
    test_scenarios.cpp
    test_perf_budget.cpp
    test_density_service.cpp
    test_workflow_guide.cpp
    test_satin_column.cpp
    test_pull_calibration.cpp
    test_export_settings.cpp
    test_hoop_store.cpp
    test_thread_palette_service.cpp
    test_preferences.cpp
    test_render_settings.cpp
    test_file_io.cpp
    test_direction_guides.cpp
    test_journeys.cpp
    test_display_filters.cpp
    test_simulation_nav.cpp
    test_thread_charts.cpp
    test_point_edit.cpp
    test_source_image_tools.cpp
    test_studio_scene.cpp
    test_stage_status.cpp
)
target_link_libraries(test_desktop_v2_core
    PRIVATE openstitch::desktop_v2_core openstitch::project_ops openstitch::commands
            Catch2::Catch2WithMain openstitch::warnings)
target_compile_definitions(test_desktop_v2_core
    PRIVATE OPENSTITCH_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
            OPENSTITCH_FONTS_DIR="${PROJECT_SOURCE_DIR}/resources/fonts"
            OPENSTITCH_LEGACY_TOOLS_HEADER="${PROJECT_SOURCE_DIR}/apps/desktop/tools.hpp")
catch_discover_tests(test_desktop_v2_core)

# Tests Qt (QTest, headless) de l'interface Desktop V2.
if(TARGET openstitch::desktop_v2_ui)
    find_package(Qt6 COMPONENTS Test QUIET)
    if(Qt6Test_FOUND)
        set(CMAKE_AUTOMOC ON)
        add_executable(test_desktop_v2_shell test_shell_window.cpp)
        target_link_libraries(test_desktop_v2_shell
            PRIVATE openstitch::desktop_v2_ui Qt6::Test openstitch::warnings)
        target_compile_definitions(test_desktop_v2_shell
            PRIVATE OPENSTITCH_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
                    OPENSTITCH_FONTS_DIR="${PROJECT_SOURCE_DIR}/resources/fonts")
        add_test(NAME test_desktop_v2_shell COMMAND test_desktop_v2_shell -o -,txt)
        set_tests_properties(test_desktop_v2_shell PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()

# Qt Quick integration uses the real QML shell and the explicit CPU fallback in CI.
if(TARGET openstitch::studio_ui)
    find_package(Qt6 COMPONENTS Test QUIET)
    if(Qt6Test_FOUND)
        set(CMAKE_AUTOMOC ON)
        add_executable(test_desktop_v2_studio_shell test_studio_shell.cpp)
        target_link_libraries(test_desktop_v2_studio_shell
            PRIVATE openstitch::studio_ui Qt6::Test openstitch::warnings)
        set(studio_test_qml_root "${PROJECT_SOURCE_DIR}/apps/desktop_v2/studio")
# Main.qml instantiates every panel: each Studio test executable embeds all of them.
file(GLOB_RECURSE studio_all_qml CONFIGURE_DEPENDS "${studio_test_qml_root}/qml/*.qml")
        qt_add_resources(test_desktop_v2_studio_shell studio_test_qml PREFIX "/studio"
            BASE "${studio_test_qml_root}" FILES ${studio_all_qml}
                "${studio_test_qml_root}/qml/qmldir")
        add_executable(test_desktop_v2_studio_inspector test_studio_inspector.cpp)
        target_link_libraries(test_desktop_v2_studio_inspector
            PRIVATE openstitch::studio_ui Qt6::Test openstitch::warnings)
        qt_add_resources(test_desktop_v2_studio_inspector studio_inspector_qml PREFIX "/studio"
            BASE "${studio_test_qml_root}" FILES ${studio_all_qml}
                "${studio_test_qml_root}/qml/qmldir")
        add_test(NAME test_desktop_v2_studio_inspector COMMAND test_desktop_v2_studio_inspector -o -,txt)
        add_test(NAME test_desktop_v2_studio_shell COMMAND test_desktop_v2_studio_shell -o -,txt)
        # Design system guard rails: WCAG contrast of the Theme tokens + QML lint (no GUI needed).
        add_executable(test_desktop_v2_studio_design test_studio_design.cpp)
        target_link_libraries(test_desktop_v2_studio_design PRIVATE Qt6::Test openstitch::warnings)
        target_compile_definitions(test_desktop_v2_studio_design PRIVATE
            OPENSTITCH_STUDIO_QML_DIR="${studio_test_qml_root}/qml")
        add_test(NAME test_desktop_v2_studio_design COMMAND test_desktop_v2_studio_design -o -,txt)
        set_tests_properties(test_desktop_v2_studio_design PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        get_filename_component(studio_test_qml_imports "${Qt6_DIR}/../../../qml" ABSOLUTE)
        set_tests_properties(test_desktop_v2_studio_shell test_desktop_v2_studio_inspector PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;QML_IMPORT_PATH=${studio_test_qml_imports};QT_QPA_FONTDIR=${PROJECT_SOURCE_DIR}/resources/fonts"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        # Famille « sélection / transformations / calques » : même shell QML, parcours de l'éditeur.
        add_executable(test_desktop_v2_studio_edit test_studio_edit.cpp)
        target_link_libraries(test_desktop_v2_studio_edit
            PRIVATE openstitch::studio_ui Qt6::Test openstitch::warnings)
        set(studio_edit_qml_files "")
        foreach(qml Main Theme StudioButton StudioIcon SectionLabel ActionPalette ObjectTree
                    LayerPanel CanvasContextMenu TransformHandles EditBar FilePanel)
            list(APPEND studio_edit_qml_files "${studio_test_qml_root}/qml/${qml}.qml")
        endforeach()
        qt_add_resources(test_desktop_v2_studio_edit studio_edit_qml PREFIX "/studio"
            BASE "${studio_test_qml_root}" FILES ${studio_all_qml}
                "${studio_test_qml_root}/qml/qmldir")
        add_test(NAME test_desktop_v2_studio_edit COMMAND test_desktop_v2_studio_edit -o -,txt)
        set_tests_properties(test_desktop_v2_studio_edit PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;QML_IMPORT_PATH=${studio_test_qml_imports};QT_QPA_FONTDIR=${PROJECT_SOURCE_DIR}/resources/fonts"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        # Famille A : fichiers, imports, exports (vraie coque QML + vrais fichiers).
        add_executable(test_desktop_v2_studio_files test_studio_files.cpp)
        target_link_libraries(test_desktop_v2_studio_files
            PRIVATE openstitch::studio_ui Qt6::Test openstitch::warnings)
        qt_add_resources(test_desktop_v2_studio_files studio_files_qml PREFIX "/studio"
            BASE "${studio_test_qml_root}" FILES ${studio_all_qml}
                "${studio_test_qml_root}/qml/qmldir")
        add_test(NAME test_desktop_v2_studio_files COMMAND test_desktop_v2_studio_files -o -,txt)
        set_tests_properties(test_desktop_v2_studio_files PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;QML_IMPORT_PATH=${studio_test_qml_imports};QT_QPA_FONTDIR=${PROJECT_SOURCE_DIR}/resources/fonts"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        # Famille F : outils de dessin et d'édition vectorielle (vrai canevas, souris QTest, vrai moteur).
        add_executable(test_desktop_v2_studio_tools test_studio_tools.cpp)
        target_link_libraries(test_desktop_v2_studio_tools
            PRIVATE openstitch::studio_ui Qt6::Test openstitch::warnings)
        qt_add_resources(test_desktop_v2_studio_tools studio_tools_qml PREFIX "/studio"
            BASE "${studio_test_qml_root}" FILES ${studio_all_qml}
                "${studio_test_qml_root}/qml/qmldir")
        add_test(NAME test_desktop_v2_studio_tools COMMAND test_desktop_v2_studio_tools -o -,txt)
        set_tests_properties(test_desktop_v2_studio_tools PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;QML_IMPORT_PATH=${studio_test_qml_imports};QT_QPA_FONTDIR=${PROJECT_SOURCE_DIR}/resources/fonts"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()

# Studio view / analysis / thread adapters (QTest headless, real documents, no QML needed).
if(TARGET openstitch::studio_ui)
    find_package(Qt6 COMPONENTS Test QUIET)
    if(Qt6Test_FOUND)
        set(CMAKE_AUTOMOC ON)
        add_executable(test_desktop_v2_studio_view test_studio_view.cpp)
        target_link_libraries(test_desktop_v2_studio_view
            PRIVATE openstitch::studio_ui Qt6::Test openstitch::warnings)
        qt_add_resources(test_desktop_v2_studio_view studio_view_qml PREFIX "/studio"
            BASE "${studio_test_qml_root}" FILES ${studio_all_qml}
                "${studio_test_qml_root}/qml/qmldir")
        add_test(NAME test_desktop_v2_studio_view COMMAND test_desktop_v2_studio_view -o -,txt)
        get_filename_component(studio_view_qml_imports "${Qt6_DIR}/../../../qml" ABSOLUTE)
        set_tests_properties(test_desktop_v2_studio_view PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;QML_IMPORT_PATH=${studio_view_qml_imports};QT_QPA_FONTDIR=${PROJECT_SOURCE_DIR}/resources/fonts"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()

# Famille E : images, prétraitement, segmentation, régions (vraie coque QML + vraie image).
if(TARGET openstitch::studio_ui)
    find_package(Qt6 COMPONENTS Test QUIET)
    if(Qt6Test_FOUND)
        set(CMAKE_AUTOMOC ON)
        add_executable(test_desktop_v2_studio_image test_studio_image.cpp)
        target_link_libraries(test_desktop_v2_studio_image
            PRIVATE openstitch::studio_ui Qt6::Test openstitch::warnings)
        qt_add_resources(test_desktop_v2_studio_image studio_image_qml PREFIX "/studio"
            BASE "${studio_test_qml_root}" FILES ${studio_all_qml}
                "${studio_test_qml_root}/qml/qmldir")
        add_test(NAME test_desktop_v2_studio_image COMMAND test_desktop_v2_studio_image -o -,txt)
        get_filename_component(studio_image_qml_imports "${Qt6_DIR}/../../../qml" ABSOLUTE)
        set_tests_properties(test_desktop_v2_studio_image PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;QML_IMPORT_PATH=${studio_image_qml_imports};QT_QPA_FONTDIR=${PROJECT_SOURCE_DIR}/resources/fonts"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()

# Studio famille G (satin, fondus, retouche des points, texte) : vrai moteur, QTest headless.
if(TARGET openstitch::studio_ui)
    find_package(Qt6 COMPONENTS Test QUIET)
    if(Qt6Test_FOUND)
        set(CMAKE_AUTOMOC ON)
        add_executable(test_desktop_v2_studio_digitize test_studio_digitize.cpp)
        target_link_libraries(test_desktop_v2_studio_digitize
            PRIVATE openstitch::studio_ui openstitch::project_ops openstitch::stitch_generation
                    Qt6::Test openstitch::warnings)
        target_compile_definitions(test_desktop_v2_studio_digitize
            PRIVATE OPENSTITCH_FONTS_DIR="${PROJECT_SOURCE_DIR}/resources/fonts")
        qt_add_resources(test_desktop_v2_studio_digitize studio_digitize_qml PREFIX "/studio"
            BASE "${studio_test_qml_root}" FILES ${studio_all_qml}
                "${studio_test_qml_root}/qml/qmldir")
        # Famille I : gestes de numerisation au canevas, tirage, applique (souris QTest, vrai moteur).
        add_executable(test_desktop_v2_studio_digitize2 test_studio_digitize2.cpp)
        target_link_libraries(test_desktop_v2_studio_digitize2
            PRIVATE openstitch::studio_ui openstitch::project_ops openstitch::stitch_generation
                    openstitch::formats Qt6::Test openstitch::warnings)
        qt_add_resources(test_desktop_v2_studio_digitize2 studio_digitize2_qml PREFIX "/studio"
            BASE "${studio_test_qml_root}" FILES ${studio_all_qml}
                "${studio_test_qml_root}/qml/qmldir")
        add_test(NAME test_desktop_v2_studio_digitize2 COMMAND test_desktop_v2_studio_digitize2 -o -,txt)
        get_filename_component(studio_digitize2_qml_imports "${Qt6_DIR}/../../../qml" ABSOLUTE)
        set_tests_properties(test_desktop_v2_studio_digitize2 PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;QML_IMPORT_PATH=${studio_digitize2_qml_imports};QT_QPA_FONTDIR=${PROJECT_SOURCE_DIR}/resources/fonts"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        add_test(NAME test_desktop_v2_studio_digitize COMMAND test_desktop_v2_studio_digitize -o -,txt)
        get_filename_component(studio_digitize_qml_imports "${Qt6_DIR}/../../../qml" ABSOLUTE)
        set_tests_properties(test_desktop_v2_studio_digitize PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;QML_IMPORT_PATH=${studio_digitize_qml_imports};QT_QPA_FONTDIR=${PROJECT_SOURCE_DIR}/resources/fonts"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()

# Studio famille H : compagnon IA (faux fournisseur), palette, preferences, aide, focus clavier.
# Famille K : rendu réaliste accéléré par le GPU. Le test de logique (runs, textures, cache,
# invalidation, repli) tourne en backend logiciel ; le test du chemin RHI (WARP sous Windows)
# se saute proprement quand le backend n'est pas RHI.
if(TARGET openstitch::studio_ui)
    find_package(Qt6 COMPONENTS Test QUIET)
    if(Qt6Test_FOUND)
        set(CMAKE_AUTOMOC ON)
        add_executable(test_desktop_v2_studio_companion test_studio_companion.cpp)
        target_link_libraries(test_desktop_v2_studio_companion
            PRIVATE openstitch::studio_ui Qt6::Test openstitch::warnings)
        qt_add_resources(test_desktop_v2_studio_companion studio_companion_qml PREFIX "/studio"
            BASE "${studio_test_qml_root}" FILES ${studio_all_qml}
                "${studio_test_qml_root}/qml/qmldir")
        add_test(NAME test_desktop_v2_studio_companion COMMAND test_desktop_v2_studio_companion -o -,txt)
        get_filename_component(studio_companion_qml_imports "${Qt6_DIR}/../../../qml" ABSOLUTE)
        set_tests_properties(test_desktop_v2_studio_companion PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;QML_IMPORT_PATH=${studio_companion_qml_imports};QT_QPA_FONTDIR=${PROJECT_SOURCE_DIR}/resources/fonts"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        add_executable(test_desktop_v2_studio_realistic test_studio_realistic.cpp)
        target_link_libraries(test_desktop_v2_studio_realistic
            PRIVATE openstitch::studio_ui Qt6::Test openstitch::warnings)
        qt_add_resources(test_desktop_v2_studio_realistic studio_realistic_qml PREFIX "/studio"
            BASE "${studio_test_qml_root}" FILES ${studio_all_qml}
                "${studio_test_qml_root}/qml/qmldir")
        add_test(NAME test_desktop_v2_studio_realistic
            COMMAND test_desktop_v2_studio_realistic -o -,txt)
        get_filename_component(studio_realistic_qml_imports "${Qt6_DIR}/../../../qml" ABSOLUTE)
        set_tests_properties(test_desktop_v2_studio_realistic PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;QML_IMPORT_PATH=${studio_realistic_qml_imports};QT_QPA_FONTDIR=${PROJECT_SOURCE_DIR}/resources/fonts"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        # Chemin RHI réel (Direct3D 11, adaptateur logiciel WARP) : poste local Windows seulement.
        # En CI (variable CI), la même fonction se saute dans le test ci-dessus.
        if(WIN32 AND NOT DEFINED ENV{CI})
            add_test(NAME test_desktop_v2_studio_realistic_gpu
                COMMAND test_desktop_v2_studio_realistic
                    gpuPathBuildsOncePerRevisionAndMatchesCpuReference -o -,txt)
            set_tests_properties(test_desktop_v2_studio_realistic_gpu PROPERTIES
                ENVIRONMENT "OPENSTITCH_RHI_TEST=1;QT_QPA_PLATFORM=windows;QSG_RHI_BACKEND=d3d11;QSG_RHI_PREFER_SOFTWARE_RENDERER=1;QT_QUICK_CONTROLS_STYLE=Basic;QML_IMPORT_PATH=${studio_realistic_qml_imports};QT_QPA_FONTDIR=${PROJECT_SOURCE_DIR}/resources/fonts"
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
                WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        endif()
    endif()
endif()

# Studio V0 : charpente des quatre espaces (vrai Main.qml, SpaceHost, WorkspaceController).
if(TARGET openstitch::studio_ui)
    find_package(Qt6 COMPONENTS Test QUIET)
    if(Qt6Test_FOUND)
        set(CMAKE_AUTOMOC ON)
        add_executable(test_desktop_v2_studio_workspaces test_studio_workspaces.cpp)
        target_link_libraries(test_desktop_v2_studio_workspaces
            PRIVATE openstitch::studio_ui Qt6::Test openstitch::warnings)
        qt_add_resources(test_desktop_v2_studio_workspaces studio_workspaces_qml PREFIX "/studio"
            BASE "${studio_test_qml_root}" FILES ${studio_all_qml}
                "${studio_test_qml_root}/qml/qmldir")
        add_test(NAME test_desktop_v2_studio_workspaces COMMAND test_desktop_v2_studio_workspaces -o -,txt)
        get_filename_component(studio_workspaces_qml_imports "${Qt6_DIR}/../../../qml" ABSOLUTE)
        set_tests_properties(test_desktop_v2_studio_workspaces PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;QML_IMPORT_PATH=${studio_workspaces_qml_imports};QT_QPA_FONTDIR=${PROJECT_SOURCE_DIR}/resources/fonts"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()

# Studio V01 espace « Image » (vrai Main.qml, vraie image, vraie couche sourceImage).
if(TARGET openstitch::studio_ui)
    find_package(Qt6 COMPONENTS Test QUIET)
    if(Qt6Test_FOUND)
        set(CMAKE_AUTOMOC ON)
        add_executable(test_desktop_v2_studio_space_image test_studio_space_image.cpp)
        target_link_libraries(test_desktop_v2_studio_space_image
            PRIVATE openstitch::studio_ui Qt6::Test openstitch::warnings)
        qt_add_resources(test_desktop_v2_studio_space_image studio_space_image_qml PREFIX "/studio"
            BASE "${studio_test_qml_root}" FILES ${studio_all_qml}
                "${studio_test_qml_root}/qml/qmldir")
        add_test(NAME test_desktop_v2_studio_space_image COMMAND test_desktop_v2_studio_space_image -o -,txt)
        get_filename_component(studio_space_image_qml_imports "${Qt6_DIR}/../../../qml" ABSOLUTE)
        set_tests_properties(test_desktop_v2_studio_space_image PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;QML_IMPORT_PATH=${studio_space_image_qml_imports};QT_QPA_FONTDIR=${PROJECT_SOURCE_DIR}/resources/fonts"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()