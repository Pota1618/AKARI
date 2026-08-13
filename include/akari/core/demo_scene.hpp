#pragma once

#include <akari/core/scene2d.hpp>

namespace akari {

class UnitCircleScene final : public Scene2D {
public:
    static constexpr double duration_seconds = 6.28318530717958647692;
    static constexpr std::size_t circle_segments = 128;

    void evaluate(const EvaluationContext& context, SceneFrame2D& output) const override;
};

} // namespace akari
