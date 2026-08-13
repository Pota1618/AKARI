#pragma once

namespace akari {

class PlaybackController {
public:
    explicit PlaybackController(double duration_seconds, double nominal_frame_rate = 60.0);

    void advance(double delta_seconds);
    void seek(double delta_seconds);
    void reset();
    void toggle_playing();

    [[nodiscard]] double time_seconds() const noexcept;
    [[nodiscard]] double nominal_frame_rate() const noexcept;
    [[nodiscard]] bool playing() const noexcept;

private:
    double duration_seconds_{};
    double nominal_frame_rate_{};
    double time_seconds_{};
    bool playing_{true};
};

} // namespace akari
