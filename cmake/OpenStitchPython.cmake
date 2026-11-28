# SCRIPT-01 : CPython embarqué (pybind11) pour le script de sélection.
#
# OPENSTITCH_WITH_PYTHON est vrai quand pybind11 est trouvé (dépendance vcpkg limitée à
# Windows dans vcpkg.json) ; la CI linux-core compile donc sans Python.
# openstitch_deploy_python(<cible>) copie la bibliothèque standard à côté de l'exécutable
# (`<dossier>/python/Lib`, sans la suite de tests ni les modules graphiques) : c'est là que
# libs/scripting la cherche par défaut. La DLL python3XX est copiée par le déploiement
# applocal de vcpkg comme toute autre dépendance.

find_package(pybind11 CONFIG QUIET)
if(pybind11_FOUND)
    set(OPENSTITCH_WITH_PYTHON ON)
else()
    set(OPENSTITCH_WITH_PYTHON OFF)
    message(STATUS "pybind11 absent : script Python de sélection désactivé (SCRIPT-01)")
endif()

if(OPENSTITCH_WITH_PYTHON AND NOT OPENSTITCH_PYTHON_STDLIB)
    set(_openstitch_stdlib_candidates)
    if(Python_STDLIB)
        list(APPEND _openstitch_stdlib_candidates "${Python_STDLIB}")
    endif()
    if(Python3_STDLIB)
        list(APPEND _openstitch_stdlib_candidates "${Python3_STDLIB}")
    endif()
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        list(APPEND _openstitch_stdlib_candidates
             "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/python3/Lib"
             "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/python3.12")
    endif()
    foreach(_candidate IN LISTS _openstitch_stdlib_candidates)
        if(EXISTS "${_candidate}/encodings/__init__.py")
            set(OPENSTITCH_PYTHON_STDLIB "${_candidate}" CACHE PATH
                "Bibliothèque standard Python copiée à côté des exécutables (SCRIPT-01)")
            break()
        endif()
    endforeach()
    if(NOT OPENSTITCH_PYTHON_STDLIB)
        message(WARNING "SCRIPT-01 : bibliothèque standard Python introuvable "
                        "(définir OPENSTITCH_PYTHON_STDLIB) ; les scripts échoueront "
                        "avec E_SCRIPT_UNAVAILABLE")
    endif()
endif()

function(openstitch_deploy_python target)
    if(NOT OPENSTITCH_WITH_PYTHON OR NOT OPENSTITCH_PYTHON_STDLIB)
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                "-DSOURCE=${OPENSTITCH_PYTHON_STDLIB}"
                "-DDESTINATION=$<TARGET_FILE_DIR:${target}>/python/Lib"
                -P "${PROJECT_SOURCE_DIR}/cmake/deploy_python_stdlib.cmake"
        VERBATIM)
endfunction()
