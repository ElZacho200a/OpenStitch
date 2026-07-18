find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc)

add_library(openstitch_segmentation
    src/segmentation.cpp
)
add_library(openstitch::segmentation ALIAS openstitch_segmentation)

target_include_directories(openstitch_segmentation PUBLIC include)
# OpenCV en PRIVATE : aucun type OpenCV ne sort de cette bibliothèque (ADR-006).
target_link_libraries(openstitch_segmentation
    PUBLIC openstitch::core openstitch::image
    PRIVATE opencv_core opencv_imgproc openstitch::warnings
)
