# Garde structurelle (lot L5-T4, specs/plans/ui-interaction-model.md §2.7) :
# l'etat de selection de MainWindow (selectedObject_, selectedRegion_,
# selectedEmbroidery_, multiSelection_) n'est ecrit QUE par le mutateur unique
# `MainWindow::setSelection` / `clearMultiSelection`, delimite dans
# apps/desktop/main_window.cpp par les marqueurs de commentaire
#   // SELECTION-MUTATOR:BEGIN  ...  // SELECTION-MUTATOR:END
# Tout autre site (assignation, reset(), clear(), push_back(), erase(), swap,
# std::move...) est une violation : un oubli laisserait multiSelection_ perimee
# par rapport a selectedObject_ (invariants non garantis).
#
# Meme esprit que check_no_raw_sequence_bypass.cmake : parcours des sources
# apps/desktop/*.cpp|hpp, partie `//` ignoree (la prose peut nommer les
# membres), echec avec fichier:ligne exact.
#
# Usage : cmake -DSRC_DIR=<racine du depot> -P check_selection_single_mutator.cmake

if(NOT DEFINED SRC_DIR)
    message(FATAL_ERROR "SRC_DIR non defini")
endif()

file(GLOB candidates "${SRC_DIR}/apps/desktop/*.cpp" "${SRC_DIR}/apps/desktop/*.hpp")

set(members "selectedObject_|selectedRegion_|selectedEmbroidery_|multiSelection_")
set(prefix "(^|[^A-Za-z0-9_])")
# Ecriture directe : `membre =` (pas `==`), ou methode mutante appelee sur le membre.
set(write_re "${prefix}(${members})[ \t]*(=[^=]|\\.(reset|clear|push_back|emplace_back|emplace|pop_back|erase|insert|assign|swap|resize)[ \t]*\\()")
# Passage du membre a swap()/move() : ecriture indirecte.
set(indirect_re "(swap|move)[ \t]*\\(([^)]*[^A-Za-z0-9_])?(${members})")

set(violations "")
set(begin_count 0)
set(end_count 0)

foreach(src_file ${candidates})
    file(READ "${src_file}" content)
    string(REPLACE "\r" "" content "${content}")
    # Protege les caracteres qui casseraient une liste CMake, puis une ligne = un element.
    string(REPLACE ";" "<SEMI>" content "${content}")
    string(REPLACE "[" "<LB>" content "${content}")
    string(REPLACE "]" "<RB>" content "${content}")
    string(REPLACE "\n" ";" lines "${content}")

    get_filename_component(src_name "${src_file}" NAME)
    set(in_mutator FALSE)
    set(line_no 0)
    foreach(line IN LISTS lines)
        math(EXPR line_no "${line_no} + 1")
        if(line MATCHES "SELECTION-MUTATOR:BEGIN")
            math(EXPR begin_count "${begin_count} + 1")
            if(NOT src_name STREQUAL "main_window.cpp")
                list(APPEND violations "${src_file}:${line_no} (marqueur hors main_window.cpp)")
            endif()
            set(in_mutator TRUE)
            continue()
        endif()
        if(line MATCHES "SELECTION-MUTATOR:END")
            math(EXPR end_count "${end_count} + 1")
            set(in_mutator FALSE)
            continue()
        endif()
        if(in_mutator)
            continue()
        endif()
        string(REGEX REPLACE "//.*$" "" code_only "${line}")
        if(code_only MATCHES "${write_re}" OR code_only MATCHES "${indirect_re}")
            list(APPEND violations "${src_file}:${line_no}")
        endif()
    endforeach()
    if(in_mutator)
        list(APPEND violations "${src_file} (SELECTION-MUTATOR:BEGIN sans END)")
    endif()
endforeach()

if(NOT begin_count EQUAL 1 OR NOT end_count EQUAL 1)
    list(APPEND violations
        "marqueurs SELECTION-MUTATOR : attendu exactement 1 BEGIN et 1 END, trouve ${begin_count}/${end_count}")
endif()

if(violations)
    string(REPLACE ";" "\n  " violations_str "${violations}")
    message(FATAL_ERROR
        "Ecriture directe de l'etat de selection hors du mutateur unique "
        "(MainWindow::setSelection, entre SELECTION-MUTATOR:BEGIN/END dans "
        "main_window.cpp). Passer par setSelection()/editSelection() :\n  ${violations_str}")
endif()

message(STATUS "check_selection_single_mutator : OK")
