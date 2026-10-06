add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(bench)

# Garde structurelle Lot 8.1 : aucun consommateur de production ne doit
# appeler generate_sequence() directement (contournerait les retouches
# manuelles) -- voir tests/check_no_raw_sequence_bypass.cmake.
add_test(NAME check_no_raw_sequence_bypass
    COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_raw_sequence_bypass.cmake)

# Garde structurelle L5-T4 : l'etat de selection de MainWindow n'est ecrit que
# par le mutateur unique setSelection() -- voir
# tests/check_selection_single_mutator.cmake.
add_test(NAME check_selection_single_mutator
    COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_selection_single_mutator.cmake)

# Auto-test du garde ci-dessus (fixtures : tests/fixtures/selection_guard/) : un
# faux negatif du script doit faire echouer la CI.
add_test(NAME check_selection_guard_selftest
    COMMAND ${CMAKE_COMMAND}
            -DGUARD=${CMAKE_CURRENT_SOURCE_DIR}/check_selection_single_mutator.cmake
            -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/selection_guard
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_selection_guard_selftest.cmake)

# Garde structurelle L2 (design system v2) : aucune couleur d'interface codee en dur
# dans apps/desktop hors design_tokens.cpp ; cliquet decroissant dans
# tests/color_guard_baseline.txt (la ligne CTest n'existe que si la base existe) --
# voir tests/check_no_hardcoded_colors.cmake.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/color_guard_baseline.txt)
    add_test(NAME check_no_hardcoded_colors
        COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${PROJECT_SOURCE_DIR}
                -DBASELINE=${CMAKE_CURRENT_SOURCE_DIR}/color_guard_baseline.txt
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_hardcoded_colors.cmake)
endif()

# Auto-test du garde de couleurs (fixtures : tests/fixtures/color_guard/, LF et CRLF).
add_test(NAME check_color_guard_selftest
    COMMAND ${CMAKE_COMMAND}
            -DGUARD=${CMAKE_CURRENT_SOURCE_DIR}/check_no_hardcoded_colors.cmake
            -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color_guard
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_color_guard_selftest.cmake)
