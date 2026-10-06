# Auto-test de tests/check_selection_single_mutator.cmake : une regression du
# script lui-meme (faux negatif) doit faire echouer la CI.
#  - clean.cpp.txt : aucun usage interdit -> le garde doit PASSER ;
#  - violations.cpp.txt : chaque ligne marquee `VIOLATION` doit etre signalee
#    `main_window.cpp:<ligne>` et AUCUNE autre ligne ne doit l'etre.
# Chaque fixture est aussi rejouee en fins de ligne CRLF (meme resultat attendu).
# Sans Qt. Usage : cmake -DGUARD=<script> -DFIXTURES=<dossier> -DWORK_DIR=<tmp> -P ...

cmake_minimum_required(VERSION 3.21)

foreach(var GUARD FIXTURES WORK_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "${var} non defini")
    endif()
endforeach()

# Lance le garde sur un faux depot ne contenant que apps/desktop/main_window.cpp.
function(run_guard fixture eol out_rc out_text)
    set(root "${WORK_DIR}/selection_guard_${eol}")
    file(REMOVE_RECURSE "${root}")
    file(MAKE_DIRECTORY "${root}/apps/desktop")
    file(READ "${FIXTURES}/${fixture}" text)
    string(REPLACE "\r" "" text "${text}")
    if(eol STREQUAL "crlf")
        string(REPLACE "\n" "\r\n" text "${text}")
    endif()
    file(WRITE "${root}/apps/desktop/main_window.cpp" "${text}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -DSRC_DIR=${root} -P "${GUARD}"
        RESULT_VARIABLE rc OUTPUT_VARIABLE so ERROR_VARIABLE se)
    set(${out_rc} ${rc} PARENT_SCOPE)
    set(${out_text} "${so}\n${se}" PARENT_SCOPE)
endfunction()

foreach(eol lf crlf)
    # --- fixture propre
    run_guard(clean.cpp.txt ${eol} rc text)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "[${eol}] clean.cpp.txt : le garde devrait PASSER (faux positif) :\n${text}")
    endif()

    # --- fixture de violations
    run_guard(violations.cpp.txt ${eol} rc text)
    if(rc EQUAL 0)
        message(FATAL_ERROR "[${eol}] violations.cpp.txt : le garde devrait ECHOUER")
    endif()
    file(READ "${FIXTURES}/violations.cpp.txt" fixture_text)
    string(REPLACE "\r" "" fixture_text "${fixture_text}")
    string(REPLACE ";" "<SEMI>" fixture_text "${fixture_text}")
    string(REPLACE "[" "<LB>" fixture_text "${fixture_text}")
    string(REPLACE "]" "<RB>" fixture_text "${fixture_text}")
    string(REPLACE "\n" ";" fixture_lines "${fixture_text}")
    set(expected "")
    set(line_no 0)
    foreach(l IN LISTS fixture_lines)
        math(EXPR line_no "${line_no} + 1")
        if(l MATCHES "// VIOLATION")
            list(APPEND expected ${line_no})
        endif()
    endforeach()
    list(LENGTH expected expected_count)
    if(expected_count LESS 30)
        message(FATAL_ERROR "fixture violations.cpp.txt : moins de 30 cas marques (${expected_count})")
    endif()

    string(REGEX MATCHALL "main_window\\.cpp:[0-9]+" reported_raw "${text}")
    set(reported "")
    foreach(r ${reported_raw})
        string(REGEX REPLACE "^.*:" "" n "${r}")
        list(APPEND reported ${n})
    endforeach()
    set(missing "")
    foreach(n ${expected})
        if(NOT n IN_LIST reported)
            list(APPEND missing ${n})
        endif()
    endforeach()
    set(extra "")
    foreach(n ${reported})
        if(NOT n IN_LIST expected)
            list(APPEND extra ${n})
        endif()
    endforeach()
    if(missing OR extra)
        message(FATAL_ERROR "[${eol}] violations.cpp.txt : lignes NON signalees (faux negatifs) : "
            "[${missing}] ; lignes signalees a tort : [${extra}]\n${text}")
    endif()
endforeach()

message(STATUS "check_selection_guard_selftest : OK (${expected_count} motifs, LF et CRLF)")
