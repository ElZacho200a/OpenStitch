find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc imgcodecs)

add_library(openstitch_image
    src/load.cpp
    src/ops.cpp
    src/mat_convert.cpp
)
add_library(openstitch::image ALIAS openstitch_image)

target_include_directories(openstitch_image PUBLIC include)
# OpenCV en PRIVATE : aucun type OpenCV ne sort de cette bibliothèque (ADR-006).
target_link_libraries(openstitch_image
    PUBLIC openstitch::core
    PRIVATE opencv_core opencv_imgproc opencv_imgcodecs openstitch::warnings
)
