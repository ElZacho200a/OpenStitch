add_subdirectory(unit)
add_subdirectory(integration)

# Garde structurelle Lot 8.1 : aucun consommateur de production ne doit
# appeler generate_sequence() directement (contournerait les retouches
# manuelles) -- voir tests/check_no_raw_sequence_bypass.cmake.
add_test(NAME check_no_raw_sequence_bypass
    COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_raw_sequence_bypass.cmake)
