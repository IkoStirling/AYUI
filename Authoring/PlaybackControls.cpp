#include "AYUI/Authoring/PlaybackControls.h"
#include "AYUI/Authoring/AuthoringPrimitives.h"
#include <AYUI/Button.h>
#include <AYUI/ComboBox.h>
#include <AYUI/Slider.h>
#include <AYUI/TextLabel.h>
#include <limits>

namespace ayt::ui::authoring {
namespace {
constexpr float kRates[] = {0.25f, 0.5f, 1.0f, 1.5f, 2.0f};
void setText(TextLabel* label, const std::wstring& text) {
    if (label && label->getText() != text) label->setText(text);
}
}
PlaybackControls::PlaybackControls(std::shared_ptr<IPlaybackSource> source, PlaybackControlsOptions options)
    : _source(std::move(source)), _options(options) {
    setSpacing(5.0f);
    auto addAction = [this](const wchar_t* text, std::function<void()> action, float width) {
        auto* button = new Button();
        button->setText(text);
        button->setPadding(7, 3, 7, 3);
        button->setOnClicked([this, action = std::move(action)] {
            if (!_source || !_source->playbackState().available) return;
            action(); changed();
        });
        addWidget(button, width);
        return button;
    };
    if (options.buttons) {
        _play = addAction(L"Play", [this] { _source->play(); }, 50);
        _pause = addAction(L"Pause", [this] { _source->pause(); }, 54);
        _stop = addAction(L"Stop", [this] { _source->stop(); }, 50);
    }
    if (options.loop) {
        _loop = addAction(L"Loop: On", [this] {
            const auto state = _source->playbackState();
            if (state.looping) (void)_source->setLooping(!*state.looping);
        }, 76);
    }
    if (options.rate) {
        _rate = new ComboBox();
        _rate->setItems({L"0.25x", L"0.5x", L"1.0x", L"1.5x", L"2.0x"});
        _rate->setOnSelectionChanged([this](int index) {
            if (_refreshing || !_source || index < 0 || index >= 5
                || !_source->playbackState().rate) return;
            (void)_source->setRate(kRates[index]); changed();
        });
        addWidget(_rate, 78);
    }
    if (options.seek) {
        _seek = new Slider();
        _seek->setOnValueChanged([this](float seconds) {
            if (_refreshing || !_source || !std::isfinite(seconds)) return;
            const auto state = _source->playbackState();
            if (!state.available) return;
            (void)_source->seek(std::clamp(static_cast<double>(seconds), 0.0,
                                         std::max(0.0, state.durationSeconds)));
            changed();
        });
        addWidget(_seek, 0);
    }
    if (options.time) {
        _time = new TextLabel(); _time->setFontSize(11);
        _time->setVerticalAlignment(TextLabel::VAlignment::Center);
        addWidget(_time, 130);
    }
    if (options.status) {
        _status = new TextLabel(); _status->setFontSize(11);
        _status->setVerticalAlignment(TextLabel::VAlignment::Center);
        addWidget(_status, 68);
    }
    refresh();
}
void PlaybackControls::changed() {
    refresh();
    if (_onChanged) _onChanged();
}
void PlaybackControls::refresh() {
    const auto state = _source ? _source->playbackState() : PlaybackState{};
    _refreshing = true;
    for (auto* button : {_play, _pause, _stop}) if (button) button->setEnabled(state.available);
    const double duration = std::isfinite(state.durationSeconds) ? std::max(0.0, state.durationSeconds) : 0.0;
    const double position = std::isfinite(state.positionSeconds) ? std::clamp(state.positionSeconds, 0.0, duration) : 0.0;
    if (_seek) {
        _seek->setEnabled(state.available);
        _seek->setValueRange(0, static_cast<float>(std::min(std::max(0.001, duration),
                                                         static_cast<double>(std::numeric_limits<float>::max()))));
        _seek->setValue(static_cast<float>(std::min(position,
            static_cast<double>(std::numeric_limits<float>::max()))));
    }
    setText(_time, formatTime(position, TimeDisplay::Seconds, _options.timePrecision)
                  + L" / " + formatTime(duration, TimeDisplay::Seconds, _options.timePrecision));
    setText(_status, !state.available ? L"Unavailable" : state.playing ? L"Playing"
                                         : position > 0 ? L"Paused" : L"Stopped");
    if (_loop) {
        _loop->setVisible(state.looping.has_value());
        _loop->setEnabled(state.available && state.looping.has_value());
        const std::wstring text = state.looping.value_or(false) ? L"Loop: On" : L"Loop: Off";
        if (_loop->getText() != text) _loop->setText(text);
    }
    if (_rate) {
        _rate->setVisible(state.rate.has_value());
        _rate->setEnabled(state.available && state.rate.has_value());
        int index = -1;
        if (state.rate) for (int i = 0; i < 5; ++i) if (std::fabs(*state.rate - kRates[i]) < 1e-5f) index = i;
        _rate->setSelectedIndex(index);
    }
    _refreshing = false;
}
} // namespace ayt::ui::authoring
