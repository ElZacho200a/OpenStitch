add_executable(test_auto_satin
    test_pipeline.cpp
    test_skeleton_equivalence.cpp
    test_skeleton_satin.cpp
    test_skeleton_satin_generation.cpp
)
target_link_libraries(test_auto_satin
    PRIVATE openstitch::auto_satin openstitch::stitch_generation
            Catch2::Catch2WithMain openstitch::warnings)
# Les tests du moteur par squelette incluent des en-têtes internes (chord, axis, orientation).
target_include_directories(test_auto_satin PRIVATE ${CMAKE_SOURCE_DIR}/libs/auto_satin/src)
catch_discover_tests(test_auto_satin)
