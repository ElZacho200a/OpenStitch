add_executable(test_project_io
    test_roundtrip.cpp
    test_overrides_persistence.cpp
)
target_link_libraries(test_project_io
    PRIVATE openstitch::project_io Catch2::Catch2WithMain openstitch::warnings)
# test_overrides_persistence.cpp forge un .osp minimal via detail::write_zip
# (archive.hpp, header interne de openstitch_project_io) pour tester la
# validation stricte et la migration v1/v2 sans reconstruire tout le pipeline
# d'ecriture normal. Aucun type minizip n'est expose par ce header.
target_include_directories(test_project_io PRIVATE ${CMAKE_SOURCE_DIR}/libs/project_io/src)
catch_discover_tests(test_project_io)
