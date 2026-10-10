# Tests de contrat de openstitch-cli (options, erreurs, codes de sortie, --json).
# Un cas = un test CTest ; le script check_cli.cmake fait les vérifications.
set(OPENSTITCH_CLI_CASES
    version help unknown_shape list_shapes usage_errors stitchdebug errors_prefix
    info_json digitize_json digitize_nothing legacy_technique_alias)

foreach(case IN LISTS OPENSTITCH_CLI_CASES)
    add_test(NAME cli_${case}
        COMMAND ${CMAKE_COMMAND}
                -DCLI=$<TARGET_FILE:openstitch-cli>
                -DFIXTURES=${PROJECT_SOURCE_DIR}/tests/fixtures/cli
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/work_${case}
                -DCASE=${case}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_cli.cmake)
    set_tests_properties(cli_${case} PROPERTIES TIMEOUT 120)
endforeach()
