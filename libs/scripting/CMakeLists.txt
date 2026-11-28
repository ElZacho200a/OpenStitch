find_package(nlohmann_json CONFIG REQUIRED)

# Script Python sur une sélection d'objets (SCRIPT-01) : seule bibliothèque qui embarque
# CPython (pybind11). Le script lit les objets (JSON d'inspection de project_ops) et rend un
# lot d'opérations ; il ne modifie jamais le document lui-même. Qt-free.
add_library(openstitch_scripting
    src/selection_script.cpp
    src/prelude.cpp
)
add_library(openstitch::scripting ALIAS openstitch_scripting)

target_include_directories(openstitch_scripting PUBLIC include)
target_link_libraries(openstitch_scripting
    PUBLIC openstitch::core openstitch::document openstitch::project_ops
    PRIVATE pybind11::embed nlohmann_json::nlohmann_json openstitch::warnings
)
