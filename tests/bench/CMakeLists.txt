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

# Mesures opt-in du parcours CLI complet, hors CTest et sans Qt (R01-01).
find_package(nlohmann_json CONFIG REQUIRED)
add_executable(openstitch-profile-engine EXCLUDE_FROM_ALL profile_engine.cpp)
target_link_libraries(openstitch-profile-engine
    PRIVATE openstitch::project_ops openstitch::autodigitize openstitch::stitch_render
            nlohmann_json::nlohmann_json openstitch::warnings)
if(WIN32)
    target_link_libraries(openstitch-profile-engine PRIVATE psapi)
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

# Captures d'écran headless de l'interface pour les audits d'ergonomie (hors build par défaut) :
#   cmake --build --preset msvc-release --target openstitch-ui-shots
#   build\msvc\tests\bench\Release\openstitch-ui-shots.exe projet.osp dossier-sortie
if(TARGET openstitch_desktop_widgets)
    add_executable(openstitch-ui-shots EXCLUDE_FROM_ALL ui_shots.cpp)
    target_link_libraries(openstitch-ui-shots
        PRIVATE openstitch::desktop_widgets openstitch::project_io openstitch::warnings)
endif()

# Native-window comparison: Quick swap cadence versus the existing V2 raster canvas.
# Kept out of the default build/CTest: needs a visible, unobscured desktop for measurements.
if(TARGET openstitch_studio_ui AND TARGET openstitch_desktop_v2_ui)
    add_executable(openstitch-studio-bench EXCLUDE_FROM_ALL studio_bench.cpp)
    target_link_libraries(openstitch-studio-bench
        PRIVATE openstitch::studio_ui openstitch::desktop_v2_ui openstitch::warnings)
    if(WIN32)
        target_link_libraries(openstitch-studio-bench PRIVATE psapi)
    endif()
endif()
