cmake_minimum_required(VERSION 3.24)

project(AKARI VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)

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

enable_testing()
add_test(NAME dependencies.smoke COMMAND akari_dependency_smoke)

