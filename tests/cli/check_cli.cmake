# SPDX-License-Identifier: Apache-2.0
# Tests de contrat de la ligne de commande openstitch-cli (options, messages
# d'erreur, codes de sortie, sortie --json). Lancé par CTest :
#   cmake -DCLI=<exe> -DFIXTURES=<dossier> -DWORK=<dossier> -DCASE=<nom> -P check_cli.cmake
# Aucun besoin de Qt ni d'image volumineuse : le pipeline tourne sur une
# image de quelques centaines de pixels (tests/fixtures/cli/).

if(NOT CLI OR NOT FIXTURES OR NOT WORK OR NOT CASE)
    message(FATAL_ERROR "usage : -DCLI= -DFIXTURES= -DWORK= -DCASE= -P check_cli.cmake")
endif()
file(MAKE_DIRECTORY "${WORK}")

# run(<args...>) -> RC, OUT (stdout), ERR (stderr)
function(run)
    execute_process(COMMAND "${CLI}" ${ARGN}
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err
        WORKING_DIRECTORY "${WORK}")
    # Le journal d'OpenCV (build Debug) peut s'écrire sur stdout avant le JSON : avec --json,
    # on ne garde que le document (à partir de la première accolade).
    list(FIND ARGN "--json" json_flag)
    if(NOT json_flag EQUAL -1)
        string(FIND "${out}" "{" brace)
        if(brace GREATER 0)
            string(SUBSTRING "${out}" ${brace} -1 out)
        endif()
    endif()
    set(RC "${rc}" PARENT_SCOPE)
    set(OUT "${out}" PARENT_SCOPE)
    set(ERR "${err}" PARENT_SCOPE)
endfunction()

function(expect_rc expected)
    if(NOT "${RC}" STREQUAL "${expected}")
        message(FATAL_ERROR "code de sortie ${RC} (attendu ${expected})\nstdout: ${OUT}\nstderr: ${ERR}")
    endif()
endfunction()

function(expect_nonzero)
    if("${RC}" STREQUAL "0")
        message(FATAL_ERROR "code de sortie 0 alors qu'une erreur était attendue\nstdout: ${OUT}")
    endif()
endfunction()

function(expect_match var regex)
    if(NOT "${${var}}" MATCHES "${regex}")
        message(FATAL_ERROR "'${regex}' absent de ${var} :\n${${var}}")
    endif()
endfunction()

set(LOGO "${FIXTURES}/logo_small.png")
set(BLANK "${FIXTURES}/blank.png")

if(CASE STREQUAL "version")
    run(--version)
    expect_rc(0)
    expect_match(OUT "[0-9]+\\.[0-9]+")

elseif(CASE STREQUAL "help")
    run(--help)
    expect_rc(0)
    expect_match(OUT "Diagnostic")
    expect_match(OUT "\\[diagnostic\\] Inspecte")
    expect_match(OUT "Codes de sortie")
    expect_match(OUT "--json")
    # Positionnels (et alias --osp / --output conservés).
    run(osp2dst --help)
    expect_rc(0)
    expect_match(OUT "POSITIONALS")
    expect_match(OUT "--osp")

elseif(CASE STREQUAL "unknown_shape")
    run(satin-auto-debug --shape pas_une_forme)
    expect_rc(1)
    expect_match(ERR "openstitch-cli satin-auto-debug : forme inconnue")
    expect_match(ERR "formes valides : rectangle")

elseif(CASE STREQUAL "list_shapes")
    run(satin-auto-debug --list-shapes)
    expect_rc(0)
    expect_match(OUT "rectangle")
    expect_match(OUT "trident")

elseif(CASE STREQUAL "usage_errors")
    run(stitchdebug --repeats 5)
    expect_nonzero()
    run(satin-auto-debug --vector 3)
    expect_nonzero()
    run(digitize "${LOGO}" "${WORK}/x.dst" --skip-background peut-etre)
    expect_nonzero()

elseif(CASE STREQUAL "stitchdebug")
    run(stitchdebug --shape circle --repeats 2)
    expect_rc(0)
    run(stitchdebug --shape ring)
    expect_rc(0)

