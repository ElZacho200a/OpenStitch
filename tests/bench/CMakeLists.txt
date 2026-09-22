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

# Banc de réactivité de l'interface (headless), seulement si le desktop est
# construit. Lancer avec le PATH Qt, p. ex. :
#   $env:PATH = "$env:QT_ROOT\bin;$env:PATH"
#   build\msvc\tests\bench\Release\openstitch-bench-ui.exe projet.osp
if(TARGET openstitch_desktop_widgets)
    add_executable(openstitch-bench-ui bench_main_window.cpp)
    target_link_libraries(openstitch-bench-ui
        PRIVATE openstitch::desktop_widgets openstitch::project_io openstitch::warnings)
endif()
