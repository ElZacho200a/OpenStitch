# Garde structurelle (lot L2 design system v2, specs/plans/ui-design-system-v2.md §3) :
# aucune couleur d'interface codee en dur dans apps/desktop/*.cpp|hpp. La SEULE source
# de litteraux de couleur est apps/desktop/design_tokens.cpp (liste blanche ci-dessous).
#
# Motifs interdits (hors commentaires // et /* */, fins de ligne LF ou CRLF) :
#  (a) dans un litteral chaine : `#rrggbb` / `#rrggbbaa` (6 ou 8 chiffres hexa) ou un
#      litteral reduit a `#rgb` / `#rgba` ;
#  (b) `QColor(` / `QColor{` suivi d'un nombre (decimal ou 0x), d'un litteral chaine ou
#      d'une couleur nommee ; `QColor::fromRgb(F)/fromRgba/fromHsv/fromHsl/fromCmyk(` a
#      argument numerique ; `qRgb(` / `qRgba(` a argument numerique ; `setNamedColor("..")` ;
#  (b') declarations `QColor nom(0x..)`, `QColor nom{12, ..}`, `QColor nom("red")`
#      (ex. `const QColor kInk(0x5C, ..)`) ;
#  (c) dans un litteral : `rgb(`, `rgba(`, `hsl(`, `hsla(` suivis d'un nombre ou d'un
#      format `%1` (ex. `rgb(%1,%2,%3)`) ;
#  (d) couleurs nommees `Qt::white/black/red/darkRed/green/.../gray/darkGray/lightGray`
#      (`Qt::transparent`, `Qt::color0/1` restent permis).
# Non vises : `QColor(r, g, b)` a variables (couleurs de fil : donnees, pas identite).
# Une instruction (jusqu'au prochain `;`) est jointe avant analyse : un appel reparti sur
# plusieurs lignes est reconnu et signale a la ligne de son debut.
#
# Derogation : `// color-ok: <raison non vide>` sur la ligne du motif ; au plus 3 dans tout
# le depot (apps/desktop). Une derogation sans raison est elle-meme une violation.
#
# Cliquet (ratchet) : si BASELINE designe un fichier existant (`<fichier> <nombre>` par
# ligne, `#` = commentaire), le nombre de motifs d'un fichier ne doit NI depasser NI etre
# inferieur a sa ligne de base (un fichier absent de la base vaut 0) ; la base ne peut que
# decroitre jusqu'a etre vide. Sans BASELINE : mode strict, toute violation echoue.
#
# Usage : cmake -DSRC_DIR=<racine> [-DBASELINE=<fichier>] [-DPRINT_COUNTS=ON]
#               -P check_no_hardcoded_colors.cmake
# (auto-test : tests/check_color_guard_selftest.cmake).

cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED SRC_DIR)
    message(FATAL_ERROR "SRC_DIR non defini")
endif()

# Fichiers autorises a contenir des litteraux de couleur (nom de fichier seul).
set(allow_files design_tokens.cpp)
set(max_exemptions 3)

file(GLOB candidates "${SRC_DIR}/apps/desktop/*.cpp" "${SRC_DIR}/apps/desktop/*.hpp")
list(SORT candidates)

set(ws "[ \t\n]")
set(named_colors "white|black|red|darkRed|green|darkGreen|blue|darkBlue|cyan|darkCyan|magenta|darkMagenta|yellow|darkYellow|gray|darkGray|lightGray")
set(num "(0[xX][0-9A-Fa-f]|[0-9])")
set(num_arg "(0[xX][0-9A-Fa-f]|[-.0-9])")

# Motifs sur le code (litteraux chaine deja remplaces par "S"). Chaque entree : nom|regex.
set(code_rules
    "QColor-literal|QColor${ws}*[({]${ws}*${num}"
    "QColor-string|QColor${ws}*[({]${ws}*\"S\""
    "QColor-decl-string|QColor${ws}+[A-Za-z_][A-Za-z0-9_]*${ws}*[({]${ws}*\"S\""
    "QColor-named|QColor${ws}*[({]${ws}*Qt::(${named_colors})"
    "QColor-decl|QColor${ws}+[A-Za-z_][A-Za-z0-9_]*${ws}*[({]${ws}*${num}"
    "QColor-from|QColor::from(Rgb|RgbF|Rgba|Rgba64|Hsv|HsvF|Hsl|HslF|Cmyk|CmykF)${ws}*[(]${ws}*${num_arg}"
    "qRgb|(^|[^A-Za-z0-9_])qRgba?${ws}*[(]${ws}*${num}"
    "setNamedColor|setNamedColor${ws}*[(]${ws}*\"S\""
    "Qt-named|Qt::(${named_colors})([^A-Za-z0-9_]|$)")