elseif(CASE STREQUAL "errors_prefix")
    run(info "${WORK}/inexistant.png")
    expect_rc(1)
    expect_match(ERR "^openstitch-cli info : ")
    expect_match(ERR "Piste : formats acceptés")
    run(stats "${WORK}/inexistant.dst")
    expect_rc(1)
    expect_match(ERR "^openstitch-cli stats : ")
    run(osp2dst "${WORK}/inexistant.osp" "${WORK}/x.dst")
    expect_rc(1)
    expect_match(ERR "^openstitch-cli osp2dst : ")
    run(osp2svg --osp "${WORK}/inexistant.osp" --output "${WORK}/x.svg")
    expect_rc(1)
    expect_match(ERR "^openstitch-cli osp2svg : ")
    run(satin-auto-debug --osp "${WORK}/inexistant.osp" --vector 1)
    expect_rc(1)
    expect_match(ERR "^openstitch-cli satin-auto-debug : ")

elseif(CASE STREQUAL "info_json")
    run(info "${LOGO}" --json)
    expect_rc(0)
    string(JSON dpi GET "${OUT}" dpi)
    string(JSON src GET "${OUT}" dpi_source)
    string(JSON w GET "${OUT}" width_px)
    if(NOT w EQUAL 120)
        message(FATAL_ERROR "width_px = ${w}")
    endif()
    if(NOT dpi GREATER 299 OR NOT src MATCHES "fichier")
        message(FATAL_ERROR "dpi du fichier non lu : ${dpi} (${src})")
    endif()
    # --dpi explicite l'emporte.
    run(info "${LOGO}" --dpi 100 --json)
    expect_rc(0)
    string(JSON dpi GET "${OUT}" dpi)
    if(NOT dpi EQUAL 100)
        message(FATAL_ERROR "--dpi ignoré : ${dpi}")
    endif()
    # Image sans résolution : défaut annoncé.
    run(info "${BLANK}")
    expect_rc(0)
    expect_match(OUT "96 dpi \\(défaut")

elseif(CASE STREQUAL "digitize_json")
    file(REMOVE "${WORK}/logo.dst")
    run(digitize "${LOGO}" "${WORK}/logo.dst" --json)
    expect_rc(0)
    # stdout = un unique document JSON ; le détail lisible est sur stderr.
    string(JSON total GET "${OUT}" objects total)
    string(JSON stitches GET "${OUT}" stitches)
    if(total LESS 1 OR stitches LESS 1)
        message(FATAL_ERROR "digitize --json : objets=${total} points=${stitches}")
    endif()
    expect_match(ERR "Objets brodés")
    # Le DST relu par stats --json donne le même nombre de points.
    run(stats "${WORK}/logo.dst" --json)
    expect_rc(0)
    string(JSON stitches2 GET "${OUT}" stitches)
    if(NOT stitches EQUAL stitches2)
        message(FATAL_ERROR "points digitize=${stitches} stats=${stitches2}")
    endif()
    # --no-clobber refuse d'écraser ; --force (défaut) écrase.
    run(digitize "${LOGO}" "${WORK}/logo.dst" --no-clobber)
    expect_rc(1)
    expect_match(ERR "^openstitch-cli digitize : ")
    run(digitize "${LOGO}" "${WORK}/logo.dst" --force)
    expect_rc(0)
    # Dossier de sortie inexistant.
    run(digitize "${LOGO}" "${WORK}/pas_de_dossier/logo.dst")
    expect_rc(1)
    expect_match(ERR "n'existe pas")

elseif(CASE STREQUAL "digitize_nothing")
    run(digitize "${BLANK}" "${WORK}/blank.dst")
    expect_rc(1)
    expect_match(ERR "^openstitch-cli digitize : ")
    if(EXISTS "${WORK}/blank.dst")
        message(FATAL_ERROR "un DST vide a été écrit")
    endif()

elseif(CASE STREQUAL "legacy_technique_alias")
    run(digitize "${LOGO}" "${WORK}/contours.dst" --mode contours --technique satin
        --skip-background no)
    expect_rc(0)
    expect_match(ERR "legacy-satin")
    run(digitize "${LOGO}" "${WORK}/contours.dst" --mode contours --technique legacy-satin
        --skip-background no)
    expect_rc(0)

else()
    message(FATAL_ERROR "cas inconnu : ${CASE}")
endif()
