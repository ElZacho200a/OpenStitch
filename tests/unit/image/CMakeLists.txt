# Ce test utilise OpenCV directement pour FABRIQUER des fichiers d'essai
# (usage test uniquement — le code produit ne voit OpenCV que dans libs/image).
find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgcodecs)

add_executable(test_image
    test_image_load.cpp
    test_ops.cpp
)
target_link_libraries(test_image
    PRIVATE openstitch::image opencv_core opencv_imgcodecs Catch2::Catch2WithMain openstitch::warnings)
catch_discover_tests(test_image)
