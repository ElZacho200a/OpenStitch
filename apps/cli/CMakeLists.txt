find_package(CLI11 CONFIG REQUIRED)

add_executable(openstitch-cli
    main.cpp
)
target_link_libraries(openstitch-cli
    PRIVATE CLI11::CLI11 openstitch::core openstitch::image openstitch::formats
            openstitch::stitch openstitch::warnings
)
