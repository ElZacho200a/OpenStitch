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
    gpu_canvas.cpp gpu_canvas.hpp
    canvas_renderer.cpp canvas_renderer.hpp)
add_library(openstitch::studio_ui ALIAS openstitch_studio_ui)
target_include_directories(openstitch_studio_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openstitch_studio_ui PUBLIC openstitch::desktop_v2_core Qt6::Quick
    Qt6::QuickControls2 PRIVATE openstitch::warnings)

set(studio_qml_files
    qml/Main.qml qml/Theme.qml qml/StudioButton.qml qml/StudioIcon.qml
    qml/SectionLabel.qml qml/ActionPalette.qml qml/ObjectTree.qml qml/LayerPanel.qml
    qml/CanvasContextMenu.qml qml/TransformHandles.qml qml/EditBar.qml qml/qmldir)

add_executable(openstitch_v2_studio WIN32 main.cpp)
target_link_libraries(openstitch_v2_studio PRIVATE openstitch::studio_ui openstitch::warnings)
qt_add_resources(openstitch_v2_studio studio_qml PREFIX "/studio" FILES ${studio_qml_files})

if(WIN32 AND TARGET Qt6::windeployqt)
    add_custom_command(TARGET openstitch_v2_studio POST_BUILD
        COMMAND Qt6::windeployqt --no-translations --no-system-d3d-compiler
            --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml" "$<TARGET_FILE:openstitch_v2_studio>"
        COMMENT "Deploy Qt Quick Studio runtime and QML imports")
endif()
