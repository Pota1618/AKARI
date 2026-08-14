#pragma once

#include <akari/core/identity.hpp>

#include <glm/common.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace akari {

class TimelineTime {
public:
    constexpr TimelineTime() = default;
    explicit constexpr TimelineTime(const std::int64_t nanoseconds) : nanoseconds_(nanoseconds) {}

    [[nodiscard]] static TimelineTime from_seconds(double seconds);
    [[nodiscard]] constexpr std::int64_t nanoseconds() const noexcept { return nanoseconds_; }
    [[nodiscard]] double seconds() const noexcept;

    friend bool operator==(const TimelineTime&, const TimelineTime&) = default;
    friend auto operator<=>(const TimelineTime&, const TimelineTime&) = default;

private:
    std::int64_t nanoseconds_{};
};

enum class PropertyKind {
    Translation2D,
    Rotation2D,
    Scale2D,
    Color,
    Opacity,
};

template <typename T>
struct PropertyHandle {
    NodeId node;
    PropertyKind kind{};

    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(node); }
    friend bool operator==(const PropertyHandle&, const PropertyHandle&) = default;
};

enum class Interpolation {
    Step,
    Linear,
    Smoothstep,
};

template <typename T>
struct Keyframe {
    TimelineTime time;
    T value{};
    Interpolation interpolation{Interpolation::Linear};

    friend bool operator==(const Keyframe&, const Keyframe&) = default;
};

template <typename T>
struct PropertyTake {
    TakeId id;
    std::vector<Keyframe<T>> keyframes;
};

namespace detail {

template <typename T>
[[nodiscard]] constexpr bool property_kind_matches(const PropertyKind kind) noexcept
{
    if constexpr (std::is_same_v<T, float>) {
        return kind == PropertyKind::Rotation2D || kind == PropertyKind::Opacity;
    } else if constexpr (std::is_same_v<T, glm::vec2>) {
        return kind == PropertyKind::Translation2D || kind == PropertyKind::Scale2D;
    } else if constexpr (std::is_same_v<T, glm::vec4>) {
        return kind == PropertyKind::Color;
    } else {
        return false;
    }
}

template <typename T>
[[nodiscard]] bool finite_value(const T& value) noexcept
{
    if constexpr (std::is_same_v<T, float>) {
        return std::isfinite(value);
    } else if constexpr (std::is_same_v<T, glm::vec2>) {
        return std::isfinite(value.x) && std::isfinite(value.y);
    } else if constexpr (std::is_same_v<T, glm::vec4>) {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
               std::isfinite(value.z) && std::isfinite(value.w);
    } else {
        return false;
    }
}

template <typename T>
[[nodiscard]] T interpolate_value(const T& first, const T& second, float alpha)
{
    if constexpr (std::is_same_v<T, float>) {
        return first + (second - first) * alpha;
    } else {
        return glm::mix(first, second, alpha);
    }
}

} // namespace detail

template <typename T>
class PropertyTrack {
public:
    explicit PropertyTrack(PropertyHandle<T> handle) : handle_(handle)
    {
        if (!handle_ || !detail::property_kind_matches<T>(handle_.kind)) {
            throw std::invalid_argument("A property track requires a valid node and value type");
        }
        takes_.push_back({TakeId{1}, {}});
    }

    [[nodiscard]] PropertyHandle<T> handle() const noexcept { return handle_; }
    [[nodiscard]] std::size_t take_count() const noexcept { return takes_.size(); }
    [[nodiscard]] std::size_t active_take_index() const noexcept { return active_take_index_; }
    [[nodiscard]] TakeId active_take_id() const noexcept { return takes_.at(active_take_index_).id; }
    [[nodiscard]] bool recording() const noexcept { return pending_take_.has_value(); }

    void add_keyframe(const Keyframe<T>& keyframe)
    {
        insert_or_replace(takes_.at(active_take_index_).keyframes, keyframe);
    }

    [[nodiscard]] T sample(const TimelineTime time, const T& fallback) const
    {
        const auto& keyframes = pending_take_ ? pending_take_->keyframes : takes_.at(active_take_index_).keyframes;
        if (keyframes.empty()) {
            return fallback;
        }
        if (time <= keyframes.front().time) {
            return keyframes.front().value;
        }
        if (time >= keyframes.back().time) {
            return keyframes.back().value;
        }

        const auto upper = std::ranges::upper_bound(
            keyframes, time, {}, [](const Keyframe<T>& keyframe) { return keyframe.time; });
        const auto& second = *upper;
        const auto& first = *(upper - 1);
        if (first.interpolation == Interpolation::Step) {
            return first.value;
        }
        const auto span = second.time.nanoseconds() - first.time.nanoseconds();
        float alpha = static_cast<float>(time.nanoseconds() - first.time.nanoseconds()) /
                      static_cast<float>(span);
        if (first.interpolation == Interpolation::Smoothstep) {
            alpha = alpha * alpha * (3.0F - 2.0F * alpha);
        }
        return detail::interpolate_value(first.value, second.value, alpha);
    }

