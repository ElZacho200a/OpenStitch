add_executable(openstitch-mcp main.cpp)
target_link_libraries(openstitch-mcp PRIVATE openstitch::mcp openstitch::warnings)
