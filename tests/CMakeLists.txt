add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(bench)
add_subdirectory(cli)

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
