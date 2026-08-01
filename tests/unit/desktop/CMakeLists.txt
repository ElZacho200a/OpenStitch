# Tests Qt (QTest) de la couche UI desktop. Ne se construisent que si
# l'application desktop l'a été (Qt6 trouvé, cf. apps/desktop/CMakeLists.txt) ;
# openstitch_desktop_widgets porte tous les widgets sauf main().
if(NOT TARGET openstitch_desktop_widgets)
    return()
endif()

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
