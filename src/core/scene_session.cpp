#include <akari/core/scene_session.hpp>
#include <akari/core/simulation.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace akari {
namespace {

[[nodiscard]] WorldTransform2D local_transform(const Transform2D& transform)
{
    const float cosine = std::cos(transform.rotation_radians);
    const float sine = std::sin(transform.rotation_radians);
    return {
        {cosine * transform.scale.x, sine * transform.scale.x},
        {-sine * transform.scale.y, cosine * transform.scale.y},
        transform.translation};
}

[[nodiscard]] WorldTransform2D compose(
    const WorldTransform2D& parent, const WorldTransform2D& child) noexcept
{
    return {
        parent.x_axis * child.x_axis.x + parent.y_axis * child.x_axis.y,
        parent.x_axis * child.y_axis.x + parent.y_axis * child.y_axis.y,
        transform_point(parent, child.translation)};
}

} // namespace

SceneSession::SceneSession(SceneDefinition definition)
    : definition_(std::move(definition)), timeline_(definition_.timeline())
{
    definition_.validate();
}

void SceneSession::set_mode(const InteractionMode mode)
{
    if (mode == InteractionMode::Record) {
        throw std::invalid_argument("Record mode must be entered with begin_recording");
    }
    if (mode_ == InteractionMode::Record) {
        throw std::logic_error("Finalize or cancel the active recording before changing mode");
    }
    clear_live_overrides();
    mode_ = mode;
}

InputSourceId SceneSession::input_source(const std::string_view stable_key) const
{
    const auto* source = definition_.find_input_source(stable_key);
    if (source == nullptr) {
        throw std::out_of_range("The scene does not define the requested input source");
    }
    return source->id;
}

const InputSourceDefinition& SceneSession::require_source(const InputSourceId id) const
{
    const auto* source = definition_.find_input_source(id);
    if (source == nullptr) {
        throw std::invalid_argument("The input source is not registered with this scene session");
    }
    return *source;
}

PropertyHandle<glm::vec2> SceneSession::bound_drag_property(const NodeId target) const
{
    for (const auto& binding : definition_.interaction_bindings()) {
        if (const auto* drag = std::get_if<DragBinding2D>(&binding);
            drag != nullptr && drag->target == target) {
            return drag->property;
        }
    }
    throw std::invalid_argument("The scene event target does not have a 2D drag binding");
}

glm::vec2 SceneSession::base_translation(const NodeId node) const
{
    const auto* definition = definition_.canvas2d().find(node);
    if (definition == nullptr) {
        throw std::out_of_range("The requested translation node does not exist");
    }
    return definition->transform.translation;
}

glm::vec2 SceneSession::resolved_translation(
    const NodeId node, const TimelineTime time, const bool include_live) const
{
    for (const auto& simulation : definition_.simulations()) {
        if (const auto* spring = std::get_if<SpringMassSystemDefinition>(&simulation);
            spring != nullptr && spring->mass_position.node == node) {
            return evaluate_spring_mass(*spring, time);
        }
    }

    const PropertyHandle<glm::vec2> property{node, PropertyKind::Translation2D};
    glm::vec2 result = timeline_.sample(property, time, base_translation(node));
    if (include_live) {
        const auto override_value = std::ranges::find(live_overrides_, property, &LiveOverride::property);
        if (override_value != live_overrides_.end()) {
            result = override_value->value;
        }
    }
    return result;
}

glm::vec2 SceneSession::evaluate_spring_mass(
    const SpringMassSystemDefinition& simulation, const TimelineTime time) const
{
    glm::vec2 position = simulation.initial_mass_position;
    glm::vec2 velocity = simulation.initial_velocity;
    if (time.nanoseconds() <= 0) {
        return position;
    }

    const SimulationRunner runner{simulation.tick_rate};
    const glm::vec2 anchor_base = base_translation(simulation.anchor_position.node);
    runner.replay(time, [&](const SimulationStepContext& context) {
        const glm::vec2 anchor = timeline_.sample(simulation.anchor_position, context.tick_time, anchor_base);
        const glm::vec2 acceleration =
            (-simulation.stiffness * (position - anchor) - simulation.damping * velocity) / simulation.mass;
        velocity += acceleration * context.delta_seconds;
        position += velocity * context.delta_seconds;
    });
    return position;
}

void SceneSession::begin_recording(
    const PropertyHandle<glm::vec2> property, const TimelineTime time, const InputSourceId source_id)
{
    if (mode_ == InteractionMode::Record) {
        throw std::logic_error("The scene session is already recording");
    }
    const auto& source = require_source(source_id);
    if (source.policy != InputSourcePolicy::RecordableEvents &&
        source.policy != InputSourcePolicy::RecordableSamples) {
        throw std::invalid_argument("The selected input source cannot create a recording");
    }
    if (time < TimelineTime{} || time > definition_.duration()) {
        throw std::out_of_range("Recording time must be within the scene duration");
    }
    const auto report = reproducibility(TimelineTime{}, time);
    if (report.status != SessionReproducibility::Replayable) {
        throw std::logic_error("Recording requires a reproducible baseline at the playhead");
    }
    auto* track = timeline_.find(property);
    if (track == nullptr) {
        throw std::invalid_argument("The armed property does not have a recordable timeline track");
    }
    clear_live_overrides();
    track->begin_recording(time, track->sample(time, base_translation(property.node)));
    recording_property_ = property;
    recording_source_ = source_id;
    recording_last_time_.reset();
    recording_last_sequence_ = 0;
    mode_ = InteractionMode::Record;
}

