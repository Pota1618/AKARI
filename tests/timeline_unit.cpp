#include <akari/core/timeline.hpp>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
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

template <typename Function>
void check_throws(Function&& function, const std::string_view message)
{
    try {
        function();
        check(false, message);
    } catch (const std::exception&) {
    }
}

void test_time()
{
    const auto time = akari::TimelineTime::from_seconds(1.25);
    check(time.nanoseconds() == 1'250'000'000, "integer nanosecond time");
    check(std::abs(time.seconds() - 1.25) < 1.0e-12, "seconds round trip");
    check_throws(
        [] { (void)akari::TimelineTime::from_seconds(std::numeric_limits<double>::infinity()); },
        "non-finite time rejected");
}

void test_interpolation_and_takes()
{
    const akari::PropertyHandle<glm::vec2> handle{{42}, akari::PropertyKind::Translation2D};
    akari::PropertyTrack<glm::vec2> track{handle};
    track.add_keyframe({akari::TimelineTime{}, {0.0F, 0.0F}, akari::Interpolation::Linear});
    track.add_keyframe({akari::TimelineTime::from_seconds(2.0), {2.0F, 4.0F}, akari::Interpolation::Linear});
    check(track.sample(akari::TimelineTime::from_seconds(1.0), {}).x == 1.0F, "linear interpolation");

    track.begin_recording(akari::TimelineTime::from_seconds(1.0), {1.0F, 2.0F});
    track.record({akari::TimelineTime::from_seconds(1.5), {4.0F, 5.0F}, akari::Interpolation::Linear});
    check(track.recording(), "pending take visible while recording");
    check(track.sample(akari::TimelineTime::from_seconds(2.0), {}).x == 4.0F, "fork replaces future");
    track.cancel_recording();
    check(track.take_count() == 1 && track.sample(akari::TimelineTime::from_seconds(2.0), {}).x == 2.0F,
          "cancel restores active take");

    track.begin_recording(akari::TimelineTime::from_seconds(1.0), {1.0F, 2.0F});
    track.record({akari::TimelineTime::from_seconds(1.5), {4.0F, 5.0F}, akari::Interpolation::Linear});
    track.finalize_recording();
    check(track.take_count() == 2 && track.active_take_index() == 1, "finalized non-destructive take");
    track.select_previous_take();
    check(track.sample(akari::TimelineTime::from_seconds(2.0), {}).x == 2.0F, "previous take selection");
    track.select_next_take();
    check(track.sample(akari::TimelineTime::from_seconds(2.0), {}).x == 4.0F, "next take selection");
}

void test_interpolation_modes_and_types()
{
    const akari::PropertyHandle<float> opacity{{7}, akari::PropertyKind::Opacity};
    akari::PropertyTrack<float> step{opacity};
    step.add_keyframe({akari::TimelineTime{}, 0.0F, akari::Interpolation::Step});
    step.add_keyframe({akari::TimelineTime::from_seconds(1.0), 1.0F, akari::Interpolation::Linear});
    check(step.sample(akari::TimelineTime::from_seconds(0.5), 0.0F) == 0.0F, "step interpolation");

    akari::PropertyTrack<float> smooth{{{8}, akari::PropertyKind::Rotation2D}};
    smooth.add_keyframe({akari::TimelineTime{}, 0.0F, akari::Interpolation::Smoothstep});
    smooth.add_keyframe({akari::TimelineTime::from_seconds(1.0), 8.0F, akari::Interpolation::Linear});
    check(std::abs(smooth.sample(akari::TimelineTime::from_seconds(0.25), 0.0F) - 1.25F) < 1.0e-6F,
          "smoothstep interpolation");

    check_throws(
        [] {
            akari::PropertyTrack<glm::vec4> invalid{{{9}, akari::PropertyKind::Translation2D}};
            (void)invalid;
        },
        "property kind and value type mismatch rejected");
    check_throws(
        [] {
            akari::PropertyTrack<float> non_finite{{{10}, akari::PropertyKind::Opacity}};
            non_finite.add_keyframe({
                akari::TimelineTime{},
                std::numeric_limits<float>::quiet_NaN(),
                akari::Interpolation::Linear});
        },
        "non-finite keyframe rejected");
}

} // namespace

int main()
{
    test_time();
    test_interpolation_and_takes();
    test_interpolation_modes_and_types();
    if (failures != 0) {
        std::cerr << failures << " timeline checks failed\n";
        return 1;
    }
    std::cout << "All timeline checks passed\n";
    return 0;
}
