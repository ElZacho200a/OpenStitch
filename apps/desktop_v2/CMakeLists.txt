# OpenStitch Desktop V2 (docs/desktop-v2/). Coexiste avec apps/desktop jusqu'à la parité
# fonctionnelle démontrée (docs/desktop-v2/06-plan-migration.md).
#
# `core` : couche Qt-FREE (session documentaire, sélection, tâches, registre d'outils, chaîne de
# numérisation). Construite et testée partout, y compris en CI `linux-core` : c'est la garantie que
# la logique de l'application ne dépend d'aucun widget.
add_library(openstitch_desktop_v2_core STATIC
    core/selection_model.cpp
    core/task_manager.cpp
    core/document_session.cpp
    core/tool_registry.cpp
    core/digitize_pipeline.cpp
    core/preview_service.cpp
    core/object_labels.cpp
    core/hit_index.cpp
    core/inspector_model.cpp
    core/command_catalog.cpp
    core/shape_batch.cpp
    core/transform_batch.cpp
    core/edit_batch.cpp
    core/diagnostics_service.cpp
    core/live_companion.cpp
    core/fade_axis.cpp
    core/path_optimizer.cpp
    core/fabric_risk.cpp
    core/segmentation_preview.cpp
    core/snap.cpp
    core/node_edit.cpp
    core/density_service.cpp
    core/param_spec.cpp
    core/action_catalog.cpp
    core/view_tools.cpp
    core/document_services.cpp
    core/thread_plan.cpp
    core/workflow_guide.cpp
    core/fade_service.cpp
    core/workspace_state.cpp
    core/arrange_batch.cpp
    core/image_prep.cpp
    core/production_sheet.cpp
    core/satin_column.cpp
    core/point_edit.cpp
    core/source_image_tools.cpp
)
add_library(openstitch::desktop_v2_core ALIAS openstitch_desktop_v2_core)
target_include_directories(openstitch_desktop_v2_core PUBLIC core)
# Police de développement : repli de l'action « Ajouter du texte » quand rien d'autre n'est trouvé.
target_compile_definitions(openstitch_desktop_v2_core PRIVATE
    OPENSTITCH_DEFAULT_FONT="${PROJECT_SOURCE_DIR}/resources/fonts/VeraBd.ttf")
target_link_libraries(openstitch_desktop_v2_core
    PUBLIC openstitch::core openstitch::document openstitch::commands openstitch::project_ops
           openstitch::stitch openstitch::stitch_generation openstitch::stitch_render openstitch::formats openstitch::image
           openstitch::mcp openstitch::optimization openstitch::stitch_analysis openstitch::autodigitize openstitch::segmentation openstitch::stipple openstitch::project_io
    PRIVATE openstitch::warnings)

# Interface Qt Widgets. Désactivée avec OPENSTITCH_BUILD_DESKTOP=OFF (CI linux-core) ou sans Qt6.
if(NOT OPENSTITCH_BUILD_DESKTOP)
    return()
endif()
find_package(Qt6 COMPONENTS Widgets Network QUIET)
if(NOT Qt6_FOUND)
    return()
endif()

set(CMAKE_AUTOMOC ON)
add_library(openstitch_desktop_v2_ui STATIC
    ui/shell_window.cpp
    ui/shell_window.hpp
    ui/feedback_cue.cpp
    ui/feedback_cue.hpp
    ui/preview_canvas.cpp
    ui/tool_icon.cpp
    ui/tool_icon.hpp
    ui/inspector_panel.cpp
    ui/calque_panel.cpp
    ui/calque_panel.hpp
    ui/thread_panel.cpp
    ui/import_dialog.cpp
    ui/import_dialog.hpp
    ui/optimize_panel.cpp
    ui/optimize_panel.hpp
    ui/companion_panel.cpp
    ui/companion_panel.hpp
    ui/live_server.cpp
    ui/live_server.hpp
    ui/thread_panel.hpp
    ui/welcome_page.cpp
    ui/welcome_page.hpp
    ui/action_dialog.cpp
    ui/action_dialog.hpp
    ui/command_palette.cpp
    ui/diagnostics_panel.cpp
    ui/diagnostics_panel.hpp
    ui/command_palette.hpp
    ui/inspector_panel.hpp
    ui/preview_canvas.hpp
)
add_library(openstitch::desktop_v2_ui ALIAS openstitch_desktop_v2_ui)
target_include_directories(openstitch_desktop_v2_ui PUBLIC ui)
target_link_libraries(openstitch_desktop_v2_ui
    PUBLIC Qt6::Widgets Qt6::Network openstitch::desktop_v2_core openstitch::project_io
    PRIVATE openstitch::warnings)

add_executable(openstitch_v2 WIN32 ui/main.cpp)
target_link_libraries(openstitch_v2 PRIVATE openstitch::desktop_v2_ui openstitch::warnings)
