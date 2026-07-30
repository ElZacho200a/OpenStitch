find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc)

add_library(openstitch_auto_satin
    src/raster.cpp
    src/distance_field.cpp
    src/skeleton.cpp
    src/skeleton_graph.cpp
    src/graph_cleanup.cpp
    src/satinability.cpp
    src/auto_satin.cpp
    src/satin_column.cpp
    src/debug_export.cpp
    src/shapes.cpp
)
add_library(openstitch::auto_satin ALIAS openstitch_auto_satin)

target_include_directories(openstitch_auto_satin PUBLIC include)
# OpenCV en PRIVATE : rasterisation + transformée de distance encapsulées.
target_link_libraries(openstitch_auto_satin
    PUBLIC openstitch::core openstitch::geometry
    PRIVATE opencv_core opencv_imgproc openstitch::warnings
)
