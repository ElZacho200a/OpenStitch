find_package(fmt CONFIG REQUIRED)

add_library(openstitch_formats
    src/dst.cpp
    src/dxf.cpp
    src/svg.cpp
)
add_library(openstitch::formats ALIAS openstitch_formats)

target_include_directories(openstitch_formats PUBLIC include)
target_link_libraries(openstitch_formats
    PUBLIC openstitch::core openstitch::stitch openstitch::geometry
    PRIVATE fmt::fmt openstitch::warnings
)
