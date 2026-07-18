#pragma once

#include <pulp/view/animation.hpp>
#include <pulp/view/frame_clock.hpp>
#include <pulp/view/parameter_binding.hpp>
#include <pulp/view/widgets.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pulp::examples {

namespace pulp_kit_ui {

using Control = std::pair<state::ParamID, std::string_view>;

/// One MIDI-ordered voice column. It polls a shared atomic hit counter from the
/// UI frame clock, then paints a bright transient behind the controls. The
/// shared activity object survives either side of processor/view teardown.
class VoiceColumn final : public view::GroupBox {
public:
    VoiceColumn(std::shared_ptr<PulpKitUiActivity> activity,
                std::shared_ptr<PulpKitManualTriggers> manual_triggers,
                PulpKitVoiceIndex voice)
        : activity_(std::move(activity)),
          manual_triggers_(std::move(manual_triggers)), voice_(voice) {
        last_hit_ = activity_->sequence(voice_);
        set_cursor(view::View::CursorStyle::pointer);
    }

    ~VoiceColumn() override { unsubscribe_activity(); }

    void on_frame_clock_changed() override {
        auto* clock = frame_clock();
        if (clock == subscribed_clock_) return;
        unsubscribe_activity();
        if (!clock) return;
        subscribed_clock_ = clock;
        activity_subscription_ = clock->subscribe_activity(
            [this](float) { poll_activity(); });
    }

    void paint(canvas::Canvas& canvas) override {
        view::GroupBox::paint(canvas);
        const auto b = local_bounds();
        canvas.set_fill_color(canvas::Color::rgba8(255, 176, 72));
        canvas.set_font("Inter", 10.0f);
        canvas.set_text_align(canvas::TextAlign::right);
        canvas.fill_text("▶", b.width - 7.0f, 20.0f);
        if (flash_ <= 0.001f) return;

        const auto alpha = static_cast<std::uint8_t>(55.0f + flash_ * 105.0f);
        canvas.set_fill_color(canvas::Color::rgba8(255, 151, 43, alpha));
        canvas.fill_rounded_rect(1.0f, 1.0f, b.width - 2.0f, b.height - 2.0f, 7.0f);
        canvas.set_stroke_color(canvas::Color::rgba8(255, 206, 92));
        canvas.set_line_width(2.0f + flash_ * 2.0f);
        canvas.stroke_rounded_rect(2.0f, 2.0f, b.width - 4.0f, b.height - 4.0f, 6.0f);
    }

    float flash_intensity() const { return flash_; }
    void on_mouse_event(const view::MouseEvent& event) override {
        // Knobs win hit-testing as child views. A press that reaches the
        // column therefore landed on its title or otherwise-empty background,
        // both of which are useful audition-pad targets.
        if (event.isPress() && event.button == view::MouseButton::left) {
            manual_triggers_->signal(voice_);
            return;
        }
        view::GroupBox::on_mouse_event(event);
    }

    void poll_activity() {
        if (!activity_->consume(voice_, last_hit_)) return;
        flash_ = 1.0f;
        request_repaint();
        animate(
            [this](float value) { flash_ = value; }, 1.0f, 0.0f, 0.42f,
            view::easing::ease_out_cubic, {}, "midi-hit");
    }

private:
    void unsubscribe_activity() {
        if (activity_subscription_ != -1 && subscribed_clock_)
            subscribed_clock_->unsubscribe_activity(activity_subscription_);
        activity_subscription_ = -1;
        subscribed_clock_ = nullptr;
    }

