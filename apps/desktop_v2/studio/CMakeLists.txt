# Experimental Qt Quick shell; core and the Widgets executable remain independently buildable.
find_package(Qt6 6.8 COMPONENTS Quick QuickControls2 Qml QUIET)
if(NOT TARGET Qt6::Quick OR NOT TARGET Qt6::QuickControls2)
    message(STATUS "OpenStitch Studio disabled: Qt 6.8 Quick / QuickControls2 not available")
    return()
endif()

set(CMAKE_AUTOMOC ON)
add_library(openstitch_studio_ui STATIC
    studio_controller.cpp studio_controller.hpp
    action_controller.cpp action_controller.hpp
    edit_controller.cpp edit_controller.hpp
    file_controller.cpp file_controller.hpp
    inspector_controller.cpp inspector_controller.hpp
    gpu_canvas.cpp gpu_canvas.hpp
    canvas_renderer.cpp canvas_renderer.hpp
    compare_controller.cpp compare_controller.hpp
    view_controller.cpp view_controller.hpp
    analysis_controller.cpp analysis_controller.hpp
    thread_controller.cpp thread_controller.hpp
    image_controller.cpp image_controller.hpp
    region_controller.cpp region_controller.hpp
    satin_controller.cpp satin_controller.hpp
    fade_controller.cpp fade_controller.hpp
    point_edit_controller.cpp point_edit_controller.hpp
    tool_controller.cpp tool_controller.hpp
    vector_controller.cpp vector_controller.hpp)
add_library(openstitch::studio_ui ALIAS openstitch_studio_ui)
target_include_directories(openstitch_studio_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openstitch_studio_ui PUBLIC openstitch::desktop_v2_core
    openstitch::ai_segmentation Qt6::Quick
    Qt6::QuickControls2 PRIVATE openstitch::warnings)

set(studio_qml_files
    qml/Main.qml qml/Theme.qml qml/StudioButton.qml qml/StudioIcon.qml
    qml/SectionLabel.qml qml/ActionPalette.qml qml/ObjectTree.qml qml/LayerPanel.qml
    qml/CanvasContextMenu.qml qml/TransformHandles.qml qml/EditBar.qml qml/FilePanel.qml
    qml/Inspector.qml qml/ParamField.qml qml/ParamSection.qml
    qml/ComparePanel.qml
    qml/ViewPanel.qml qml/SimulationBar.qml qml/AnalysisPanel.qml qml/ThreadPanel.qml
    qml/PreparePanel.qml qml/ImagePrepSection.qml qml/SegmentationSection.qml
    qml/RegionTools.qml qml/AiSection.qml 
    qml/SatinPanel.qml qml/FadePanel.qml qml/PointEditPanel.qml qml/DigitizeHub.qml qml/ToolRail.qml qml/ToolOptions.qml qml/ToolOverlay.qml qml/qmldir)

add_executable(openstitch_v2_studio WIN32 main.cpp)
target_link_libraries(openstitch_v2_studio PRIVATE openstitch::studio_ui openstitch::warnings)
qt_add_resources(openstitch_v2_studio studio_qml PREFIX "/studio" FILES ${studio_qml_files})

if(WIN32 AND TARGET Qt6::windeployqt)
    add_custom_command(TARGET openstitch_v2_studio POST_BUILD
        COMMAND Qt6::windeployqt --no-translations --no-system-d3d-compiler
            --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml" "$<TARGET_FILE:openstitch_v2_studio>"
        COMMENT "Deploy Qt Quick Studio runtime and QML imports")
endif()
