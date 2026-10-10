find_package(fmt CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)

add_library(openstitch_formats
    src/machine.cpp
    src/format_registry.cpp
    src/dst.cpp
    src/codec_common.cpp
    src/machine_palettes.cpp
    src/pes.cpp
    src/jef.cpp
    src/exp.cpp
    src/export_limits.cpp
    src/dxf.cpp
    src/svg.cpp
    src/svg_import.cpp
)
add_library(openstitch::formats ALIAS openstitch_formats)

target_include_directories(openstitch_formats PUBLIC include)
target_link_libraries(openstitch_formats
    PUBLIC openstitch::core openstitch::stitch openstitch::geometry openstitch::thread_palette
    PRIVATE fmt::fmt pugixml::pugixml openstitch::warnings
)
