find_package(Freetype REQUIRED)

add_library(openstitch_lettering
    src/font.cpp
    src/layout.cpp
    src/build.cpp
)
add_library(openstitch::lettering ALIAS openstitch_lettering)

target_include_directories(openstitch_lettering PUBLIC include)
# FreeType en PRIVATE : aucun type FreeType ne sort de cette bibliothèque (même
# règle que Clipper2 dans geometry). Licence FTL, jamais l'option GPLv2.
target_link_libraries(openstitch_lettering
    PUBLIC openstitch::core openstitch::geometry openstitch::document
    PRIVATE Freetype::Freetype openstitch::auto_satin openstitch::warnings
)
