find_package(nlohmann_json CONFIG REQUIRED)

# Guide de numérisation embarqué : le skill `openstitch-digitize` (SKILL.md + references/*.md)
# est LA source de vérité ; on l'intègre à l'exécutable à la configuration (octets en
# hexadécimal, pas de limite de taille de littéral sous MSVC) pour que tout client MCP y accède
# sans copie à maintenir. Un test compare l'embarqué aux fichiers.
set(OPENSTITCH_GUIDE_DIR "${PROJECT_SOURCE_DIR}/.claude/skills/openstitch-digitize")
file(GLOB GUIDE_REFERENCES CONFIGURE_DEPENDS "${OPENSTITCH_GUIDE_DIR}/references/*.md")
list(SORT GUIDE_REFERENCES)
set(GUIDE_FILES "${OPENSTITCH_GUIDE_DIR}/SKILL.md" ${GUIDE_REFERENCES})

set(GUIDE_TMP "${CMAKE_CURRENT_BINARY_DIR}/guide_data.cpp.tmp")
set(GUIDE_CPP "${CMAKE_CURRENT_BINARY_DIR}/guide_data.cpp")
set(GUIDE_BODY "// Généré par libs/mcp/CMakeLists.txt : ne pas modifier.\n#include \"guide.hpp\"\n\nnamespace openstitch::mcp {\nnamespace {\n")
set(GUIDE_TABLE "")
set(GUIDE_INDEX 0)
foreach(file IN LISTS GUIDE_FILES)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
    get_filename_component(base "${file}" NAME_WE)
    if(base STREQUAL "SKILL")
        set(topic "workflow")
    else()
        set(topic "${base}")
    endif()
    file(READ "${file}" hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(APPEND GUIDE_BODY "const unsigned char kBlob${GUIDE_INDEX}[] = {${bytes}0x00};\n")
    string(APPEND GUIDE_TABLE "    {\"${topic}\", kBlob${GUIDE_INDEX}, sizeof(kBlob${GUIDE_INDEX}) - 1},\n")
    math(EXPR GUIDE_INDEX "${GUIDE_INDEX} + 1")
endforeach()
string(APPEND GUIDE_BODY "} // namespace\n\nconst GuideBlob kGuideBlobs[] = {\n${GUIDE_TABLE}};\nconst std::size_t kGuideBlobCount = ${GUIDE_INDEX};\n\n} // namespace openstitch::mcp\n")
file(WRITE "${GUIDE_TMP}" "${GUIDE_BODY}")
configure_file("${GUIDE_TMP}" "${GUIDE_CPP}" COPYONLY) # ne touche pas au fichier s'il est identique

# Serveur MCP (outils pour agents IA) au-dessus de project_ops. Qt-free ; JSON encapsulé :
# l'API publique n'échange que des std::string.
add_library(openstitch_mcp
    src/server.cpp
    src/tools.cpp
    src/guide.cpp
    "${GUIDE_CPP}"
)
add_library(openstitch::mcp ALIAS openstitch_mcp)

target_include_directories(openstitch_mcp PUBLIC include PRIVATE src)
target_link_libraries(openstitch_mcp
    PUBLIC openstitch::core
    PRIVATE nlohmann_json::nlohmann_json openstitch::project_ops openstitch::document
            openstitch::project_io openstitch::image openstitch::stitch
            openstitch::autodigitize openstitch::segmentation
            openstitch::stitch_generation openstitch::ai_segmentation openstitch::warnings
)
