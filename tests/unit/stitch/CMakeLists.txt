add_executable(test_stitch
    test_stats.cpp
    test_color_block.cpp
    test_running_stitch.cpp
    test_generate.cpp
    test_finish.cpp
    test_length_bounds.cpp
    test_join_closest.cpp
    test_knockout.cpp
    test_hidden_travel.cpp
    test_tatami.cpp
    test_directional_fill.cpp
    test_blanket_stitch.cpp
    test_satin.cpp
    test_satin_guides.cpp
    test_satin_pairing_metrics.cpp
    test_routing.cpp
    test_overrides.cpp
    test_point_insert_delete.cpp
    test_engine_points.cpp
    test_pull_auto.cpp
    test_pull_other_fills.cpp
    test_adaptive_pull.cpp
    test_pull_field.cpp
    test_pull_map.cpp
    test_pull_test_piece.cpp
    test_predicted_pull.cpp
    test_chunk_cache.cpp
    test_calques_generation.cpp
    test_contour_fill.cpp
    test_motif.cpp
    test_radial_spiral_crosshatch.cpp
    test_cross_stitch.cpp
)
target_link_libraries(test_stitch
    PRIVATE openstitch::stitch openstitch::stitch_generation Catch2::Catch2WithMain
            openstitch::warnings)
catch_discover_tests(test_stitch)

# HP-QA-011 : tout tri instable des moteurs de points doit porter une annotation
# `total-order-ok:` (cle totale) -- voir tests/check_sort_total_order.cmake.
add_test(NAME check_sort_total_order
    COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${PROJECT_SOURCE_DIR}
            -P ${PROJECT_SOURCE_DIR}/tests/check_sort_total_order.cmake)