# Motifs sur le contenu des litteraux chaine.
set(hex6 "#[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]")
set(hex_short "^#[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]?$")
set(literal_rules
    "hex|${hex6}"
    "hex-short|${hex_short}"
    "css-rgb|(^|[^A-Za-z0-9_])(rgb|rgba|hsl|hsla)[(]${ws}*(%[0-9]|[0-9])")

# Ajoute a `var` une entree "ligne:regle" par motif de `rules` trouve dans `text`
# (une entree par occurrence). `base_line` = ligne de debut de `text`.
function(scan_rules var text base_line rules)
    set(found "${${var}}")
    foreach(rule IN LISTS rules)
        string(FIND "${rule}" "|" bar)
        string(SUBSTRING "${rule}" 0 ${bar} rule_name)
        math(EXPR rx_start "${bar} + 1")
        string(SUBSTRING "${rule}" ${rx_start} -1 rx)
        set(rest "${text}")
        set(nl_consumed 0)
        while(TRUE)
            string(REGEX MATCH "${rx}" m "${rest}")
            if(m STREQUAL "")
                break()
            endif()
            string(FIND "${rest}" "${m}" pos)
            if(pos EQUAL -1)
                break()
            endif()
            string(SUBSTRING "${rest}" 0 ${pos} before)
            string(REGEX REPLACE "[^\n]" "" before_nl "${before}")
            string(LENGTH "${before_nl}" nb)
            math(EXPR line "${base_line} + ${nl_consumed} + ${nb}")
            # Le motif peut commencer par un caractere de contexte (\n ou espace) : la
            # ligne est celle du premier caractere non blanc du motif.
            set(nl_lead 0)
            if(m MATCHES "^[ \t\n]+")
                string(REGEX REPLACE "[^\n]" "" lead_nl "${CMAKE_MATCH_0}")
                string(LENGTH "${lead_nl}" nl_lead)
            endif()
            math(EXPR line "${line} + ${nl_lead}")
            list(APPEND found "${line}:${rule_name}")
            string(LENGTH "${m}" mlen)
            math(EXPR cut "${pos} + ${mlen}")
            string(SUBSTRING "${rest}" 0 ${cut} consumed_text)
            string(REGEX REPLACE "[^\n]" "" consumed_nl "${consumed_text}")
            string(LENGTH "${consumed_nl}" nconsumed)
            math(EXPR nl_consumed "${nl_consumed} + ${nconsumed}")
            string(SUBSTRING "${rest}" ${cut} -1 rest)
        endwhile()
    endforeach()
    set(${var} "${found}" PARENT_SCOPE)
endfunction()

set(all_violations "")   # "fichier:ligne" (pour les messages)
set(counts_report "")
set(baseline_failures "")
set(exempt_total 0)

# --- base de reference (cliquet) ---
set(use_baseline FALSE)
if(DEFINED BASELINE AND EXISTS "${BASELINE}")
    set(use_baseline TRUE)
    file(STRINGS "${BASELINE}" baseline_lines)
    foreach(bl IN LISTS baseline_lines)
        string(STRIP "${bl}" bl)
        if(bl STREQUAL "" OR bl MATCHES "^#")
            continue()
        endif()
        if(bl MATCHES "^([^ \t]+)[ \t]+([0-9]+)$")
            set(base_${CMAKE_MATCH_1} ${CMAKE_MATCH_2})
        else()
            list(APPEND baseline_failures "base illisible : '${bl}'")
        endif()
    endforeach()
endif()

