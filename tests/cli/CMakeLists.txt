# Tests de contrat de openstitch-cli (options, erreurs, codes de sortie, --json).
# Un cas = un test CTest ; le script check_cli.cmake fait les vérifications.
set(OPENSTITCH_CLI_CASES
    version help unknown_shape list_shapes usage_errors stitchdebug errors_prefix
    info_json digitize_json digitize_nothing legacy_technique_alias
    project_new_inspect project_cycle project_introspection ai_backends render_png)

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

# HP-QA-006 : corpus image synthetique et smoke import sur le vrai CLI.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME qa_image_corpus_unit
        COMMAND "${Python3_EXECUTABLE}" -m unittest discover
                -s "${CMAKE_SOURCE_DIR}/scripts/qa_images" -p "test_*.py")
    add_test(NAME cli_qa_image_corpus
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/scripts/qa_images/image_corpus.py"
                --cli "$<TARGET_FILE:openstitch-cli>")
    set_tests_properties(qa_image_corpus_unit cli_qa_image_corpus PROPERTIES TIMEOUT 60)
endif()
