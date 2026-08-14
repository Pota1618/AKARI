#pragma once

#include <akari/core/scene.hpp>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace akari {

struct SimulationStepContext {
    std::uint64_t tick_index{};
    TimelineTime tick_time;
    float delta_seconds{};
};

class SimulationRunner {
public:
    explicit SimulationRunner(std::uint32_t tick_rate);

    [[nodiscard]] std::uint32_t tick_rate() const noexcept { return tick_rate_; }
    [[nodiscard]] std::uint64_t completed_ticks(TimelineTime time) const;
    [[nodiscard]] std::uint64_t event_tick(TimelineTime time) const;
    [[nodiscard]] TimelineTime tick_time(std::uint64_t tick) const;

    template <typename Step>
    void replay(const TimelineTime time, Step&& step) const
    {
        const auto count = completed_ticks(time);
        const float delta = 1.0F / static_cast<float>(tick_rate_);
        for (std::uint64_t tick = 0; tick < count; ++tick) {
            step(SimulationStepContext{tick, tick_time(tick), delta});
        }
    }

private:
    std::uint32_t tick_rate_{};
};

struct ScheduledSceneEvent {
    std::uint64_t tick{};
    SceneEvent event;
};

class RecordedEventTrack {
public:
    void append(SceneEvent event);
    [[nodiscard]] std::vector<ScheduledSceneEvent> scheduled(const SimulationRunner& runner) const;
    [[nodiscard]] const std::vector<SceneEvent>& events() const noexcept { return events_; }

private:
    std::vector<SceneEvent> events_;
};

} // namespace akari
