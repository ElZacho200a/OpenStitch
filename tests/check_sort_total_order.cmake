# Garde structurelle HP-QA-011 : un tri instable (`std::sort`, `std::ranges::sort`) dont le
# comparateur admet des egalites entre elements discernables rend la sortie dependante de
# l'implementation de la STL. Dans les moteurs qui produisent des points (stitch_generation,
# auto_satin, optimization), tout appel a `std::sort(` / `ranges::sort(` doit donc porter,
# sur sa ligne ou la precedente, une annotation `total-order-ok: <raison>` qui atteste que la
# cle est totale (indice stable en dernier critere, valeurs scalaires, ...). Sinon : utiliser
# `std::stable_sort` ou ajouter une cle de depart (l'indice d'origine).
#
# Usage : cmake -DSRC_DIR=<racine du depot> -P check_sort_total_order.cmake

if(NOT DEFINED SRC_DIR)
    message(FATAL_ERROR "SRC_DIR non defini")
endif()

file(GLOB_RECURSE candidates
    "${SRC_DIR}/libs/stitch_generation/*.cpp" "${SRC_DIR}/libs/stitch_generation/*.hpp"
    "${SRC_DIR}/libs/auto_satin/*.cpp" "${SRC_DIR}/libs/auto_satin/*.hpp"
    "${SRC_DIR}/libs/optimization/*.cpp" "${SRC_DIR}/libs/optimization/*.hpp")

set(violations "")
set(sort_count 0)

foreach(src_file ${candidates})
    file(STRINGS "${src_file}" lines)
    set(line_no 0)
    set(prev_line "")
    foreach(line ${lines})
        math(EXPR line_no "${line_no} + 1")
        string(REGEX REPLACE "//.*$" "" code_only "${line}")
        if(code_only MATCHES "(std::sort|ranges::sort)[ \t]*\\(")
            math(EXPR sort_count "${sort_count} + 1")
            if((NOT line MATCHES "total-order-ok") AND (NOT prev_line MATCHES "total-order-ok"))
                list(APPEND violations "${src_file}:${line_no}")
            endif()
        endif()
        set(prev_line "${line}")
    endforeach()
endforeach()

if(violations)
    string(REPLACE ";" "\n  " violations_str "${violations}")
    message(FATAL_ERROR
        "Tri instable sans annotation 'total-order-ok:' (HP-QA-011). Donner une cle de "
        "depart totale (indice stable), ou utiliser std::stable_sort, ou annoter "
        "'// total-order-ok: <raison>' :\n  ${violations_str}")
endif()

message(STATUS "check_sort_total_order : OK (${sort_count} tris annotes)")