foreach(src_file IN LISTS candidates)
    get_filename_component(src_name "${src_file}" NAME)
    list(FIND allow_files "${src_name}" allowed_idx)
    if(NOT allowed_idx EQUAL -1)
        continue()
    endif()

    file(READ "${src_file}" content)
    string(REPLACE "\r" "" content "${content}")
    # Proteger ; [ ] pour les listes, puis une ligne = un element non vide.
    string(REPLACE "[" "<LB>" content "${content}")
    string(REPLACE "]" "<RB>" content "${content}")
    string(REPLACE ";" "<SEMI>" content "${content}")
    string(REPLACE "\n" "\n " content "${content}")
    string(REPLACE "\n" ";" lines " ${content}")

    set(code "")            # code sans commentaires, litteraux remplaces par "S"
    set(file_hits "")       # "ligne:regle" issus des litteraux
    set(exempt_lines "")
    set(in_block FALSE)
    set(in_raw FALSE)
    set(raw_close "")
    set(line_no 0)

    foreach(line IN LISTS lines)
        math(EXPR line_no "${line_no} + 1")
        set(out "")
        set(rest "${line}")

        # Ligne triviale : ni guillemet, ni apostrophe, ni barre, hors etat special.
        if(NOT in_block AND NOT in_raw AND NOT rest MATCHES "[\"'/]")
            string(APPEND code "${rest}\n")
            continue()
        endif()

        while(TRUE)
            if(rest STREQUAL "")
                break()
            endif()
            if(in_block)
                string(FIND "${rest}" "*/" cpos)
                if(cpos EQUAL -1)
                    set(rest "")
                    break()
                endif()
                math(EXPR cut "${cpos} + 2")
                string(SUBSTRING "${rest}" ${cut} -1 rest)
                set(in_block FALSE)
                string(APPEND out " ")
                continue()
            endif()
            if(in_raw)
                string(FIND "${rest}" "${raw_close}" rpos)
                if(rpos EQUAL -1)
                    scan_rules(file_hits "${rest}" ${line_no} "${literal_rules}")
                    set(rest "")
                    break()
                endif()
                string(SUBSTRING "${rest}" 0 ${rpos} raw_text)
                scan_rules(file_hits "${raw_text}" ${line_no} "${literal_rules}")
                string(LENGTH "${raw_close}" rc_len)
                math(EXPR cut "${rpos} + ${rc_len}")
                string(SUBSTRING "${rest}" ${cut} -1 rest)
                set(in_raw FALSE)
                string(APPEND out "\"S\"")
                continue()
            endif()

            # Prochain jeton parmi " ' // /*.
            set(best -1)
            set(best_kind "")
            foreach(tok IN ITEMS "\"" "'" "//" "/*")
                string(FIND "${rest}" "${tok}" tpos)
                if(NOT tpos EQUAL -1 AND (best EQUAL -1 OR tpos LESS best))
                    set(best ${tpos})
                    set(best_kind "${tok}")
                endif()
            endforeach()
            if(best EQUAL -1)
                string(APPEND out "${rest}")
                set(rest "")
                break()
            endif()
            string(SUBSTRING "${rest}" 0 ${best} before)
            string(SUBSTRING "${rest}" ${best} -1 from_tok)

            if(best_kind STREQUAL "//")
                if(from_tok MATCHES "^//[ \t]*color-ok:(.*)$")
                    math(EXPR exempt_total "${exempt_total} + 1")
                    string(STRIP "${CMAKE_MATCH_1}" reason)
                    if(reason STREQUAL "")
                        list(APPEND file_hits "${line_no}:color-ok-sans-raison")
                    else()
                        list(APPEND exempt_lines ${line_no})
                    endif()
                endif()
                string(APPEND out "${before}")
                set(rest "")
                break()
            elseif(best_kind STREQUAL "/*")
                string(APPEND out "${before}")
                string(SUBSTRING "${from_tok}" 2 -1 rest)
                set(in_block TRUE)
                continue()
            elseif(best_kind STREQUAL "'")
                # Separateur de chiffres (1'000) ou prefixe : pas un litteral caractere.
                set(is_sep FALSE)
                if(NOT before STREQUAL "" AND before MATCHES "[A-Za-z0-9_]$")
                    set(is_sep TRUE)
                endif()
                if(is_sep)
                    string(APPEND out "${before}'")
                    string(SUBSTRING "${from_tok}" 1 -1 rest)
                    continue()
                endif()
                string(REGEX MATCH [=[^'([^'\]|\\.)*']=] lit "${from_tok}")
                if(lit STREQUAL "")
                    string(APPEND out "${before}")
                    set(rest "")
                    break()
                endif()
                string(LENGTH "${lit}" llen)
                string(SUBSTRING "${from_tok}" ${llen} -1 rest)
                string(APPEND out "${before}'c'")
                continue()
            else()
                # Litteral chaine ; chaine brute si precedee de R.
                if(before MATCHES "R$")
                    if(from_tok MATCHES "^\"([^(]*)[(]")
                        set(raw_close ")${CMAKE_MATCH_1}\"")
                        string(LENGTH "${CMAKE_MATCH_1}" dlen)
                        math(EXPR cut "${dlen} + 2")
                        string(SUBSTRING "${from_tok}" ${cut} -1 rest)
                        set(in_raw TRUE)
                        string(APPEND out "${before}")
                        continue()
                    endif()
                endif()
                string(REGEX MATCH [=[^"([^"\]|\\.)*"]=] lit "${from_tok}")
                if(lit STREQUAL "")
                    string(APPEND out "${before}")
                    set(rest "")
                    break()
                endif()
                string(LENGTH "${lit}" llen)
                string(SUBSTRING "${from_tok}" ${llen} -1 rest)
                math(EXPR inner_len "${llen} - 2")
                string(SUBSTRING "${lit}" 1 ${inner_len} inner)
                scan_rules(file_hits "${inner}" ${line_no} "${literal_rules}")
                string(APPEND out "${before}\"S\"")
                continue()
            endif()
        endwhile()
        string(APPEND code "${out}\n")
    endforeach()

    # Instructions : decoupage au `;` (marqueur <SEMI>), appels multi-lignes joints.
    string(REPLACE "<SEMI>" ";" statements "${code}")
    set(stmt_line 1)
    foreach(stmt IN LISTS statements)
        if(stmt MATCHES "QColor|Qt::|qRgb|setNamedColor")
            scan_rules(file_hits "${stmt}" ${stmt_line} "${code_rules}")
        endif()
        string(REGEX REPLACE "[^\n]" "" stmt_nl "${stmt}")
        string(LENGTH "${stmt_nl}" nb)
        math(EXPR stmt_line "${stmt_line} + ${nb}")
    endforeach()

    # Derogations et comptage.
    set(file_count 0)
    set(file_lines "")
    foreach(hit IN LISTS file_hits)
        string(REGEX MATCH "^[0-9]+" hit_line "${hit}")
        list(FIND exempt_lines "${hit_line}" ex_idx)
        if(NOT ex_idx EQUAL -1)
            continue()
        endif()
        math(EXPR file_count "${file_count} + 1")
        list(APPEND file_lines "${hit_line}")
    endforeach()
    list(REMOVE_DUPLICATES file_lines)

    if(file_count GREATER 0)
        list(APPEND counts_report "${src_name} ${file_count}")
    endif()
    if(use_baseline)
        if(DEFINED base_${src_name})
            set(allowed ${base_${src_name}})
        else()
            set(allowed 0)
        endif()
        if(file_count GREATER allowed)
            foreach(l IN LISTS file_lines)
                list(APPEND all_violations "${src_file}:${l}")
            endforeach()
            list(APPEND baseline_failures
                "${src_name} : ${file_count} motifs > base ${allowed} (couleur en dur ajoutee : utiliser tokens())")
        elseif(file_count LESS allowed)
            list(APPEND baseline_failures
                "${src_name} : ${file_count} motifs < base ${allowed} : abaisser la base dans tests/color_guard_baseline.txt")
        endif()
    else()
        foreach(l IN LISTS file_lines)
            list(APPEND all_violations "${src_file}:${l}")
        endforeach()
    endif()
endforeach()

# Entrees de base sans fichier correspondant : a retirer (cliquet vers vide).
if(use_baseline)
    get_cmake_property(all_vars VARIABLES)
    foreach(v IN LISTS all_vars)
        if(v MATCHES "^base_(.+)$")
            set(fname "${CMAKE_MATCH_1}")
            if(NOT EXISTS "${SRC_DIR}/apps/desktop/${fname}")
                list(APPEND baseline_failures "base : ${fname} n'existe plus, retirer la ligne")
            endif()
        endif()
    endforeach()
endif()

if(DEFINED PRINT_COUNTS)
    string(REPLACE ";" "\n" counts_text "${counts_report}")
    message(STATUS "COMPTES (${exempt_total} derogations) :\n${counts_text}")
    return()
endif()

if(exempt_total GREATER max_exemptions)
    list(APPEND baseline_failures
        "${exempt_total} derogations `// color-ok:` > ${max_exemptions} autorisees dans tout le depot")
endif()

if(all_violations OR baseline_failures)
    list(REMOVE_DUPLICATES all_violations)
    string(REPLACE ";" "\n  " violations_str "${all_violations}")
    string(REPLACE ";" "\n  " failures_str "${baseline_failures}")
    message(FATAL_ERROR
        "Couleur codee en dur dans apps/desktop (utiliser les tokens de design_tokens.hpp : "
        "AppTheme::instance().tokens().xxx, ui::setRole/setVariant pour les etats) :\n  "
        "${violations_str}\n${failures_str}")
endif()

message(STATUS "check_no_hardcoded_colors : OK")
