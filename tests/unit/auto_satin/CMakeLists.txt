add_executable(test_auto_satin
    test_pipeline.cpp
    test_columns.cpp
    test_coverage_regression.cpp
    test_satin_column_view.cpp
    test_skeleton_equivalence.cpp
    test_medial_field.cpp
)
target_link_libraries(test_auto_satin
    PRIVATE openstitch::auto_satin openstitch::stitch_generation openstitch::satin_coverage
            Catch2::Catch2WithMain openstitch::warnings)
# test_medial_field.cpp teste nearest_boundary_feet directement sur
# P2/Poly/BoundaryFoot (medial_field.hpp, geometry_detail.hpp), des en-têtes
# internes à openstitch_auto_satin (HP-STI-018 Phase A) -- même pattern que
# tests/unit/project_io pour archive.hpp.
target_include_directories(test_auto_satin PRIVATE ${CMAKE_SOURCE_DIR}/libs/auto_satin/src)
catch_discover_tests(test_auto_satin)
