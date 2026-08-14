#include <akari/core/simulation.hpp>

#include <limits>

namespace akari {
namespace {

constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000ULL;

[[nodiscard]] std::uint64_t checked_whole_ticks(
    const std::uint64_t whole_seconds, const std::uint32_t tick_rate)
{
    if (whole_seconds > std::numeric_limits<std::uint64_t>::max() / tick_rate) {
        throw std::overflow_error("Simulation tick index overflow");
    }
    return whole_seconds * tick_rate;
}

} // namespace

SimulationRunner::SimulationRunner(const std::uint32_t tick_rate) : tick_rate_(tick_rate)
{
    if (tick_rate_ == 0 || tick_rate_ > 1'000'000) {
        throw std::invalid_argument("Simulation tick rate must be in [1, 1000000]");
    }
}

std::uint64_t SimulationRunner::completed_ticks(const TimelineTime time) const
{
    if (time.nanoseconds() < 0) {
        throw std::out_of_range("Simulation time must not be negative");
    }
    const auto nanoseconds = static_cast<std::uint64_t>(time.nanoseconds());
    const auto whole = checked_whole_ticks(nanoseconds / nanoseconds_per_second, tick_rate_);
    const auto remainder = nanoseconds % nanoseconds_per_second;
    return whole + remainder * tick_rate_ / nanoseconds_per_second;
}

std::uint64_t SimulationRunner::event_tick(const TimelineTime time) const
{
    if (time.nanoseconds() < 0) {
        throw std::out_of_range("Simulation event time must not be negative");
    }
    const auto nanoseconds = static_cast<std::uint64_t>(time.nanoseconds());
    const auto whole = checked_whole_ticks(nanoseconds / nanoseconds_per_second, tick_rate_);
    const auto remainder = nanoseconds % nanoseconds_per_second;
    const auto partial = remainder * tick_rate_;
    return whole + partial / nanoseconds_per_second + (partial % nanoseconds_per_second == 0 ? 0 : 1);
}

TimelineTime SimulationRunner::tick_time(const std::uint64_t tick) const
{
    const long double nanoseconds = static_cast<long double>(tick) * nanoseconds_per_second /
                                    static_cast<long double>(tick_rate_);
    if (nanoseconds > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        throw std::overflow_error("Simulation tick time overflow");
    }
    return TimelineTime{static_cast<std::int64_t>(nanoseconds)};
}

void RecordedEventTrack::append(SceneEvent event)
{
    if (!event.target || !event.source || event.time.nanoseconds() < 0) {
        throw std::invalid_argument("A recorded scene event requires valid identity and nonnegative time");
    }
    events_.push_back(std::move(event));
}

std::vector<ScheduledSceneEvent> RecordedEventTrack::scheduled(const SimulationRunner& runner) const
{
    std::vector<ScheduledSceneEvent> result;
    result.reserve(events_.size());
    for (const auto& event : events_) {
        result.push_back({runner.event_tick(event.time), event});
    }
    std::ranges::stable_sort(result, [](const ScheduledSceneEvent& lhs, const ScheduledSceneEvent& rhs) {
        return lhs.tick < rhs.tick || (lhs.tick == rhs.tick && lhs.event.sequence < rhs.event.sequence);
    });
    return result;
}

} // namespace akari
