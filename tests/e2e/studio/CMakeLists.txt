if(NOT TARGET openstitch::studio_ui OR NOT TARGET Qt6::Test)
    return()
endif()

set(CMAKE_AUTOMOC ON)
file(GLOB studio_e2e_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/journey_*.cpp")
add_executable(test_studio_e2e main.cpp journey_harness.cpp journey_harness.hpp ${studio_e2e_sources})
target_link_libraries(test_studio_e2e PRIVATE openstitch::studio_ui Qt6::Test openstitch::warnings)
target_compile_definitions(test_studio_e2e PRIVATE
    OPENSTITCH_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
    OPENSTITCH_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden")
set(studio_e2e_qml_root "${PROJECT_SOURCE_DIR}/apps/desktop_v2/studio")
file(GLOB studio_e2e_qml CONFIGURE_DEPENDS "${studio_e2e_qml_root}/qml/*.qml")
qt_add_resources(test_studio_e2e studio_e2e_qml PREFIX "/studio"
    BASE "${studio_e2e_qml_root}" FILES ${studio_e2e_qml} "${studio_e2e_qml_root}/qml/qmldir")
get_filename_component(studio_e2e_imports "${Qt6_DIR}/../../../qml" ABSOLUTE)
foreach(journey_file IN LISTS studio_e2e_sources)
    get_filename_component(journey_name "${journey_file}" NAME_WE)
    string(REGEX REPLACE "^journey_([0-9]+)_.*" "\\1" journey_code "${journey_name}")
    add_test(NAME "studio_e2e_${journey_name}" COMMAND test_studio_e2e -o -,txt)
    set_tests_properties("studio_e2e_${journey_name}" PROPERTIES
        LABELS "studio;e2e" TIMEOUT 180
        ENVIRONMENT "STUDIO_E2E_JOURNEY=${journey_code};QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;QML_IMPORT_PATH=${studio_e2e_imports};QT_QPA_FONTDIR=${PROJECT_SOURCE_DIR}/resources/fonts"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endforeach()
