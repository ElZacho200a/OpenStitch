# Garde de dérive (lot L5, specs/plans/ui-interaction-model.md §4) : le bloc
# situé entre <!-- GESTURES:BEGIN --> et <!-- GESTURES:END --> dans
# docs/source/user-guide.md doit être IDENTIQUE, octet pour octet, à la sortie
# de `openstitch_gesture_table --markdown` (contenu exact, pas un grep partiel).
#
# Régénération : openstitch_gesture_table --markdown, puis recopier la sortie
# entre les marqueurs (la sortie se termine par un saut de ligne ; les marqueurs
# sont chacun sur leur propre ligne).
#
# Usage : cmake -DTOOL=<openstitch_gesture_table> -DDOC=<user-guide.md> -P check_gesture_docs.cmake

if(NOT DEFINED TOOL OR NOT DEFINED DOC)
    message(FATAL_ERROR "TOOL et DOC doivent etre definis")
endif()

execute_process(COMMAND "${TOOL}" --markdown
    ENCODING UTF-8
    OUTPUT_VARIABLE generated
    RESULT_VARIABLE rc
    ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "openstitch_gesture_table a echoue (${rc}) : ${err}")
endif()

string(FIND "${generated}" "\r" cr_pos)
if(NOT cr_pos EQUAL -1)
    message(FATAL_ERROR "La sortie du generateur contient des CR : elle doit etre en LF pur")
endif()

file(READ "${DOC}" doc)
# Normalise les fins de ligne (autocrlf Windows) des deux cotes.
string(REPLACE "\r\n" "\n" doc "${doc}")
string(REPLACE "\r\n" "\n" generated "${generated}")
# Sortie attendue en LF pur (le generateur passe stdout en binaire sous Windows).

set(begin_marker "<!-- GESTURES:BEGIN -->\n")
set(end_marker "<!-- GESTURES:END -->")
string(FIND "${doc}" "${begin_marker}" begin_pos)
string(FIND "${doc}" "${end_marker}" end_pos)
if(begin_pos EQUAL -1 OR end_pos EQUAL -1 OR end_pos LESS begin_pos)
    message(FATAL_ERROR "Marqueurs GESTURES:BEGIN / GESTURES:END introuvables dans ${DOC}")
endif()

string(LENGTH "${begin_marker}" begin_len)
math(EXPR start "${begin_pos} + ${begin_len}")
math(EXPR len "${end_pos} - ${start}")
string(SUBSTRING "${doc}" ${start} ${len} block)

if(NOT block STREQUAL generated)
    message(FATAL_ERROR
        "La table des gestes de ${DOC} est perimee par rapport a interaction_map.cpp.\n"
        "Regenerez-la : openstitch_gesture_table --markdown, puis recopiez la sortie entre "
        "<!-- GESTURES:BEGIN --> et <!-- GESTURES:END -->.")
endif()
