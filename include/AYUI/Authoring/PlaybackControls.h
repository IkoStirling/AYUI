#pragma once

#include "AYUI/Box.h"
#include <functional>
#include <memory>
#include <optional>

namespace ayt::ui { class Button; class Slider; class TextLabel; class ComboBox; }
namespace ayt::ui::authoring {

struct PlaybackState {
    bool available = false, playing = false;
    double positionSeconds = 0.0, durationSeconds = 0.0;
    std::optional<bool> looping;
    std::optional<float> rate;
};
/** @brief Optional playback adapter, separate from curve editing and resource formats.
 * Views retain a shared source. Hosts own playback tick authority; controls never tick.
 * Return unavailable for a detached document. Loop/rate are optional, not fake defaults.
 */
class IPlaybackSource {
public:
    virtual ~IPlaybackSource() = default;
    virtual PlaybackState playbackState() const = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual bool seek(double seconds) = 0;
    virtual bool setLooping(bool) { return false; }
    virtual bool setRate(float) { return false; }
};
struct PlaybackControlsOptions {
    bool buttons = true, seek = true, time = true, status = false, loop = false, rate = false;
    int timePrecision = 3;
};
class PlaybackControls final : public HBox {
public:
    explicit PlaybackControls(std::shared_ptr<IPlaybackSource> source,
                              PlaybackControlsOptions options = {});
    void refresh();
    void setOnChanged(std::function<void()> callback) { _onChanged = std::move(callback); }
    Button* playButton() const noexcept { return _play; }
    Button* pauseButton() const noexcept { return _pause; }
    Button* stopButton() const noexcept { return _stop; }
    Button* loopButton() const noexcept { return _loop; }
    ComboBox* rateInput() const noexcept { return _rate; }
    Slider* seekInput() const noexcept { return _seek; }
    TextLabel* timeLabel() const noexcept { return _time; }
private:
    void changed();
    std::shared_ptr<IPlaybackSource> _source;
    PlaybackControlsOptions _options;
    std::function<void()> _onChanged;
    Button *_play = nullptr, *_pause = nullptr, *_stop = nullptr, *_loop = nullptr;
    ComboBox* _rate = nullptr;
    Slider* _seek = nullptr;
    TextLabel *_time = nullptr, *_status = nullptr;
    bool _refreshing = false;
};
} // namespace ayt::ui::authoring
