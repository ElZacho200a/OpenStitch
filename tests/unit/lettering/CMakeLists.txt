add_executable(test_lettering
    test_font.cpp
    test_layout.cpp
    test_build.cpp
    test_text_commands.cpp
    test_alphabet.cpp
)
target_link_libraries(test_lettering
    PRIVATE openstitch::lettering openstitch::commands openstitch::project_io
            openstitch::stitch_generation openstitch::stitch_analysis
            Catch2::Catch2WithMain openstitch::warnings)
# Polices libres embarquées (resources/fonts/) : Bitstream Vera, licence permissive.
target_compile_definitions(test_lettering PRIVATE
    OPENSTITCH_FONT_DIR="${CMAKE_SOURCE_DIR}/resources/fonts")
catch_discover_tests(test_lettering)
