find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc)
# Boost.Polygon (BSL-1.0) : diagramme de Voronoi de segments, encapsulé dans medial_axis.cpp.
find_package(boost_polygon CONFIG REQUIRED)

add_library(openstitch_auto_satin
    src/raster.cpp
    src/distance_field.cpp
    src/skeleton.cpp
    src/skeleton_graph.cpp
    src/graph_cleanup.cpp
    src/satinability.cpp
    src/auto_satin.cpp
    src/shapes.cpp
    src/chord.cpp
    src/axis.cpp
    src/axis_sampler.cpp
    src/skeleton_satin.cpp
    src/coverage.cpp
    src/medial_axis.cpp
    src/parallel_sweep.cpp
    src/satin_strategy.cpp
)
add_library(openstitch::auto_satin ALIAS openstitch_auto_satin)

target_include_directories(openstitch_auto_satin PUBLIC include)
# OpenCV en PRIVATE : rasterisation + transformée de distance encapsulées.
target_link_libraries(openstitch_auto_satin
    PUBLIC openstitch::core openstitch::geometry
    PRIVATE opencv_core opencv_imgproc Boost::polygon openstitch::warnings
)
