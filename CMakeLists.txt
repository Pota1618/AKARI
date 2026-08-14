cmake_minimum_required(VERSION 3.24)

project(AKARI VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)
include(CTest)

option(AKARI_ENABLE_VULKAN "Build the Vulkan renderer, applications, and GPU tests" ON)

if(AKARI_ENABLE_VULKAN)
    find_package(Vulkan 1.3 REQUIRED COMPONENTS glslc)
endif()

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

FetchContent_MakeAvailable(glm stb)
if(AKARI_ENABLE_VULKAN)
    FetchContent_MakeAvailable(glfw vma)
endif()

add_library(akari_core STATIC
    src/core/error.cpp
    src/core/identity.cpp
    src/core/timeline.cpp
    src/core/scene.cpp
    src/core/scene_session.cpp
    src/core/simulation.cpp
    src/core/tessellator2d.cpp
    src/core/demo_scene.cpp
    src/core/playback_controller.cpp
)
target_compile_features(akari_core PUBLIC cxx_std_23)
target_include_directories(akari_core PUBLIC include)
target_link_libraries(akari_core PUBLIC glm::glm)

add_library(akari_image_io STATIC src/image/png_writer.cpp)
target_compile_features(akari_image_io PUBLIC cxx_std_23)
target_include_directories(akari_image_io PUBLIC include PRIVATE "${stb_SOURCE_DIR}")
target_link_libraries(akari_image_io PUBLIC akari_core)

if(AKARI_ENABLE_VULKAN)
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

    add_library(akari_vulkan STATIC
        src/platform/executable_directory.cpp
        src/vulkan/vulkan_backend.cpp
        src/vulkan/vulkan_offscreen_renderer.cpp
    )
    target_compile_features(akari_vulkan PUBLIC cxx_std_23)
    target_include_directories(akari_vulkan PUBLIC include PRIVATE src "${vma_SOURCE_DIR}/include")
    target_link_libraries(akari_vulkan PUBLIC akari_core PRIVATE Vulkan::Vulkan)
    add_dependencies(akari_vulkan akari_shaders)

    add_library(akari_vulkan_preview STATIC src/vulkan/vulkan_renderer.cpp)
    target_compile_features(akari_vulkan_preview PUBLIC cxx_std_23)
    target_include_directories(akari_vulkan_preview PUBLIC include PRIVATE src "${vma_SOURCE_DIR}/include")
    target_link_libraries(akari_vulkan_preview PUBLIC akari_vulkan PRIVATE glfw Vulkan::Vulkan)

    add_executable(akari_preview src/preview/main.cpp)
    target_compile_features(akari_preview PRIVATE cxx_std_23)
    target_link_libraries(akari_preview PRIVATE akari_core akari_vulkan_preview glfw)

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
    akari_copy_shader_assets(akari_capture)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