    std::shared_ptr<PulpKitUiActivity> activity_;
    std::shared_ptr<PulpKitManualTriggers> manual_triggers_;
    PulpKitVoiceIndex voice_;
    PulpKitUiActivity::Sequence last_hit_ = 0;
    float flash_ = 0.0f;
    view::FrameClock* subscribed_clock_ = nullptr;
    int activity_subscription_ = -1;
};

class PulpKitEditor final : public view::View {
public:
    PulpKitEditor(state::StateStore& store,
                  std::shared_ptr<PulpKitUiActivity> activity,
                  std::shared_ptr<PulpKitManualTriggers> manual_triggers)
        : store_(store), activity_(std::move(activity)),
          manual_triggers_(std::move(manual_triggers)) {
        set_bounds({0, 0, 1200, 330});
        set_theme(view::Theme::dark());
        set_background_color(canvas::Color::rgba8(0x12, 0x17, 0x1D));
        flex().direction = view::FlexDirection::column;
        flex().padding = 10.0f;
        flex().gap = 6.0f;

        auto heading = std::make_unique<view::Label>(
            "PulpKit   MIDI voices 36 → 75");
        heading->set_font_size(18.0f);
        heading->set_font_weight(700);
        heading->flex().preferred_height = 24.0f;
        add_child(std::move(heading));

        auto surface = std::make_unique<view::View>();
        surface->set_id("kit-surface");
        surface->flex().direction = view::FlexDirection::row;
        surface->flex().align_items = view::FlexAlign::stretch;
        surface->flex().gap = 4.0f;
        surface->flex().flex_grow = 1.0f;

        // Strictly ordered by each voice's lowest mapped MIDI note.
        add_voice(*surface, "BD 36", "voice:36", kPulpKitVoiceKick, {
            {kPulpKitKickLevel, "Level"}, {kPulpKitKickTone, "Tone"},
            {kPulpKitKickDecay, "Decay"}, {kPulpKitKickTune, "Tune"}});
        add_voice(*surface, "RS 37", "voice:37", kPulpKitVoiceRim, {
            {kPulpKitRimLevel, "Level"}, {kPulpKitRimTune, "Tune"},
            {kPulpKitRimDecay, "Decay"}, {kPulpKitRimTone, "Tone"}});
        add_voice(*surface, "SD 38", "voice:38", kPulpKitVoiceSnare, {
            {kPulpKitSnareLevel, "Level"}, {kPulpKitSnareTone, "Tone"},
            {kPulpKitSnareSnappy, "Snappy"}, {kPulpKitSnareTune, "Tune"},
            {kPulpKitSnareDecay, "Decay"}, {kPulpKitSnareBalance, "Balance"}});
        add_voice(*surface, "CP 39", "voice:39", kPulpKitVoiceClap, {
            {kPulpKitClapLevel, "Level"}, {kPulpKitClapTune, "Tune"},
            {kPulpKitClapDecay, "Decay"}, {kPulpKitClapTone, "Tone"}});
        add_voice(*surface, "LT 41", "voice:41", kPulpKitVoiceLowTom, {
            {kPulpKitLowTomLevel, "Level"}, {kPulpKitLowTomTune, "Tuning"},
            {kPulpKitLowTomDecay, "Decay"}, {kPulpKitLowTomTone, "Tone"}});
        add_voice(*surface, "CH 42", "voice:42", kPulpKitVoiceClosedHat, {
            {kPulpKitClosedHatLevel, "Level"}, {kPulpKitClosedHatTune, "Tune"},
            {kPulpKitClosedHatDecay, "Decay"}, {kPulpKitClosedHatTone, "Tone"}});
        add_voice(*surface, "MT 45", "voice:45", kPulpKitVoiceMidTom, {
            {kPulpKitMidTomLevel, "Level"}, {kPulpKitMidTomTune, "Tuning"},
            {kPulpKitMidTomDecay, "Decay"}, {kPulpKitMidTomTone, "Tone"}});
        add_voice(*surface, "OH 46", "voice:46", kPulpKitVoiceOpenHat, {
            {kPulpKitOpenHatLevel, "Level"}, {kPulpKitOpenHatDecay, "Decay"},
            {kPulpKitOpenHatTune, "Tune"}, {kPulpKitOpenHatTone, "Tone"}});
        add_voice(*surface, "HT 48", "voice:48", kPulpKitVoiceHiTom, {
            {kPulpKitHiTomLevel, "Level"}, {kPulpKitHiTomTune, "Tuning"},
            {kPulpKitHiTomDecay, "Decay"}, {kPulpKitHiTomTone, "Tone"}});
        add_voice(*surface, "CY 49", "voice:49", kPulpKitVoiceCymbal, {
            {kPulpKitCymbalLevel, "Level"}, {kPulpKitCymbalTone, "Tone"},
            {kPulpKitCymbalDecay, "Decay"}, {kPulpKitCymbalTune, "Tune"}});
        add_voice(*surface, "CB 51", "voice:51", kPulpKitVoiceCowbell, {
            {kPulpKitCowbellLevel, "Level"}, {kPulpKitCowbellTune, "Tune"},
            {kPulpKitCowbellDecay, "Decay"}, {kPulpKitCowbellTone, "Tone"}});
        add_voice(*surface, "MA 70", "voice:70", kPulpKitVoiceMaracas, {
            {kPulpKitMaracasLevel, "Level"}, {kPulpKitMaracasTune, "Tune"},
            {kPulpKitMaracasDecay, "Decay"}, {kPulpKitMaracasTone, "Tone"}});
        add_voice(*surface, "CL 75", "voice:75", kPulpKitVoiceClave, {
            {kPulpKitClaveLevel, "Level"}, {kPulpKitClaveTune, "Tune"},
            {kPulpKitClaveDecay, "Decay"}, {kPulpKitClaveTone, "Tone"}});
        add_child(std::move(surface));
    }

private:
    std::string format_value(state::ParamID id, float normalized) const {
        const auto* info = store_.info(id);
        if (!info) return {};
        const float value = info->range.denormalize(normalized);
        if (info->to_string) return info->to_string(value);

        char number[32];
        if (std::abs(value) >= 100.0f)
            std::snprintf(number, sizeof(number), "%.0f", value);
        else if (std::abs(value) >= 10.0f)
            std::snprintf(number, sizeof(number), "%.1f", value);
        else
            std::snprintf(number, sizeof(number), "%.2f", value);
        return std::string(number) + (info->unit.empty() ? "" : " " + info->unit);
    }

