# Experimental Qt Quick shell; core and the Widgets executable remain independently buildable.
find_package(Qt6 6.8 COMPONENTS Quick QuickControls2 Qml Network QUIET)
if(NOT TARGET Qt6::Quick OR NOT TARGET Qt6::QuickControls2 OR NOT TARGET Qt6::Network OR NOT TARGET openstitch::companion_backends)
    message(STATUS "OpenStitch Studio disabled: Qt 6.8 Quick / QuickControls2 not available")
    return()
endif()

set(CMAKE_AUTOMOC ON)
add_library(openstitch_studio_ui STATIC
    studio_controller.cpp perf_log.cpp studio_controller.hpp
    usage_controller.cpp usage_controller.hpp
    history_controller.cpp history_controller.hpp
    action_controller.cpp action_controller.hpp
    edit_controller.cpp edit_controller.hpp
    file_controller.cpp file_controller.hpp
    inspector_controller.cpp inspector_controller.hpp
    gpu_canvas.cpp gpu_canvas.hpp
    frame_probe.cpp frame_probe.hpp
    canvas_renderer.cpp canvas_renderer.hpp
    realistic_scene.cpp realistic_scene.hpp
    thread_textures.cpp thread_textures.hpp
    view_controller.cpp view_controller.hpp
    analysis_controller.cpp analysis_controller.hpp
    thread_controller.cpp thread_controller.hpp
    image_controller.cpp image_controller.hpp
    region_controller.cpp region_controller.hpp
    satin_controller.cpp satin_controller.hpp
    fade_controller.cpp fade_controller.hpp
    point_edit_controller.cpp point_edit_controller.hpp
    tool_controller.cpp tool_controller.hpp
    warp_tool.cpp warp_tool.hpp
    bucket_tool.cpp bucket_tool.hpp
    vector_controller.cpp vector_controller.hpp
    workspace_controller.cpp workspace_controller.hpp
    vector_space_controller.cpp vector_space_controller.hpp
    layers/vector_layer.cpp layers/vector_layer.hpp
    companion_controller.cpp companion_controller.hpp
    preferences_controller.cpp preferences_controller.hpp
    help_controller.cpp help_controller.hpp
    digitize_gestures.cpp digitize_gestures.hpp
    pull_controller.cpp pull_controller.hpp
    applique_controller.cpp applique_controller.hpp
    fix_controller.cpp fix_controller.hpp
    image_space_controller.cpp image_space_controller.hpp
    layers/source_image_layer.cpp layers/source_image_layer.hpp
    points_space_controller.cpp points_space_controller.hpp
    segmentation_space_controller.cpp segmentation_space_controller.hpp
    layers/region_layer.cpp layers/region_layer.hpp)
add_library(openstitch::studio_ui ALIAS openstitch_studio_ui)
target_include_directories(openstitch_studio_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openstitch_studio_ui PUBLIC openstitch::desktop_v2_core
    openstitch::ai_segmentation openstitch::companion_backends
    openstitch::desktop_v2_live Qt6::Network Qt6::Quick
    Qt6::QuickControls2 PRIVATE openstitch::warnings)

set(studio_qml_files
    qml/Main.qml qml/HistoryPanel.qml qml/Theme.qml qml/StudioButton.qml qml/StudioIcon.qml
    qml/StudioLabel.qml qml/StudioPanel.qml qml/StudioToolTip.qml qml/StudioTextField.qml
    qml/StudioCheckBox.qml qml/StudioSwitch.qml qml/StudioSlider.qml qml/StudioSpinBox.qml
    qml/StudioComboBox.qml
    qml/SectionLabel.qml qml/ActionPalette.qml qml/ObjectTree.qml qml/LayerPanel.qml
    qml/CanvasContextMenu.qml qml/TransformHandles.qml qml/EditBar.qml qml/FilePanel.qml
    qml/Inspector.qml qml/ParamField.qml qml/ParamSection.qml
    qml/ViewPanel.qml qml/SimulationBar.qml qml/AnalysisPanel.qml qml/ThreadPanel.qml
    qml/PreparePanel.qml qml/ImagePrepSection.qml qml/SegmentationSection.qml
    qml/RegionTools.qml qml/AiSection.qml qml/CompanionPanel.qml qml/PreferencesDialog.qml
    qml/HelpDialog.qml qml/ShortcutsSheet.qml
    qml/SatinPanel.qml qml/FadePanel.qml qml/FadeProperties.qml qml/PointEditPanel.qml qml/DigitizeHub.qml qml/ToolRail.qml qml/ToolOptions.qml qml/ToolOverlay.qml qml/RealisticToggle.qml qml/HandlesOverlay.qml qml/PullPanel.qml qml/AppliquePanel.qml qml/FixPanel.qml qml/qmldir
    qml/spaces/SpaceHost.qml qml/spaces/ImageSpace.qml qml/spaces/SegmentationSpace.qml
    qml/spaces/VectorSpace.qml qml/spaces/PointsSpace.qml)
list(APPEND studio_qml_files qml/spaces/vector/VectorShapePanel.qml)
# Les sous-composants d'un espace (qml/spaces/<x>/...) s'ajoutent ICI, une ligne par fichier.
list(APPEND studio_qml_files qml/spaces/image/ImageOpStack.qml qml/spaces/image/CompareCurtain.qml
    qml/spaces/image/CropOverlay.qml qml/spaces/image/ImageSizeSection.qml)
list(APPEND studio_qml_files qml/spaces/segmentation/SegmentationPanels.qml qml/spaces/segmentation/RegionLayerControls.qml)
list(APPEND studio_qml_files
    qml/spaces/points/PointsToolRail.qml qml/spaces/points/PointsPanelBar.qml
    qml/spaces/points/PointsOptionsBar.qml qml/spaces/points/PointsObjectsPanel.qml)

add_executable(openstitch_v2_studio WIN32 main.cpp)
# Studio est l'application par défaut : openstitch.exe (la V1 devient openstitch-v1.exe).
set_target_properties(openstitch_v2_studio PROPERTIES OUTPUT_NAME openstitch)
target_link_libraries(openstitch_v2_studio PRIVATE openstitch::studio_ui openstitch::warnings)
qt_add_resources(openstitch_v2_studio studio_qml PREFIX "/studio" FILES ${studio_qml_files})

if(WIN32 AND TARGET Qt6::windeployqt)
    add_custom_command(TARGET openstitch_v2_studio POST_BUILD
        COMMAND Qt6::windeployqt --no-translations --no-system-d3d-compiler
            --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml" "$<TARGET_FILE:openstitch_v2_studio>"
        COMMENT "Deploy Qt Quick Studio runtime and QML imports")
endif()
