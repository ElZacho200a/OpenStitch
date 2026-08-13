add_executable(test_satin_planning
    test_branch_pairing.cpp
)
target_link_libraries(test_satin_planning
    PRIVATE openstitch::satin_planning openstitch::auto_satin Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_satin_planning)
