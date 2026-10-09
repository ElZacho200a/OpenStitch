# Garde structurelle (lot L5-T4, specs/plans/ui-interaction-model.md §2.7) :
# l'etat de selection de MainWindow (selectedObject_, selectedRegion_,
# selectedEmbroidery_, multiSelection_) n'est ecrit QUE par le mutateur unique
# `MainWindow::setSelection`, delimite dans apps/desktop/main_window.cpp par
#   // SELECTION-MUTATOR:BEGIN  ...  // SELECTION-MUTATOR:END
#
# Regle INVERSEE (liste blanche) : hors du bloc, CHAQUE occurrence d'un des
# quatre membres est une violation, sauf usage en lecture seule reconnu :
# comparaison (==, !=, <, >), has_value()/empty()/size()/value()/back()/front()/
# at()/[i]/->champ en lecture, begin()/end() passes a find/any_of/count...,
# `if (membre)`, copie par valeur, reference const, range-for const, argument
# de fonction. Sont toujours des violations : affectation / operateur compose /
# ++ -- (y compris sur back(), [i], at(), value(), ->champ), methodes mutantes
# (reset, clear, push_back, erase, insert...), alias non const (`auto& r =
# membre`, `&membre`, capture `[&membre]`, `for (auto& x : membre)`),
# std::ref, et passage a un algorithme mutant (sort, erase_if, reverse, swap,
# exchange, move, fill, unique, rotate...). Les commentaires // et /* */ sont
# ignores ; une instruction peut s'etendre sur plusieurs lignes.
#
# Usage : cmake -DSRC_DIR=<racine> -P check_selection_single_mutator.cmake
# (auto-test de ce script : tests/check_selection_guard_selftest.cmake).

cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED SRC_DIR)
    message(FATAL_ERROR "SRC_DIR non defini")
endif()

file(GLOB candidates "${SRC_DIR}/apps/desktop/*.cpp" "${SRC_DIR}/apps/desktop/*.hpp")

set(members selectedObject_ selectedRegion_ selectedEmbroidery_ multiSelection_ extraRegions_)
set(mutating_methods "reset|clear|push_back|emplace_back|emplace|pop_back|erase|insert|assign|swap|resize|emplace_front|push_front|pop_front|reserve|shrink_to_fit|merge|splice|remove|sort|reverse|unique")
set(mutating_algos "sort|stable_sort|erase|erase_if|remove|remove_if|reverse|rotate|shuffle|swap|iter_swap|exchange|move|fill|fill_n|unique|replace|replace_if|partition|stable_partition|assign|ref|next_permutation|prev_permutation")
set(read_algos "find|find_if|any_of|all_of|none_of|count|count_if|contains|binary_search|equal")

# Remplace [debut, fin] par autant de retours a la ligne (conserve les numeros de ligne).
function(blank_span text_var start_idx end_idx_exclusive)
    set(text "${${text_var}}")
    math(EXPR len "${end_idx_exclusive} - ${start_idx}")
    string(SUBSTRING "${text}" ${start_idx} ${len} span)
    string(REGEX REPLACE "[^\n]" "" newlines "${span}")
    string(SUBSTRING "${text}" 0 ${start_idx} head)
    string(SUBSTRING "${text}" ${end_idx_exclusive} -1 tail)
    set(${text_var} "${head}${newlines}${tail}" PARENT_SCOPE)
endfunction()

set(violations "")
set(begin_count 0)
set(end_count 0)

