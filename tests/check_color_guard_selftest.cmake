# Auto-test de tests/check_no_hardcoded_colors.cmake : une regression du script lui-meme
# (faux negatif ou faux positif) doit faire echouer la CI.
#  - clean.cpp.txt : aucun motif interdit -> le garde doit PASSER ;
#  - violations.cpp.txt : chaque ligne marquee `// VIOLATION` doit etre signalee
#    `main_window.cpp:<ligne>` et AUCUNE autre ligne ne doit l'etre ;
#  - une violation dans design_tokens.cpp (liste blanche) ne doit PAS etre signalee ;
#  - cliquet (BASELINE), plafond de derogations `// color-ok:` et base orpheline.
# Chaque fixture est rejouee en fins de ligne CRLF (meme resultat attendu). Sans Qt.
# Usage : cmake -DGUARD=<script> -DFIXTURES=<dossier> -DWORK_DIR=<tmp> -P ...

cmake_minimum_required(VERSION 3.21)

foreach(var GUARD FIXTURES WORK_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "${var} non defini")
    endif()
endforeach()

# Lance le garde sur un faux depot : apps/desktop/<name> = `text` (+ base optionnelle).
function(run_guard_text name text eol baseline_text out_rc out_text)
    set(root "${WORK_DIR}/color_guard_${eol}")
    file(REMOVE_RECURSE "${root}")
    file(MAKE_DIRECTORY "${root}/apps/desktop")
    string(REPLACE "\r" "" text "${text}")
    if(eol STREQUAL "crlf")
        string(REPLACE "\n" "\r\n" text "${text}")
    endif()
    file(WRITE "${root}/apps/desktop/${name}" "${text}")
    set(extra "")
    if(NOT baseline_text STREQUAL "NONE")
        file(WRITE "${root}/baseline.txt" "${baseline_text}")
        set(extra "-DBASELINE=${root}/baseline.txt")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -DSRC_DIR=${root} ${extra} -P "${GUARD}"
        RESULT_VARIABLE rc OUTPUT_VARIABLE so ERROR_VARIABLE se)
    set(${out_rc} ${rc} PARENT_SCOPE)
    set(${out_text} "${so}\n${se}" PARENT_SCOPE)
endfunction()

function(run_guard fixture name eol out_rc out_text)
    file(READ "${FIXTURES}/${fixture}" text)
    run_guard_text("${name}" "${text}" ${eol} "NONE" rc txt)
    set(${out_rc} ${rc} PARENT_SCOPE)
    set(${out_text} "${txt}" PARENT_SCOPE)
endfunction()

function(expect_pass label rc text)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "${label} : le garde devrait PASSER (faux positif) :\n${text}")
    endif()
endfunction()

function(expect_fail label rc text must_contain)
    if(rc EQUAL 0)
        message(FATAL_ERROR "${label} : le garde devrait ECHOUER (faux negatif)")
    endif()
    if(NOT must_contain STREQUAL "" AND NOT text MATCHES "${must_contain}")
        message(FATAL_ERROR "${label} : message attendu '${must_contain}' absent :\n${text}")
    endif()
endfunction()

set(two_violations "void f() {\n    QColor a(1, 2, 3);\n    QColor b(4, 5, 6);\n}\n")

