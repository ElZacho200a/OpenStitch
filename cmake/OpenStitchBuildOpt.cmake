# BUILD-OPT-01.6 : PCH selectifs et Unity Build opt-in (docs/source/build-pch-unity.md).
# Tout est OFF par defaut : sans option, la configuration est identique a avant.
#
#   -DOPENSTITCH_PCH=ON                      active les PCH des cibles de OPENSTITCH_PCH_TARGETS
#   -DOPENSTITCH_PCH_TARGETS="a;b"           (defaut : candidats mesures, voir la doc)
#   -DOPENSTITCH_UNITY_TARGETS="a;b"         Unity Build uniquement pour ces cibles (defaut : aucune)
#   -DOPENSTITCH_UNITY_BATCH=8               fichiers .cpp par unite
#
# Chaque cible PCH lit cmake/pch/<nom sans prefixe openstitch_>.hpp.
option(OPENSTITCH_PCH "Active les en-tetes precompiles selectifs (opt-in)" OFF)
set(OPENSTITCH_PCH_TARGETS "openstitch_geometry;openstitch_stitch_generation;openstitch_stitch_analysis"
    CACHE STRING "Cibles recevant un PCH quand OPENSTITCH_PCH=ON")
set(OPENSTITCH_UNITY_TARGETS "" CACHE STRING "Cibles compilees en Unity Build (liste vide = aucune)")
set(OPENSTITCH_UNITY_BATCH "8" CACHE STRING "Taille de lot Unity")

function(openstitch_apply_build_opt)
    foreach(tgt IN LISTS ARGN)
        if(NOT TARGET ${tgt})
            continue()
        endif()
        if(OPENSTITCH_PCH AND tgt IN_LIST OPENSTITCH_PCH_TARGETS)
            string(REGEX REPLACE "^openstitch_" "" short "${tgt}")
            set(hdr "${PROJECT_SOURCE_DIR}/cmake/pch/${short}.hpp")
            if(EXISTS "${hdr}")
                target_precompile_headers(${tgt} PRIVATE "${hdr}")
            endif()
        endif()
        if(tgt IN_LIST OPENSTITCH_UNITY_TARGETS)
            set_target_properties(${tgt} PROPERTIES
                UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE ${OPENSTITCH_UNITY_BATCH})
        endif()
    endforeach()
endfunction()
