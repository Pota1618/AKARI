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

FetchContent_MakeAvailable(glfw glm)

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

add_library(akari_vulkan STATIC src/vulkan/vulkan_renderer.cpp)
target_compile_features(akari_vulkan PUBLIC cxx_std_23)
target_include_directories(akari_vulkan PUBLIC include)
target_compile_definitions(akari_vulkan PRIVATE AKARI_SHADER_DIR="${AKARI_SHADER_OUTPUT_DIR}")
target_link_libraries(akari_vulkan PUBLIC akari_core PRIVATE glfw Vulkan::Vulkan)
add_dependencies(akari_vulkan akari_shaders)

add_executable(akari_preview src/preview/main.cpp)
target_compile_features(akari_preview PRIVATE cxx_std_23)
target_link_libraries(akari_preview PRIVATE akari_core akari_vulkan glfw)

add_executable(akari_vulkan_smoke src/vulkan_smoke.cpp)
target_compile_features(akari_vulkan_smoke PRIVATE cxx_std_23)
target_link_libraries(akari_vulkan_smoke PRIVATE akari_core akari_vulkan glfw)

enable_testing()
add_test(NAME dependencies.smoke COMMAND akari_dependency_smoke)
add_test(NAME core.unit COMMAND akari_core_tests)
add_test(NAME vulkan.smoke COMMAND akari_vulkan_smoke)
set_tests_properties(vulkan.smoke PROPERTIES LABELS "gpu;vulkan" TIMEOUT 30)
