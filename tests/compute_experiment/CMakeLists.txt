find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc)

# Host-only R01 experiment. No production target depends on this library.
add_library(openstitch_compute_experiment STATIC compute.cpp)
target_include_directories(openstitch_compute_experiment PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openstitch_compute_experiment
    PRIVATE opencv_core opencv_imgproc openstitch::warnings)

add_executable(test_compute_experiment test_compute.cpp)
target_link_libraries(test_compute_experiment
    PRIVATE openstitch_compute_experiment opencv_core opencv_imgproc
            Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_compute_experiment)
