add_library(openstitch_document
    src/image_placement.cpp
    src/vector_object.cpp
)
add_library(openstitch::document ALIAS openstitch_document)

target_include_directories(openstitch_document PUBLIC include)
target_link_libraries(openstitch_document
    PUBLIC openstitch::core openstitch::geometry openstitch::image openstitch::segmentation
    PRIVATE openstitch::warnings
)
