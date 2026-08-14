#include <akari/core/timeline.hpp>

#include <limits>

namespace akari {
namespace {

template <typename T>
PropertyTrack<T>* find_track(std::vector<PropertyTrack<T>>& tracks, const PropertyHandle<T> handle) noexcept
{
    const auto result = std::ranges::find(tracks, handle, &PropertyTrack<T>::handle);
    return result == tracks.end() ? nullptr : &*result;
}

template <typename T>
const PropertyTrack<T>* find_track(
    const std::vector<PropertyTrack<T>>& tracks, const PropertyHandle<T> handle) noexcept
{
    const auto result = std::ranges::find(tracks, handle, &PropertyTrack<T>::handle);
    return result == tracks.end() ? nullptr : &*result;
}

template <typename T>
PropertyTrack<T>& add_track(std::vector<PropertyTrack<T>>& tracks, const PropertyHandle<T> handle)
{
    if (find_track(tracks, handle) != nullptr) {
        throw std::invalid_argument("A property may only have one timeline track");
    }
    return tracks.emplace_back(handle);
}

} // namespace

TimelineTime TimelineTime::from_seconds(const double seconds)
{
    if (!std::isfinite(seconds)) {
        throw std::invalid_argument("Timeline time must be finite");
    }
    constexpr long double nanoseconds_per_second = 1'000'000'000.0L;
    const long double nanoseconds = static_cast<long double>(seconds) * nanoseconds_per_second;
    if (nanoseconds < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
        nanoseconds > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        throw std::overflow_error("Timeline time exceeds the signed 64-bit nanosecond range");
    }
    return TimelineTime{static_cast<std::int64_t>(std::llround(nanoseconds))};
}

double TimelineTime::seconds() const noexcept
{
    return static_cast<double>(nanoseconds_) / 1'000'000'000.0;
}

PropertyTrack<float>& Timeline::add_track(const PropertyHandle<float> handle)
{
    return akari::add_track(float_tracks_, handle);
}

PropertyTrack<glm::vec2>& Timeline::add_track(const PropertyHandle<glm::vec2> handle)
{
    return akari::add_track(vec2_tracks_, handle);
}

PropertyTrack<glm::vec4>& Timeline::add_track(const PropertyHandle<glm::vec4> handle)
{
    return akari::add_track(vec4_tracks_, handle);
}

PropertyTrack<float>* Timeline::find(const PropertyHandle<float> handle) noexcept
{
    return find_track(float_tracks_, handle);
}

const PropertyTrack<float>* Timeline::find(const PropertyHandle<float> handle) const noexcept
{
    return find_track(float_tracks_, handle);
}

PropertyTrack<glm::vec2>* Timeline::find(const PropertyHandle<glm::vec2> handle) noexcept
{
    return find_track(vec2_tracks_, handle);
}

const PropertyTrack<glm::vec2>* Timeline::find(const PropertyHandle<glm::vec2> handle) const noexcept
{
    return find_track(vec2_tracks_, handle);
}

PropertyTrack<glm::vec4>* Timeline::find(const PropertyHandle<glm::vec4> handle) noexcept
{
    return find_track(vec4_tracks_, handle);
}

const PropertyTrack<glm::vec4>* Timeline::find(const PropertyHandle<glm::vec4> handle) const noexcept
{
    return find_track(vec4_tracks_, handle);
}

float Timeline::sample(const PropertyHandle<float> handle, const TimelineTime time, const float fallback) const
{
    const auto* track = find(handle);
    return track == nullptr ? fallback : track->sample(time, fallback);
}

glm::vec2 Timeline::sample(
    const PropertyHandle<glm::vec2> handle, const TimelineTime time, const glm::vec2& fallback) const
{
    const auto* track = find(handle);
    return track == nullptr ? fallback : track->sample(time, fallback);
}

glm::vec4 Timeline::sample(
    const PropertyHandle<glm::vec4> handle, const TimelineTime time, const glm::vec4& fallback) const
{
    const auto* track = find(handle);
    return track == nullptr ? fallback : track->sample(time, fallback);
}

} // namespace akari
