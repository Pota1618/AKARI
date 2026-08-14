#pragma once

#include <akari/core/render_data2d.hpp>
#include <akari/core/scene.hpp>

namespace akari {

void tessellate_2d(const SceneSnapshot& snapshot, SceneFrame2D& output);

} // namespace akari
