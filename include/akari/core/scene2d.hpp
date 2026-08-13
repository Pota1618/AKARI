#pragma once

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <vector>

namespace akari {

struct EvaluationContext {
    double time_seconds{};
    std::uint64_t frame_index{};
    double frame_rate{60.0};
    std::uint64_t random_seed{};
};

struct Vertex2D {
    glm::vec2 position{};
    glm::vec4 color{};

    bool operator==(const Vertex2D&) const = default;
};

struct SceneFrame2D {
    std::vector<Vertex2D> vertices;
    std::vector<std::uint32_t> indices;
};

class Scene2D {
public:
    virtual ~Scene2D() = default;

    virtual void evaluate(const EvaluationContext& context, SceneFrame2D& output) const = 0;
};

} // namespace akari