    void add_knob(view::View& parent, state::ParamID id, std::string_view label) {
        auto knob = std::make_unique<view::Knob>();
        knob->set_id("control:" + std::to_string(id));
        knob->set_label(std::string(label));
        knob->set_format([this, id](float normalized) {
            return format_value(id, normalized);
        });
        knob->flex().preferred_width = 38.0f;
        knob->flex().preferred_height = 38.0f;
        knob->flex().flex_shrink = 1.0f;
        bindings_.push_back(view::bind_parameter(*knob, store_, id));
        parent.add_child(std::move(knob));
    }

    void add_voice(view::View& surface, std::string title, std::string id,
                   PulpKitVoiceIndex voice,
                   std::initializer_list<Control> controls) {
        auto column = std::make_unique<VoiceColumn>(
            activity_, manual_triggers_, voice);
        column->set_title(std::move(title));
        column->set_id(std::move(id));
        column->flex().direction = view::FlexDirection::column;
        column->flex().align_items = view::FlexAlign::center;
        column->flex().padding = 3.0f;
        column->flex().padding_top = 34.0f;
        column->flex().gap = 2.0f;
        column->flex().preferred_width = 82.0f;
        column->flex().min_width = 52.0f;
        column->flex().flex_grow = 1.0f;
        column->flex().flex_shrink = 1.0f;
        for (const auto& [param, label] : controls)
            add_knob(*column, param, label);
        surface.add_child(std::move(column));
    }

    state::StateStore& store_;
    std::shared_ptr<PulpKitUiActivity> activity_;
    std::shared_ptr<PulpKitManualTriggers> manual_triggers_;
    // Destroy listeners before View's base destructor destroys their widgets.
    std::vector<view::ParameterBinding> bindings_;
};

}  // namespace pulp_kit_ui

inline std::unique_ptr<view::View> PulpKit::create_view() {
    return std::make_unique<pulp_kit_ui::PulpKitEditor>(
        state(), ui_activity_, manual_triggers_);
}

}  // namespace pulp::examples
