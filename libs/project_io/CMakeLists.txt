find_package(nlohmann_json CONFIG REQUIRED)
find_package(minizip-ng CONFIG REQUIRED)

add_library(openstitch_project_io
    src/json_serialize.cpp
    src/archive.cpp
    src/project_io.cpp
    src/machine_file.cpp
    src/thread_chart_io.cpp
)
add_library(openstitch::project_io ALIAS openstitch_project_io)

target_include_directories(openstitch_project_io PUBLIC include)
# json et minizip encapsulés : n'apparaissent pas dans l'API publique.
target_link_libraries(openstitch_project_io
    PUBLIC openstitch::core openstitch::document openstitch::image
           openstitch::stitch_generation openstitch::stitch_analysis openstitch::formats
    PRIVATE nlohmann_json::nlohmann_json MINIZIP::minizip-ng openstitch::warnings
)