foreach(src_file ${candidates})
    file(READ "${src_file}" content)
    string(REPLACE "\r" "" content "${content}")

    # 1. Bloc mutateur : blanchi (marqueurs compris), nombre de marqueurs compte.
    string(REGEX MATCHALL "SELECTION-MUTATOR:BEGIN" b_all "${content}")
    string(REGEX MATCHALL "SELECTION-MUTATOR:END" e_all "${content}")
    list(LENGTH b_all nb)
    list(LENGTH e_all ne)
    math(EXPR begin_count "${begin_count} + ${nb}")
    math(EXPR end_count "${end_count} + ${ne}")
    get_filename_component(src_name "${src_file}" NAME)
    if((nb GREATER 0 OR ne GREATER 0) AND NOT src_name STREQUAL "main_window.cpp")
        list(APPEND violations "${src_file} (marqueur SELECTION-MUTATOR hors main_window.cpp)")
    endif()
    if(nb EQUAL 1 AND ne EQUAL 1)
        string(FIND "${content}" "SELECTION-MUTATOR:BEGIN" bpos)
        string(FIND "${content}" "SELECTION-MUTATOR:END" epos)
        if(bpos LESS epos)
            set(rest_start ${epos})
            string(SUBSTRING "${content}" ${epos} -1 after_end)
            string(FIND "${after_end}" "\n" nl_off)
            if(nl_off EQUAL -1)
                string(LENGTH "${content}" rest_start)
            else()
                math(EXPR rest_start "${epos} + ${nl_off}")
            endif()
            blank_span(content ${bpos} ${rest_start})
        else()
            list(APPEND violations "${src_file} (SELECTION-MUTATOR:END avant BEGIN)")
        endif()
    endif()

    # 2. Commentaires : // puis /* */ (retours a la ligne conserves).
    string(REGEX REPLACE "//[^\n]*" "" content "${content}")
    while(TRUE)
        string(FIND "${content}" "/*" cstart)
        if(cstart EQUAL -1)
            break()
        endif()
        string(SUBSTRING "${content}" ${cstart} -1 from_c)
        string(FIND "${from_c}" "*/" crel)
        if(crel EQUAL -1)
            string(LENGTH "${content}" cend)
        else()
            math(EXPR cend "${cstart} + ${crel} + 2")
        endif()
        blank_span(content ${cstart} ${cend})
    endwhile()

    # 3. Une instruction = un element de liste (separateurs ; { }). Les crochets et
    #    points-virgules d'origine sont proteges pour ne pas casser la liste.
    string(REPLACE "[" "<LB>" content "${content}")
    string(REPLACE "]" "<RB>" content "${content}")
    string(REPLACE ";" "{" content "${content}")
    string(REPLACE "}" "{" content "${content}")
    string(REPLACE "{" ";" statements "${content}")

    set(line_no 1)
    foreach(stmt IN LISTS statements)
        string(REGEX MATCHALL "\n" nls "${stmt}")
        list(LENGTH nls stmt_nl)
        foreach(member IN LISTS members)
            set(rest "${stmt}")
            set(consumed 0)
            while(TRUE)
                string(FIND "${rest}" "${member}" mpos)
                if(mpos EQUAL -1)
                    break()
                endif()
                string(LENGTH "${member}" mlen)
                math(EXPR abs_pos "${consumed} + ${mpos}")
                # Identifiant plus long (ex. lastselectedObject_) : pas une occurrence.
                set(ident_ok TRUE)
                if(abs_pos GREATER 0)
                    math(EXPR prev_pos "${abs_pos} - 1")
                    string(SUBSTRING "${stmt}" ${prev_pos} 1 prev_ch)
                    if(prev_ch MATCHES "[A-Za-z0-9_]")
                        set(ident_ok FALSE)
                    endif()
                endif()
                if(ident_ok)
                    string(SUBSTRING "${stmt}" 0 ${abs_pos} left)
                    math(EXPR right_start "${abs_pos} + ${mlen}")
                    string(SUBSTRING "${stmt}" ${right_start} -1 right)
                    # Ligne de l'occurrence.
                    string(REGEX MATCHALL "\n" nls_before "${left}")
                    list(LENGTH nls_before nb_before)
                    math(EXPR occ_line "${line_no} + ${nb_before}")
                    # Normalise les blancs.
                    string(REGEX REPLACE "[ \t\n]+" " " left "${left}")
                    string(REGEX REPLACE "[ \t\n]+" " " right "${right}")
                    string(REGEX REPLACE "^ " "" right "${right}")
                    set(bad FALSE)

                    # --- gauche : alias, adresse, ref, algorithme mutant, range-for non const
                    if(left MATCHES "[^&]&[ ]*$" OR left MATCHES "^&[ ]*$")
                        set(bad TRUE) # &membre (adresse / capture par reference)
                    endif()
                    if(left MATCHES "&[ ]*[A-Za-z_][A-Za-z0-9_]*[ ]*=[ ]*$" AND NOT left MATCHES "const")
                        set(bad TRUE) # auto& r = membre;
                    endif()
                    if(left MATCHES "&[ ]*[A-Za-z_][A-Za-z0-9_]*[ ]*:[ ]*$" AND NOT left MATCHES "const")
                        set(bad TRUE) # for (auto& x : membre)
                    endif()
                    if(left MATCHES "(^|[^A-Za-z0-9_])(${mutating_algos})[ ]*\\([^()]*$")
                        set(bad TRUE) # algorithme mutant / std::ref / std::move(membre...
                    endif()
                    if(left MATCHES "(^|[^A-Za-z0-9_])(${mutating_algos})[ ]*\\([^()]*\\([^()]*$"
                       AND NOT left MATCHES "(${read_algos})[ ]*\\([^()]*$")
                        set(bad TRUE) # membre.begin() imbrique dans un algorithme mutant
                    endif()

                    # --- droite : on retire les acces en lecture puis on teste ecriture
                    set(tail "${right}")
                    set(guard 0)
                    while(guard LESS 8)
                        math(EXPR guard "${guard} + 1")
                        if(tail MATCHES "^\\.(back|front|value|at|begin|end|cbegin|cend|data|operator\\*)[ ]*\\([^()]*\\)(.*)$")
                            set(tail "${CMAKE_MATCH_2}")
                        elseif(tail MATCHES "^<LB>[^<]*<RB>(.*)$")
                            set(tail "${CMAKE_MATCH_1}")
                        elseif(tail MATCHES "^(\\.|->)[A-Za-z_][A-Za-z0-9_]*([^A-Za-z0-9_(].*|)$")
                            set(tail "${CMAKE_MATCH_2}")
                        else()
                            break()
                        endif()
                        string(REGEX REPLACE "^ " "" tail "${tail}")
                    endwhile()
                    if(tail MATCHES "^(=[^=]|\\+=|-=|\\*=|/=|%=|\\|=|&=|\\^=|<<=|>>=|\\+\\+|--)")
                        set(bad TRUE)
                    endif()
                    if(right MATCHES "^(\\.|->)(${mutating_methods})[ ]*\\(" OR tail MATCHES "^(\\.|->)(${mutating_methods})[ ]*\\(")
                        set(bad TRUE)
                    endif()
                    if(left MATCHES "(\\+\\+|--)[ ]*\\*?[ ]*$")
                        set(bad TRUE) # ++membre
                    endif()

                    # --- liste blanche : ce qui reste doit etre un usage en lecture reconnu
                    if(NOT bad)
                        if(NOT (tail STREQUAL "" OR tail MATCHES "^(==|!=|<|>|\\)|,|&&|\\|\\||\\?|:|\\.|->)"))
                            set(bad TRUE) # forme non reconnue : refusee par prudence
                        endif()
                    endif()

                    if(bad)
                        list(APPEND violations "${src_file}:${occ_line}")
                    endif()
                endif()
                math(EXPR consumed "${consumed} + ${mpos} + ${mlen}")
                string(SUBSTRING "${rest}" ${mpos} -1 rest)
                string(SUBSTRING "${rest}" ${mlen} -1 rest)
            endwhile()
        endforeach()
        math(EXPR line_no "${line_no} + ${stmt_nl}")
    endforeach()
endforeach()

if(NOT begin_count EQUAL 1 OR NOT end_count EQUAL 1)
    list(APPEND violations
        "marqueurs SELECTION-MUTATOR : attendu exactement 1 BEGIN et 1 END, trouve ${begin_count}/${end_count}")
endif()

if(violations)
    list(REMOVE_DUPLICATES violations)
    string(REPLACE ";" "\n  " violations_str "${violations}")
    message(FATAL_ERROR
        "Ecriture (ou usage non reconnu en lecture seule) de l'etat de selection hors du "
        "mutateur unique (MainWindow::setSelection, entre SELECTION-MUTATOR:BEGIN/END dans "
        "main_window.cpp). Passer par setSelection()/editSelection() :\n  ${violations_str}")
endif()

message(STATUS "check_selection_single_mutator : OK")
