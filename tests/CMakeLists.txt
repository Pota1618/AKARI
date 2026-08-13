add_executable(akari_test_dependencies dependency_smoke.cpp)
target_compile_features(akari_test_dependencies PRIVATE cxx_std_23)
target_link_libraries(akari_test_dependencies PRIVATE glfw glm::glm)

add_executable(akari_test_core core_unit.cpp)
target_compile_features(akari_test_core PRIVATE cxx_std_23)
target_link_libraries(akari_test_core PRIVATE akari_core)

add_executable(akari_test_image_io image_io_unit.cpp)
target_compile_features(akari_test_image_io PRIVATE cxx_std_23)
target_link_libraries(akari_test_image_io PRIVATE akari_image_io)

add_executable(akari_test_vulkan_preview vulkan_preview_smoke.cpp)
target_compile_features(akari_test_vulkan_preview PRIVATE cxx_std_23)
target_link_libraries(akari_test_vulkan_preview PRIVATE akari_core akari_vulkan_preview glfw)

add_executable(akari_test_vulkan_offscreen vulkan_offscreen_smoke.cpp)
target_compile_features(akari_test_vulkan_offscreen PRIVATE cxx_std_23)
target_link_libraries(akari_test_vulkan_offscreen PRIVATE akari_core akari_vulkan)

akari_copy_shader_assets(akari_test_vulkan_preview)
akari_copy_shader_assets(akari_test_vulkan_offscreen)

add_test(NAME dependencies.smoke COMMAND akari_test_dependencies)
add_test(NAME core.unit COMMAND akari_test_core)
add_test(NAME image_io.unit COMMAND akari_test_image_io)
add_test(NAME vulkan.preview COMMAND akari_test_vulkan_preview)
set_tests_properties(vulkan.preview PROPERTIES LABELS "gpu;vulkan;preview" TIMEOUT 30)
add_test(NAME vulkan.offscreen COMMAND akari_test_vulkan_offscreen)
set_tests_properties(vulkan.offscreen PROPERTIES LABELS "gpu;vulkan;offscreen" TIMEOUT 30)
add_test(
    NAME capture.cli
    COMMAND akari_capture
        --output "${CMAKE_CURRENT_BINARY_DIR}/capture_cli_test.png"
        --time 1.57079632679
        --width 64
        --height 64
)
set_tests_properties(capture.cli PROPERTIES LABELS "gpu;vulkan;offscreen;cli" TIMEOUT 30)
