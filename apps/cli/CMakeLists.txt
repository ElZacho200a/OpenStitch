find_package(CLI11 CONFIG REQUIRED)

add_executable(openstitch-cli
    main.cpp
    project_cmd.cpp
)
target_link_libraries(openstitch-cli
    PRIVATE CLI11::CLI11 openstitch::core openstitch::image openstitch::formats
            openstitch::stitch openstitch::geometry openstitch::stitch_generation
            openstitch::auto_satin openstitch::stitch_render
            openstitch::segmentation openstitch::project_io openstitch::autodigitize openstitch::stitch_analysis openstitch::project_ops
            openstitch::warnings
)