foreach(eol lf crlf)
    # --- fixture propre
    run_guard(clean.cpp.txt main_window.cpp ${eol} rc text)
    expect_pass("[${eol}] clean.cpp.txt" ${rc} "${text}")

    # --- fixture de violations
    run_guard(violations.cpp.txt main_window.cpp ${eol} rc text)
    expect_fail("[${eol}] violations.cpp.txt" ${rc} "${text}" "")
    file(READ "${FIXTURES}/violations.cpp.txt" fixture_text)
    string(REPLACE "\r" "" fixture_text "${fixture_text}")
    string(REPLACE ";" "<SEMI>" fixture_text "${fixture_text}")
    string(REPLACE "[" "<LB>" fixture_text "${fixture_text}")
    string(REPLACE "]" "<RB>" fixture_text "${fixture_text}")
    # Une ligne = un element non vide (les lignes vides sinon disparaitraient de la liste).
    string(REPLACE "\n" "\n_" fixture_text "_${fixture_text}")
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
    if(expected_count LESS 25)
        message(FATAL_ERROR "fixture violations.cpp.txt : moins de 25 cas marques (${expected_count})")
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
    set(extra_lines "")
    foreach(n ${reported})
        if(NOT n IN_LIST expected)
            list(APPEND extra_lines ${n})
        endif()
    endforeach()
    if(missing OR extra_lines)
        message(FATAL_ERROR "[${eol}] violations.cpp.txt : lignes NON signalees (faux negatifs) : "
            "[${missing}] ; lignes signalees a tort : [${extra_lines}]\n${text}")
    endif()

    # --- liste blanche : design_tokens.cpp peut contenir des litteraux de couleur
    run_guard(violations.cpp.txt design_tokens.cpp ${eol} rc text)
    expect_pass("[${eol}] design_tokens.cpp (liste blanche)" ${rc} "${text}")

    # --- cliquet
    run_guard_text(main_window.cpp "${two_violations}" ${eol} "main_window.cpp 2\n" rc text)
    expect_pass("[${eol}] cliquet : base = nombre" ${rc} "${text}")
    run_guard_text(main_window.cpp "${two_violations}" ${eol}
        "# commentaire\n\nmain_window.cpp 2\n" rc text)
    expect_pass("[${eol}] cliquet : commentaires et lignes vides" ${rc} "${text}")
    run_guard_text(main_window.cpp "${two_violations}" ${eol} "main_window.cpp 1\n" rc text)
    expect_fail("[${eol}] cliquet : depassement" ${rc} "${text}" "main_window\\.cpp:[0-9]+")
    run_guard_text(main_window.cpp "${two_violations}" ${eol} "main_window.cpp 3\n" rc text)
    expect_fail("[${eol}] cliquet : base trop haute" ${rc} "${text}" "abaisser la base")
    run_guard_text(main_window.cpp "${two_violations}" ${eol} "other.cpp 2\n" rc text)
    expect_fail("[${eol}] cliquet : fichier absent de la base" ${rc} "${text}" "main_window\\.cpp:[0-9]+")
    run_guard_text(main_window.cpp "void f() {}\n" ${eol} "main_window.cpp 2\n" rc text)
    expect_fail("[${eol}] cliquet : fichier propre mais base > 0" ${rc} "${text}" "abaisser la base")
    run_guard_text(main_window.cpp "void f() {}\n" ${eol} "# vide\n" rc text)
    expect_pass("[${eol}] cliquet : base vide, fichier propre" ${rc} "${text}")
    run_guard_text(main_window.cpp "void f() {}\n" ${eol} "gone.cpp 1\n" rc text)
    expect_fail("[${eol}] cliquet : ligne de base orpheline" ${rc} "${text}" "n'existe plus")

    # --- derogation sans raison : violation a part entiere (meme sur une ligne propre)
    run_guard_text(main_window.cpp "QColor a(1, 2, 3); // color-ok:\n" ${eol} "NONE" rc text)
    expect_fail("[${eol}] derogation sans raison" ${rc} "${text}" "main_window\\.cpp:1")
    run_guard_text(main_window.cpp "void f() {}\n// color-ok:   \n" ${eol} "NONE" rc text)
    expect_fail("[${eol}] derogation vide isolee" ${rc} "${text}" "main_window\\.cpp:2")

    # --- plafond de derogations (3 au total)
    set(ex3 "")
    foreach(i 1 2 3)
        string(APPEND ex3 "QColor s${i}(1, 2, 3); // color-ok: pastille ${i}\n")
    endforeach()
    run_guard_text(main_window.cpp "${ex3}" ${eol} "NONE" rc text)
    expect_pass("[${eol}] 3 derogations" ${rc} "${text}")
    run_guard_text(main_window.cpp "${ex3}QColor s4(1, 2, 3); // color-ok: quatrieme\n" ${eol} "NONE" rc text)
    expect_fail("[${eol}] 4 derogations" ${rc} "${text}" "derogations")
endforeach()

message(STATUS "check_color_guard_selftest : OK (${expected_count} motifs, LF et CRLF)")
