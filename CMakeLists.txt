cmake_minimum_required(VERSION 3.24)

project(AKARI VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)
find_package(Vulkan 1.3 REQUIRED COMPONENTS glslc)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)

set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_ENABLE_CXX_20 ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.3
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG 1d8f600fd424278486eade7ed3e877c99f0846b1
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(glfw glm vma stb)

add_executable(akari_dependency_smoke src/dependency_smoke.cpp)
target_compile_features(akari_dependency_smoke PRIVATE cxx_std_23)
target_link_libraries(akari_dependency_smoke PRIVATE glfw glm::glm)

add_library(akari_core STATIC
    src/core/demo_scene.cpp
    src/core/playback_controller.cpp
)
target_compile_features(akari_core PUBLIC cxx_std_23)
target_include_directories(akari_core PUBLIC include)
target_link_libraries(akari_core PUBLIC glm::glm)

add_executable(akari_core_tests tests/core_tests.cpp)
target_compile_features(akari_core_tests PRIVATE cxx_std_23)
target_link_libraries(akari_core_tests PRIVATE akari_core)

set(AKARI_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
set(AKARI_SHADER_SOURCES
    shaders/flat_color.vert
    shaders/flat_color.frag
)
set(AKARI_SHADER_BINARIES)
foreach(AKARI_SHADER IN LISTS AKARI_SHADER_SOURCES)
    get_filename_component(AKARI_SHADER_NAME "${AKARI_SHADER}" NAME)
    set(AKARI_SHADER_BINARY "${AKARI_SHADER_OUTPUT_DIR}/${AKARI_SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT "${AKARI_SHADER_BINARY}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${AKARI_SHADER_OUTPUT_DIR}"
        COMMAND "$<TARGET_FILE:Vulkan::glslc>" "${CMAKE_CURRENT_SOURCE_DIR}/${AKARI_SHADER}" -o "${AKARI_SHADER_BINARY}"
        DEPENDS "${AKARI_SHADER}"
        VERBATIM
    )
    list(APPEND AKARI_SHADER_BINARIES "${AKARI_SHADER_BINARY}")
endforeach()
add_custom_target(akari_shaders DEPENDS ${AKARI_SHADER_BINARIES})

add_library(akari_image_io STATIC src/image/png_writer.cpp)
target_compile_features(akari_image_io PUBLIC cxx_std_23)
target_include_directories(akari_image_io PUBLIC include PRIVATE "${stb_SOURCE_DIR}")

add_executable(akari_image_io_tests tests/image_io_tests.cpp)
target_compile_features(akari_image_io_tests PRIVATE cxx_std_23)
target_link_libraries(akari_image_io_tests PRIVATE akari_image_io)

add_library(akari_vulkan STATIC
    src/vulkan/vulkan_backend.cpp
    src/vulkan/vulkan_offscreen_renderer.cpp
)
target_compile_features(akari_vulkan PUBLIC cxx_std_23)
target_include_directories(akari_vulkan PUBLIC include PRIVATE src/vulkan "${vma_SOURCE_DIR}/include")
target_compile_definitions(akari_vulkan PRIVATE AKARI_SHADER_DIR="${AKARI_SHADER_OUTPUT_DIR}")
target_link_libraries(akari_vulkan PUBLIC akari_core PRIVATE Vulkan::Vulkan)
add_dependencies(akari_vulkan akari_shaders)

add_library(akari_vulkan_preview STATIC src/vulkan/vulkan_renderer.cpp)
target_compile_features(akari_vulkan_preview PUBLIC cxx_std_23)
target_include_directories(akari_vulkan_preview PUBLIC include PRIVATE src/vulkan "${vma_SOURCE_DIR}/include")
target_link_libraries(akari_vulkan_preview PUBLIC akari_vulkan PRIVATE glfw Vulkan::Vulkan)

add_executable(akari_preview src/preview/main.cpp)
target_compile_features(akari_preview PRIVATE cxx_std_23)
target_link_libraries(akari_preview PRIVATE akari_core akari_vulkan_preview glfw)

add_executable(akari_vulkan_smoke src/vulkan_smoke.cpp)
target_compile_features(akari_vulkan_smoke PRIVATE cxx_std_23)
target_link_libraries(akari_vulkan_smoke PRIVATE akari_core akari_vulkan_preview glfw)

add_executable(akari_offscreen_smoke src/offscreen_smoke.cpp)
target_compile_features(akari_offscreen_smoke PRIVATE cxx_std_23)
target_link_libraries(akari_offscreen_smoke PRIVATE akari_core akari_vulkan)

add_executable(akari_capture src/capture/main.cpp)
target_compile_features(akari_capture PRIVATE cxx_std_23)
target_link_libraries(akari_capture PRIVATE akari_core akari_vulkan akari_image_io)

function(akari_copy_shader_assets target)
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/assets/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${AKARI_SHADER_BINARIES}
            "$<TARGET_FILE_DIR:${target}>/assets/shaders"
        VERBATIM
    )
endfunction()

akari_copy_shader_assets(akari_preview)
akari_copy_shader_assets(akari_vulkan_smoke)
akari_copy_shader_assets(akari_offscreen_smoke)
akari_copy_shader_assets(akari_capture)

enable_testing()
add_test(NAME dependencies.smoke COMMAND akari_dependency_smoke)
add_test(NAME core.unit COMMAND akari_core_tests)
add_test(NAME image_io.unit COMMAND akari_image_io_tests)
add_test(NAME vulkan.smoke COMMAND akari_vulkan_smoke)
set_tests_properties(vulkan.smoke PROPERTIES LABELS "gpu;vulkan" TIMEOUT 30)
add_test(NAME vulkan.offscreen COMMAND akari_offscreen_smoke)
set_tests_properties(vulkan.offscreen PROPERTIES LABELS "gpu;vulkan;offscreen" TIMEOUT 30)
add_test(
    NAME capture.cli
    COMMAND akari_capture --output "${CMAKE_CURRENT_BINARY_DIR}/capture_cli_test.png" --time 1.57079632679 --width 64 --height 64
)
set_tests_properties(capture.cli PROPERTIES LABELS "gpu;vulkan;offscreen;cli" TIMEOUT 30)
