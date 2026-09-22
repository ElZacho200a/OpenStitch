find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc)

add_executable(test_vectorization
    test_vectorize.cpp
    test_vectorize_equivalence.cpp
)
# OpenCV : uniquement pour l'implémentation de référence recopiée dans
# test_vectorize_equivalence.cpp (audit perf 2026-09).
target_link_libraries(test_vectorization
    PRIVATE openstitch::vectorization opencv_core opencv_imgproc Catch2::Catch2WithMain
            openstitch::warnings)
catch_discover_tests(test_vectorization)
