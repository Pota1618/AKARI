#include <akari/core/demo_scene.hpp>
#include <akari/core/playback_controller.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
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

bool near(const float lhs, const float rhs, const float tolerance = 1.0e-5F)
{
    return std::abs(lhs - rhs) <= tolerance;
}

glm::vec2 moving_point_center(const akari::SceneFrame2D& frame)
{
    constexpr std::size_t axes_vertices = 8;
    constexpr std::size_t circle_vertices = akari::UnitCircleScene::circle_segments * 4;
    return frame.vertices.at(axes_vertices + circle_vertices).position;
}

akari::SceneFrame2D evaluate_at(const double time, const double frame_rate)
{
    akari::UnitCircleScene scene;
    akari::SceneFrame2D frame;
    scene.evaluate(
        {.time_seconds = time,
         .frame_index = static_cast<std::uint64_t>(std::floor(time * frame_rate)),
         .frame_rate = frame_rate,
         .random_seed = 42},
        frame);
    return frame;
}

void test_cardinal_points()
{
    const std::array cases{
        std::pair{0.0, glm::vec2{1.0F, 0.0F}},
        std::pair{std::numbers::pi / 2.0, glm::vec2{0.0F, 1.0F}},
        std::pair{std::numbers::pi, glm::vec2{-1.0F, 0.0F}},
        std::pair{3.0 * std::numbers::pi / 2.0, glm::vec2{0.0F, -1.0F}},
    };

    for (const auto& [time, expected] : cases) {
        const auto actual = moving_point_center(evaluate_at(time, 60.0));
        check(near(actual.x, expected.x) && near(actual.y, expected.y), "cardinal point position");
    }
}

void test_determinism()
{
    constexpr std::array times{0.0, 0.17, 1.5, 4.9, 2.3};
    std::array<akari::SceneFrame2D, times.size()> expected;
    for (std::size_t index = 0; index < times.size(); ++index) {
        expected[index] = evaluate_at(times[index], 60.0);
    }

    constexpr std::array order{4U, 1U, 3U, 0U, 2U};
    for (const auto index : order) {
        const auto actual = evaluate_at(times[index], 60.0);
        check(actual.vertices == expected[index].vertices, "seek order vertex determinism");
        check(actual.indices == expected[index].indices, "seek order index determinism");
    }

    for (const double frame_rate : {30.0, 60.0, 120.0}) {
        const auto actual = evaluate_at(1.2345, frame_rate);
        const auto reference = evaluate_at(1.2345, 60.0);
        check(actual.vertices == reference.vertices, "frame-rate-independent vertices");
        check(actual.indices == reference.indices, "frame-rate-independent indices");
    }
}

void test_mesh_validity()
{
    const auto frame = evaluate_at(0.75, 60.0);
    constexpr std::size_t expected_vertices = 8 + akari::UnitCircleScene::circle_segments * 4 + 33;
    constexpr std::size_t expected_indices = 12 + akari::UnitCircleScene::circle_segments * 6 + 32 * 3;
    check(frame.vertices.size() == expected_vertices, "expected vertex count");
    check(frame.indices.size() == expected_indices, "expected index count");
    check(frame.indices.size() % 3 == 0, "triangle index count");

    for (const auto& vertex : frame.vertices) {
        check(std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y), "finite position");
        check(
            std::isfinite(vertex.color.r) && std::isfinite(vertex.color.g) &&
                std::isfinite(vertex.color.b) && std::isfinite(vertex.color.a),
            "finite color");
    }
    for (const auto index : frame.indices) {
        check(index < frame.vertices.size(), "index in range");
    }
}

void test_playback()
{
    akari::PlaybackController playback{2.0, 60.0};
    playback.advance(1.25);
    check(std::abs(playback.time_seconds() - 1.25) < 1.0e-9, "playback advance");
    playback.advance(1.0);
    check(std::abs(playback.time_seconds() - 0.25) < 1.0e-9, "playback loop");
    playback.seek(-1.0);
    check(playback.time_seconds() == 0.0, "seek lower clamp");
    playback.seek(5.0);
    check(playback.time_seconds() == 2.0, "seek upper clamp");
    playback.reset();
    check(playback.time_seconds() == 0.0, "playback reset");
    playback.toggle_playing();
    playback.advance(0.5);
    check(playback.time_seconds() == 0.0, "paused playback");
}

} // namespace

int main()
{
    test_cardinal_points();
    test_determinism();
    test_mesh_validity();
    test_playback();

    if (failures != 0) {
        std::cerr << failures << " core test checks failed\n";
        return 1;
    }
    std::cout << "All core checks passed\n";
    return 0;
}
