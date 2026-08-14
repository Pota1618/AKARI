#pragma once

#include <akari/core/scene.hpp>

namespace akari {

inline constexpr double unit_circle_duration_seconds = 6.28318530717958647692;
inline constexpr std::size_t unit_circle_segments = 128;
inline constexpr double spring_mass_duration_seconds = 10.0;

[[nodiscard]] SceneDefinition make_unit_circle_scene();
[[nodiscard]] SceneDefinition make_spring_mass_scene();

[[nodiscard]] PropertyHandle<glm::vec2> unit_circle_point_position(const SceneDefinition& scene);
[[nodiscard]] PropertyHandle<glm::vec2> spring_anchor_position(const SceneDefinition& scene);
[[nodiscard]] InputSourceId spring_pointer_source(const SceneDefinition& scene);

} // namespace akari
