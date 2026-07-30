find_package(Qt6 COMPONENTS Widgets QUIET)
if(NOT Qt6_FOUND)
    message(WARNING
        "Qt6 introuvable : l'application desktop ne sera pas construite. "
        "Définissez la variable d'environnement QT_ROOT (ex. C:/Qt/6.8.3/msvc2022_64).")
    return()
endif()

set(CMAKE_AUTOMOC ON)

add_executable(openstitch WIN32
    main.cpp
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
)
target_link_libraries(openstitch
    PRIVATE Qt6::Widgets openstitch::core openstitch::image openstitch::document
            openstitch::commands openstitch::vectorization openstitch::stitch
            openstitch::stitch_generation openstitch::formats openstitch::project_io
            openstitch::stitch_analysis openstitch::optimization openstitch::autodigitize
            openstitch::warnings
)

# Copie les DLL Qt à côté de l'exécutable pour pouvoir le lancer directement.
# (Les DLL vcpkg — OpenCV, etc. — sont déjà copiées par VCPKG_APPLOCAL_DEPS.)
if(WIN32 AND TARGET Qt6::windeployqt)
    add_custom_command(TARGET openstitch POST_BUILD
        COMMAND Qt6::windeployqt --no-translations --no-system-d3d-compiler
                "$<TARGET_FILE:openstitch>"
        COMMENT "Déploiement des DLL Qt (windeployqt)")
endif()
