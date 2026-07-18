find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc)

add_library(openstitch_vectorization
    src/vectorize.cpp
)
add_library(openstitch::vectorization ALIAS openstitch_vectorization)

target_include_directories(openstitch_vectorization PUBLIC include)
target_link_libraries(openstitch_vectorization
    PUBLIC openstitch::core openstitch::geometry openstitch::segmentation
    PRIVATE opencv_core opencv_imgproc openstitch::warnings
)
