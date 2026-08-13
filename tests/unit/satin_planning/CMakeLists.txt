add_executable(test_satin_planning
    test_branch_pairing.cpp
    test_region_split.cpp
    test_region_satinability.cpp
    test_region_oracle.cpp
    test_beam_search.cpp
    test_merge_pass.cpp
    test_overlap.cpp
    test_region_routing.cpp
)
target_link_libraries(test_satin_planning
    PRIVATE openstitch::satin_planning openstitch::auto_satin openstitch::satin_coverage
            Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_satin_planning)
