add_library(akari_test_support STATIC support/image_compare.cpp)
target_compile_features(akari_test_support PUBLIC cxx_std_23)
target_include_directories(akari_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" PRIVATE "${stb_SOURCE_DIR}")
target_link_libraries(akari_test_support PUBLIC akari_image_io)

add_executable(akari_test_dependencies dependency_smoke.cpp)
target_compile_features(akari_test_dependencies PRIVATE cxx_std_23)
target_link_libraries(akari_test_dependencies PRIVATE glm::glm)

add_executable(akari_test_core core_unit.cpp)
target_compile_features(akari_test_core PRIVATE cxx_std_23)
target_link_libraries(akari_test_core PRIVATE akari_core)

add_executable(akari_test_timeline timeline_unit.cpp)
target_compile_features(akari_test_timeline PRIVATE cxx_std_23)
target_link_libraries(akari_test_timeline PRIVATE akari_core)

add_executable(akari_test_scene_session scene_session_unit.cpp)
target_compile_features(akari_test_scene_session PRIVATE cxx_std_23)
target_link_libraries(akari_test_scene_session PRIVATE akari_core)

add_executable(akari_test_scene_model scene_model_unit.cpp)
target_compile_features(akari_test_scene_model PRIVATE cxx_std_23)
target_link_libraries(akari_test_scene_model PRIVATE akari_core)

add_executable(akari_test_simulation simulation_unit.cpp)
target_compile_features(akari_test_simulation PRIVATE cxx_std_23)
target_link_libraries(akari_test_simulation PRIVATE akari_core)

add_executable(akari_test_image_io image_io_unit.cpp)
target_compile_features(akari_test_image_io PRIVATE cxx_std_23)
target_link_libraries(akari_test_image_io PRIVATE akari_image_io)

add_executable(akari_test_image_compare image_compare_unit.cpp)
target_compile_features(akari_test_image_compare PRIVATE cxx_std_23)
target_link_libraries(akari_test_image_compare PRIVATE akari_test_support)

set(AKARI_TEST_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/test-working")
file(MAKE_DIRECTORY "${AKARI_TEST_WORKING_DIRECTORY}")

add_test(NAME dependencies.smoke COMMAND akari_test_dependencies)
set_tests_properties(dependencies.smoke PROPERTIES LABELS "cpu;dependency")
add_test(NAME core.unit COMMAND akari_test_core)
set_tests_properties(core.unit PROPERTIES LABELS "cpu")
add_test(NAME timeline.unit COMMAND akari_test_timeline)
set_tests_properties(timeline.unit PROPERTIES LABELS "cpu")
add_test(NAME scene_session.unit COMMAND akari_test_scene_session)
set_tests_properties(scene_session.unit PROPERTIES LABELS "cpu")
add_test(NAME scene_model.unit COMMAND akari_test_scene_model)
set_tests_properties(scene_model.unit PROPERTIES LABELS "cpu")
add_test(NAME simulation.unit COMMAND akari_test_simulation)
set_tests_properties(simulation.unit PROPERTIES LABELS "cpu")
add_test(NAME image_io.unit COMMAND akari_test_image_io)
set_tests_properties(image_io.unit PROPERTIES LABELS "cpu;image")
add_test(NAME image_compare.unit COMMAND akari_test_image_compare)
set_tests_properties(image_compare.unit PROPERTIES LABELS "cpu;image")

if(AKARI_ENABLE_VULKAN)
    add_executable(akari_test_vulkan_preview vulkan_preview_smoke.cpp)
    target_compile_features(akari_test_vulkan_preview PRIVATE cxx_std_23)
    target_link_libraries(akari_test_vulkan_preview PRIVATE akari_core akari_vulkan_preview glfw)

    add_executable(akari_test_vulkan_offscreen vulkan_offscreen_smoke.cpp)
    target_compile_features(akari_test_vulkan_offscreen PRIVATE cxx_std_23)
    target_link_libraries(akari_test_vulkan_offscreen PRIVATE akari_core akari_vulkan)

    add_executable(akari_test_image_regression image_regression.cpp)
    target_compile_features(akari_test_image_regression PRIVATE cxx_std_23)
    target_link_libraries(
        akari_test_image_regression
        PRIVATE akari_core akari_vulkan akari_image_io akari_test_support
    )

    add_executable(akari_test_shader_assets shader_assets.cpp)
    target_compile_features(akari_test_shader_assets PRIVATE cxx_std_23)
    target_link_libraries(akari_test_shader_assets PRIVATE akari_core akari_vulkan)

    add_executable(akari_test_spring_render spring_render_smoke.cpp)
    target_compile_features(akari_test_spring_render PRIVATE cxx_std_23)
    target_link_libraries(akari_test_spring_render PRIVATE akari_core akari_vulkan)

    akari_copy_shader_assets(akari_test_vulkan_preview)
    akari_copy_shader_assets(akari_test_vulkan_offscreen)
    akari_copy_shader_assets(akari_test_image_regression)
    akari_copy_shader_assets(akari_test_shader_assets)
    akari_copy_shader_assets(akari_test_spring_render)

    add_test(NAME vulkan.preview COMMAND akari_test_vulkan_preview)
    set_tests_properties(vulkan.preview PROPERTIES LABELS "gpu;vulkan;preview" TIMEOUT 30)
    add_test(NAME vulkan.offscreen COMMAND akari_test_vulkan_offscreen)
    set_tests_properties(vulkan.offscreen PROPERTIES LABELS "gpu;vulkan;offscreen" TIMEOUT 30)
    add_test(
        NAME image_regression.unit_circle
        COMMAND akari_test_image_regression
            --golden "${CMAKE_CURRENT_SOURCE_DIR}/golden/unit_circle_256.png"
            --artifacts "${CMAKE_BINARY_DIR}/test-artifacts"
    )
    set_tests_properties(
        image_regression.unit_circle
        PROPERTIES LABELS "gpu;vulkan;image-regression" TIMEOUT 30
    )
    add_test(
        NAME vulkan.shader_assets
        COMMAND akari_test_shader_assets
            "$<TARGET_FILE_DIR:akari_test_shader_assets>/assets/shaders"
            "${CMAKE_BINARY_DIR}/test-artifacts/shader-assets"
    )
    set_tests_properties(vulkan.shader_assets PROPERTIES LABELS "gpu;vulkan" TIMEOUT 30)
    add_test(NAME vulkan.spring_recording COMMAND akari_test_spring_render)
    set_tests_properties(vulkan.spring_recording PROPERTIES LABELS "gpu;vulkan;offscreen" TIMEOUT 30)
    add_test(
        NAME capture.cli
        COMMAND akari_capture
            --output "${CMAKE_CURRENT_BINARY_DIR}/capture_cli_test.png"
            --time 1.57079632679
            --width 64
            --height 64
    )
    set_tests_properties(capture.cli PROPERTIES LABELS "gpu;vulkan;offscreen;cli" TIMEOUT 30)
endif()

get_property(AKARI_TESTS DIRECTORY PROPERTY TESTS)
set_tests_properties(${AKARI_TESTS} PROPERTIES WORKING_DIRECTORY "${AKARI_TEST_WORKING_DIRECTORY}")
