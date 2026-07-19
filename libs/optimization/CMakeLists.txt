add_library(openstitch_optimization
    src/order.cpp
)
add_library(openstitch::optimization ALIAS openstitch_optimization)

target_include_directories(openstitch_optimization PUBLIC include)
target_link_libraries(openstitch_optimization
    PUBLIC openstitch::core
    PRIVATE openstitch::warnings
)
