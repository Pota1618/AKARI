#include <akari/core/playback_controller.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace akari {

PlaybackController::PlaybackController(const double duration_seconds, const double nominal_frame_rate)
    : duration_seconds_(duration_seconds), nominal_frame_rate_(nominal_frame_rate)
{
    if (!std::isfinite(duration_seconds_) || duration_seconds_ <= 0.0) {
        throw std::invalid_argument("Playback duration must be finite and positive");
    }
    if (!std::isfinite(nominal_frame_rate_) || nominal_frame_rate_ <= 0.0) {
        throw std::invalid_argument("Nominal frame rate must be finite and positive");
    }
}

void PlaybackController::advance(const double delta_seconds)
{
    if (!playing_ || !std::isfinite(delta_seconds) || delta_seconds <= 0.0) {
        return;
    }
    time_seconds_ = std::fmod(time_seconds_ + delta_seconds, duration_seconds_);
}

void PlaybackController::seek(const double delta_seconds)
{
    if (!std::isfinite(delta_seconds)) {
        return;
    }
    time_seconds_ = std::clamp(time_seconds_ + delta_seconds, 0.0, duration_seconds_);
}

void PlaybackController::reset()
{
    time_seconds_ = 0.0;
}

void PlaybackController::toggle_playing()
{
    playing_ = !playing_;
}

double PlaybackController::time_seconds() const noexcept
{
    return time_seconds_;
}

double PlaybackController::nominal_frame_rate() const noexcept
{
    return nominal_frame_rate_;
}

bool PlaybackController::playing() const noexcept
{
    return playing_;
}

} // namespace akari