    void begin_recording(const TimelineTime time, const T& continuity_value)
    {
        if (pending_take_) {
            throw std::logic_error("The property track is already recording");
        }
        validate_value(continuity_value);
        PropertyTake<T> pending{TakeId{next_take_id_++}, {}};
        const auto& active = takes_.at(active_take_index_).keyframes;
        std::ranges::copy_if(active, std::back_inserter(pending.keyframes), [time](const Keyframe<T>& keyframe) {
            return keyframe.time < time;
        });
        pending.keyframes.push_back({time, continuity_value, Interpolation::Linear});
        pending_take_ = std::move(pending);
    }

    void record(const Keyframe<T>& keyframe)
    {
        if (!pending_take_) {
            throw std::logic_error("The property track is not recording");
        }
        if (keyframe.time < pending_take_->keyframes.back().time) {
            throw std::invalid_argument("Recorded keyframes must be monotonic");
        }
        insert_or_replace(pending_take_->keyframes, keyframe);
    }

    void finalize_recording()
    {
        if (!pending_take_) {
            throw std::logic_error("The property track is not recording");
        }
        takes_.push_back(std::move(*pending_take_));
        pending_take_.reset();
        active_take_index_ = takes_.size() - 1;
    }

    void cancel_recording() noexcept { pending_take_.reset(); }

    void select_previous_take() noexcept
    {
        if (!pending_take_ && active_take_index_ > 0) {
            --active_take_index_;
        }
    }

    void select_next_take() noexcept
    {
        if (!pending_take_ && active_take_index_ + 1 < takes_.size()) {
            ++active_take_index_;
        }
    }

    [[nodiscard]] const std::vector<PropertyTake<T>>& takes() const noexcept { return takes_; }

private:
    void validate_value(const T& value) const
    {
        if (!detail::finite_value(value)) {
            throw std::invalid_argument("A property keyframe value must be finite");
        }
        if constexpr (std::is_same_v<T, float>) {
            if (handle_.kind == PropertyKind::Opacity && (value < 0.0F || value > 1.0F)) {
                throw std::out_of_range("An opacity keyframe value must be in [0, 1]");
            }
        }
    }

    void insert_or_replace(std::vector<Keyframe<T>>& keyframes, const Keyframe<T>& keyframe)
    {
        validate_value(keyframe.value);
        switch (keyframe.interpolation) {
        case Interpolation::Step:
        case Interpolation::Linear:
        case Interpolation::Smoothstep:
            break;
        default:
            throw std::invalid_argument("A property keyframe has an invalid interpolation mode");
        }
        const auto position = std::ranges::lower_bound(
            keyframes, keyframe.time, {}, [](const Keyframe<T>& candidate) { return candidate.time; });
        if (position != keyframes.end() && position->time == keyframe.time) {
            *position = keyframe;
        } else {
            keyframes.insert(position, keyframe);
        }
    }

    PropertyHandle<T> handle_;
    std::vector<PropertyTake<T>> takes_;
    std::size_t active_take_index_{};
    std::uint64_t next_take_id_{2};
    std::optional<PropertyTake<T>> pending_take_;
};

class Timeline {
public:
    PropertyTrack<float>& add_track(PropertyHandle<float> handle);
    PropertyTrack<glm::vec2>& add_track(PropertyHandle<glm::vec2> handle);
    PropertyTrack<glm::vec4>& add_track(PropertyHandle<glm::vec4> handle);

    [[nodiscard]] PropertyTrack<float>* find(PropertyHandle<float> handle) noexcept;
    [[nodiscard]] const PropertyTrack<float>* find(PropertyHandle<float> handle) const noexcept;
    [[nodiscard]] PropertyTrack<glm::vec2>* find(PropertyHandle<glm::vec2> handle) noexcept;
    [[nodiscard]] const PropertyTrack<glm::vec2>* find(PropertyHandle<glm::vec2> handle) const noexcept;
    [[nodiscard]] PropertyTrack<glm::vec4>* find(PropertyHandle<glm::vec4> handle) noexcept;
    [[nodiscard]] const PropertyTrack<glm::vec4>* find(PropertyHandle<glm::vec4> handle) const noexcept;

    [[nodiscard]] float sample(PropertyHandle<float> handle, TimelineTime time, float fallback) const;
    [[nodiscard]] glm::vec2 sample(
        PropertyHandle<glm::vec2> handle, TimelineTime time, const glm::vec2& fallback) const;
    [[nodiscard]] glm::vec4 sample(
        PropertyHandle<glm::vec4> handle, TimelineTime time, const glm::vec4& fallback) const;

private:
    std::vector<PropertyTrack<float>> float_tracks_;
    std::vector<PropertyTrack<glm::vec2>> vec2_tracks_;
    std::vector<PropertyTrack<glm::vec4>> vec4_tracks_;
};

} // namespace akari
