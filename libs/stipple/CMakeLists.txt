add_library(openstitch_stipple
    src/density.cpp
    src/stipple.cpp
    src/tour.cpp
)
add_library(openstitch::stipple ALIAS openstitch_stipple)

target_include_directories(openstitch_stipple PUBLIC include)
target_link_libraries(openstitch_stipple
    PUBLIC openstitch::core openstitch::image
    PRIVATE openstitch::warnings
)