void SceneSession::finalize_recording()
{
    if (mode_ != InteractionMode::Record || !recording_property_) {
        throw std::logic_error("The scene session is not recording");
    }
    timeline_.find(*recording_property_)->finalize_recording();
    recording_property_.reset();
    recording_source_.reset();
    recording_last_time_.reset();
    recording_last_sequence_ = 0;
    clear_live_overrides();
    mode_ = InteractionMode::Playback;
}

void SceneSession::cancel_recording() noexcept
{
    if (recording_property_) {
        if (auto* track = timeline_.find(*recording_property_)) {
            track->cancel_recording();
        }
    }
    recording_property_.reset();
    recording_source_.reset();
    recording_last_time_.reset();
    recording_last_sequence_ = 0;
    clear_live_overrides();
    mode_ = InteractionMode::Playback;
}

void SceneSession::mark_live_only(const InputSourceId source, const TimelineTime time)
{
    if (!first_unreproducible_time_ || time < *first_unreproducible_time_) {
        first_unreproducible_time_ = time;
    }
    if (std::ranges::find(blockers_, source) == blockers_.end()) {
        blockers_.push_back(source);
    }
}

void SceneSession::apply_event(const SceneEvent& event)
{
    const auto& source = require_source(event.source);
    if (!event.target || event.time < TimelineTime{} || event.time > definition_.duration()) {
        throw std::invalid_argument("Scene event target or time is invalid");
    }
    const auto* pointer = std::get_if<PointerEvent2D>(&event.payload);
    if (pointer == nullptr) {
        throw std::invalid_argument("The scene session does not support this event payload");
    }
    const PropertyHandle<glm::vec2> property = bound_drag_property(event.target);

    if (mode_ == InteractionMode::Edit) {
        auto* node = definition_.canvas2d().find(event.target);
        if (node == nullptr) {
            throw std::out_of_range("The edited node does not exist");
        }
        if (pointer->type != PointerEventType::EndDrag) {
            node->transform.translation = pointer->world_position;
        }
        return;
    }

    if (mode_ == InteractionMode::Record) {
        if (!recording_property_ || !recording_source_ || property != *recording_property_ ||
            event.source != *recording_source_) {
            throw std::invalid_argument("The scene event does not target the armed recording property");
        }
        if (recording_last_time_ && event.time < *recording_last_time_) {
            throw std::invalid_argument("Recorded scene events must be monotonic in time");
        }
        if (recording_last_time_ && event.time == *recording_last_time_ &&
            event.sequence <= recording_last_sequence_) {
            return;
        }
        timeline_.find(property)->record({event.time, pointer->world_position, Interpolation::Linear});
        recording_last_time_ = event.time;
        recording_last_sequence_ = event.sequence;
        const auto override_value = std::ranges::find(live_overrides_, property, &LiveOverride::property);
        if (override_value == live_overrides_.end()) {
            live_overrides_.push_back({property, pointer->world_position});
        } else {
            override_value->value = pointer->world_position;
        }
        return;
    }

    if (source.policy == InputSourcePolicy::Deterministic) {
        throw std::invalid_argument("A deterministic source cannot create a live override");
    }
    const auto override_value = std::ranges::find(live_overrides_, property, &LiveOverride::property);
    if (override_value == live_overrides_.end()) {
        live_overrides_.push_back({property, pointer->world_position});
    } else {
        override_value->value = pointer->world_position;
    }
    mark_live_only(event.source, event.time);
}

void SceneSession::clear_live_overrides() noexcept
{
    live_overrides_.clear();
    first_unreproducible_time_.reset();
    blockers_.clear();
}

void SceneSession::select_previous_take(const PropertyHandle<glm::vec2> property)
{
    auto* track = timeline_.find(property);
    if (track == nullptr) {
        throw std::invalid_argument("The property does not have a timeline track");
    }
    track->select_previous_take();
}

void SceneSession::select_next_take(const PropertyHandle<glm::vec2> property)
{
    auto* track = timeline_.find(property);
    if (track == nullptr) {
        throw std::invalid_argument("The property does not have a timeline track");
    }
    track->select_next_take();
}

std::size_t SceneSession::take_count(const PropertyHandle<glm::vec2> property) const
{
    const auto* track = timeline_.find(property);
    return track == nullptr ? 0 : track->take_count();
}

