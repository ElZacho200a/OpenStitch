# Garde structurelle (Lot 8.1, cf. docs/lot8-manual-editing-design.md SS5) :
# `stitch_generation::effective_sequence(project)` est le SEUL point d'entree
# de production pour obtenir la sequence d'un projet (apercu/analyse/export/
# simulation/CLI) -- il enchaine `generate_sequence` (brut) puis
# `apply_manual_overrides` (retouches manuelles). Un consommateur qui appelle
# `generate_sequence` directement contournerait silencieusement les retouches
# de l'utilisateur.
#
# Plutot qu'un grep aveugle sur tout le depot, ce script :
#  - ignore libs/stitch_generation/ (implementation interne legitime de
#    generate_sequence/effective_sequence) ;
#  - ignore tests/ (fixtures/generateurs synthetiques, jamais un vrai
#    projet utilisateur) puisque le glob ne porte que sur apps/ et libs/ ;
#  - exige, pour tout autre site d'appel, une annotation explicite
#    `raw-sequence-ok: <raison>` sur la ligne de l'appel ou la ligne
#    precedente -- documentant pourquoi ce site precis n'a pas besoin des
#    retouches (ex. generateur de debug qui construit un projet synthetique
#    jamais charge/sauvegarde). Sans cette annotation, le test echoue avec le
#    fichier:ligne exact.
#
# Usage : cmake -DSRC_DIR=<racine du depot> -P check_no_raw_sequence_bypass.cmake

if(NOT DEFINED SRC_DIR)
    message(FATAL_ERROR "SRC_DIR non defini")
endif()

file(GLOB_RECURSE candidates
    "${SRC_DIR}/apps/*.cpp" "${SRC_DIR}/apps/*.hpp"
    "${SRC_DIR}/libs/*.cpp" "${SRC_DIR}/libs/*.hpp")

set(violations "")

foreach(src_file ${candidates})
    if(src_file MATCHES "/libs/stitch_generation/")
        continue()
    endif()

    file(STRINGS "${src_file}" lines)
    set(line_no 0)
    set(prev_line "")
    foreach(line ${lines})
        math(EXPR line_no "${line_no} + 1")
        # La partie apres // est un commentaire : ignoree pour la detection
        # (permet de nommer effective_sequence/generate_sequence en prose).
        string(REGEX REPLACE "//.*$" "" code_only "${line}")
        if(code_only MATCHES "generate_sequence[ \t]*\\(")
            if((NOT line MATCHES "raw-sequence-ok") AND (NOT prev_line MATCHES "raw-sequence-ok"))
                list(APPEND violations "${src_file}:${line_no}")
            endif()
        endif()
        set(prev_line "${line}")
    endforeach()
endforeach()

if(violations)
    string(REPLACE ";" "\n  " violations_str "${violations}")
    message(FATAL_ERROR
        "Appel direct a generate_sequence() detecte hors de "
        "libs/stitch_generation/, sans annotation 'raw-sequence-ok:'. Ceci "
        "contournerait silencieusement les retouches manuelles de "
        "l'utilisateur (Lot 8.1). Utiliser "
        "stitch_generation::effective_sequence(project) a la place, ou "
        "ajouter un commentaire '// raw-sequence-ok: <raison>' si l'appel "
        "est deliberement un generateur/test synthetique :\n  ${violations_str}")
endif()

message(STATUS "check_no_raw_sequence_bypass : OK")
