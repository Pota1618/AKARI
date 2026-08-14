#pragma once

#include <akari/core/scene.hpp>

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace akari {

enum class SessionReproducibility {
    Replayable,
    Recording,
    LiveOnly,
};

struct ReproducibilityReport {
    SessionReproducibility status{SessionReproducibility::Replayable};
    TimelineTime first_unreproducible_time;
    std::vector<InputSourceId> blockers;
};

enum class InteractionMode {
    Playback,
    Edit,
    Record,
};

class SceneSession {
public:
    explicit SceneSession(SceneDefinition definition);

    [[nodiscard]] const SceneDefinition& definition() const noexcept { return definition_; }
    [[nodiscard]] TimelineTime duration() const noexcept { return definition_.duration(); }
    [[nodiscard]] InteractionMode mode() const noexcept { return mode_; }
    void set_mode(InteractionMode mode);

    [[nodiscard]] InputSourceId input_source(std::string_view stable_key) const;
    void begin_recording(PropertyHandle<glm::vec2> property, TimelineTime time, InputSourceId source);
    void finalize_recording();
    void cancel_recording() noexcept;
    void apply_event(const SceneEvent& event);
    void clear_live_overrides() noexcept;

    void select_previous_take(PropertyHandle<glm::vec2> property);
    void select_next_take(PropertyHandle<glm::vec2> property);
    [[nodiscard]] std::size_t take_count(PropertyHandle<glm::vec2> property) const;
    [[nodiscard]] std::size_t active_take_index(PropertyHandle<glm::vec2> property) const;

    [[nodiscard]] ReproducibilityReport reproducibility(TimelineTime begin, TimelineTime end) const;
    [[nodiscard]] SceneSnapshot evaluate(TimelineTime time) const;

private:
    struct LiveOverride {
        PropertyHandle<glm::vec2> property;
        glm::vec2 value{};
    };

    [[nodiscard]] const InputSourceDefinition& require_source(InputSourceId id) const;
    [[nodiscard]] PropertyHandle<glm::vec2> bound_drag_property(NodeId target) const;
    [[nodiscard]] glm::vec2 base_translation(NodeId node) const;
    [[nodiscard]] glm::vec2 resolved_translation(NodeId node, TimelineTime time, bool include_live) const;
    [[nodiscard]] glm::vec2 evaluate_spring_mass(
        const SpringMassSystemDefinition& simulation, TimelineTime time) const;
    void mark_live_only(InputSourceId source, TimelineTime time);

    SceneDefinition definition_;
    Timeline timeline_;
    InteractionMode mode_{InteractionMode::Playback};
    std::optional<PropertyHandle<glm::vec2>> recording_property_;
    std::optional<InputSourceId> recording_source_;
    std::optional<TimelineTime> recording_last_time_;
    std::uint64_t recording_last_sequence_{};
    std::vector<LiveOverride> live_overrides_;
    std::optional<TimelineTime> first_unreproducible_time_;
    std::vector<InputSourceId> blockers_;
};

[[nodiscard]] std::string_view to_string(SessionReproducibility value) noexcept;
[[nodiscard]] std::string_view to_string(InteractionMode value) noexcept;

} // namespace akari
