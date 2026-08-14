#include <akari/core/simulation.hpp>

#include <exception>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

void test_tick_mapping()
{
    const akari::SimulationRunner runner{120};
    check(runner.completed_ticks(akari::TimelineTime::from_seconds(1.0)) == 120, "one second has 120 completed ticks");
    check(runner.event_tick(akari::TimelineTime{}) == 0, "zero-time event belongs to tick zero");
    check(runner.event_tick(akari::TimelineTime{1}) == 1, "sub-tick event is assigned to the next tick");
    std::uint64_t replayed{};
    runner.replay(akari::TimelineTime::from_seconds(0.5), [&](const akari::SimulationStepContext& context) {
        check(context.tick_index == replayed, "simulation replay emits ordered tick indices");
        ++replayed;
    });
    check(replayed == 60, "half a second replays 60 ticks");
}

void test_event_ordering()
{
    const akari::SimulationRunner runner{120};
    akari::RecordedEventTrack track;
    const akari::TimelineTime time{1};
    track.append({time, {10}, 7, {1}, akari::PointerEvent2D{akari::PointerEventType::Drag, {}}});
    track.append({time, {10}, 2, {1}, akari::PointerEvent2D{akari::PointerEventType::Drag, {}}});
    const auto scheduled = track.scheduled(runner);
    check(scheduled.size() == 2, "recorded events are scheduled");
    check(scheduled[0].tick == 1 && scheduled[0].event.sequence == 2 && scheduled[1].event.sequence == 7,
          "same-tick events are ordered by sequence");
}

} // namespace

int main()
{
    test_tick_mapping();
    test_event_ordering();
    if (failures != 0) {
        std::cerr << failures << " simulation checks failed\n";
        return 1;
    }
    std::cout << "All simulation checks passed\n";
    return 0;
}