std::size_t SceneSession::active_take_index(const PropertyHandle<glm::vec2> property) const
{
    const auto* track = timeline_.find(property);
    return track == nullptr ? 0 : track->active_take_index();
}

ReproducibilityReport SceneSession::reproducibility(
    const TimelineTime begin, const TimelineTime end) const
{
    if (begin < TimelineTime{} || end < begin || end > definition_.duration()) {
        throw std::out_of_range("Reproducibility range must be ordered and within the scene duration");
    }
    if (mode_ == InteractionMode::Record) {
        return {SessionReproducibility::Recording, begin, recording_source_ ? std::vector{*recording_source_} : std::vector<InputSourceId>{}};
    }
    if (first_unreproducible_time_ && *first_unreproducible_time_ <= end) {
        return {SessionReproducibility::LiveOnly, *first_unreproducible_time_, blockers_};
    }
    return {SessionReproducibility::Replayable, TimelineTime{}, {}};
}

SceneSnapshot SceneSession::evaluate(const TimelineTime time) const
{
    if (time < TimelineTime{} || time > definition_.duration()) {
        throw std::out_of_range("Scene evaluation time must be within the scene duration");
    }
    const auto& canvas = definition_.canvas2d();
    LayerSnapshot2D layer{canvas.id(), canvas.camera(), {}};
    layer.nodes.reserve(canvas.nodes().size());

    std::unordered_map<std::uint64_t, WorldTransform2D> transforms;
    std::unordered_map<std::uint64_t, int> visiting;
    const std::function<WorldTransform2D(const SceneNode2D&)> resolve_transform =
        [&](const SceneNode2D& node) -> WorldTransform2D {
        if (const auto found = transforms.find(node.id.value); found != transforms.end()) {
            return found->second;
        }
        if (visiting[node.id.value] == 1) {
            throw std::logic_error("A validated scene developed a transform cycle during evaluation");
        }
        visiting[node.id.value] = 1;
        Transform2D evaluated = node.transform;
        evaluated.translation = resolved_translation(node.id, time, true);
        evaluated.rotation_radians = timeline_.sample(
            PropertyHandle<float>{node.id, PropertyKind::Rotation2D}, time, node.transform.rotation_radians);
        evaluated.scale = timeline_.sample(
            PropertyHandle<glm::vec2>{node.id, PropertyKind::Scale2D}, time, node.transform.scale);
        WorldTransform2D world = local_transform(evaluated);
        if (node.parent) {
            world = compose(resolve_transform(*canvas.find(*node.parent)), world);
        }
        visiting[node.id.value] = 2;
        transforms.emplace(node.id.value, world);
        return world;
    };

    for (const auto& node : canvas.nodes()) {
        if (!node.enabled || !node.visible) {
            continue;
        }
        glm::vec4 color = timeline_.sample(
            PropertyHandle<glm::vec4>{node.id, PropertyKind::Color}, time, node.color);
        const float opacity = timeline_.sample(
            PropertyHandle<float>{node.id, PropertyKind::Opacity}, time, node.opacity);
        color.a *= opacity;
        layer.nodes.push_back({
            node.id,
            resolve_transform(node),
            node.primitive,
            color,
            node.draw_order,
            node.pickable,
            node.geometry_hint,
            node.geometry_revision});
    }
    std::ranges::stable_sort(layer.nodes, {}, &SnapshotNode2D::draw_order);

    for (const auto& simulation : definition_.simulations()) {
        if (const auto* spring = std::get_if<SpringMassSystemDefinition>(&simulation)) {
            const auto spring_node = std::ranges::find(layer.nodes, spring->spring_node, &SnapshotNode2D::source);
            const auto anchor_node = std::ranges::find(layer.nodes, spring->anchor_position.node, &SnapshotNode2D::source);
            const auto mass_node = std::ranges::find(layer.nodes, spring->mass_position.node, &SnapshotNode2D::source);
            if (spring_node != layer.nodes.end() && anchor_node != layer.nodes.end() && mass_node != layer.nodes.end()) {
                spring_node->primitive = Line2D{
                    anchor_node->world_transform.translation, mass_node->world_transform.translation, 0.025F};
                spring_node->world_transform = {};
                ++spring_node->geometry_revision;
            }
        }
    }

    return {time, {SpatialLayerSnapshot{std::move(layer)}}};
}

std::string_view to_string(const SessionReproducibility value) noexcept
{
    switch (value) {
    case SessionReproducibility::Replayable:
        return "Replayable";
    case SessionReproducibility::Recording:
        return "Recording";
    case SessionReproducibility::LiveOnly:
        return "Live-only";
    }
    return "Unknown";
}

std::string_view to_string(const InteractionMode value) noexcept
{
    switch (value) {
    case InteractionMode::Playback:
        return "Playback";
    case InteractionMode::Edit:
        return "Edit";
    case InteractionMode::Record:
        return "Record";
    }
    return "Unknown";
}

} // namespace akari
