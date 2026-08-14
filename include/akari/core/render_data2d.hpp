#pragma once

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <vector>

namespace akari {

struct Camera2D {
    glm::vec2 center{};
    float vertical_span{3.0F};

    friend bool operator==(const Camera2D&, const Camera2D&) = default;
};

struct Vertex2D {
    glm::vec2 position{};
    glm::vec4 color{};

    friend bool operator==(const Vertex2D&, const Vertex2D&) = default;
};

struct SceneFrame2D {
    Camera2D camera;
    std::vector<Vertex2D> vertices;
    std::vector<std::uint32_t> indices;
};

} // namespace akari
