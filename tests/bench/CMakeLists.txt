# Banc de mesure des performances (pas un test CTest) -- cf.
# docs/performance-audit.md. À lancer sur une build Release :
#   build\msvc\tests\bench\Release\openstitch-bench.exe tests\fixtures\tentabrode.png
add_executable(openstitch-bench EXCLUDE_FROM_ALL
    openstitch_bench.cpp
)
target_link_libraries(openstitch-bench
    PRIVATE openstitch::image openstitch::segmentation openstitch::vectorization
            openstitch::autodigitize openstitch::stitch_generation openstitch::formats
            openstitch::project_io openstitch::optimization openstitch::warnings)
if(WIN32)
    target_link_libraries(openstitch-bench PRIVATE psapi)
endif()
