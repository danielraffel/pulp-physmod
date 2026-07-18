#pragma once

#include <pulp/view/parameter_binding.hpp>
#include <pulp/view/ui_components.hpp>
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

/// A wrapping page whose content height follows the current viewport width.
/// At the preferred editor size every card is visible; smaller host windows
/// reflow the cards and gain vertical scrolling instead of clipping controls.
class WrappingPage final : public view::ScrollView {
public:
    WrappingPage(float item_width, float item_height, float gap = 8.0f,
                 float padding = 12.0f)
        : item_width_(item_width), item_height_(item_height), gap_(gap),
          padding_(padding) {
        set_direction(Direction::vertical);

        auto content = std::make_unique<view::View>();
        content_ = content.get();
        content_->flex().direction = view::FlexDirection::row;
        content_->flex().flex_wrap = view::FlexWrap::wrap;
        content_->flex().align_items = view::FlexAlign::start;
        content_->flex().align_content = view::FlexAlign::start;
        content_->flex().gap = gap_;
        content_->flex().padding = padding_;
        add_child(std::move(content));
    }

    void add_card(std::unique_ptr<view::View> card) {
        card->flex().preferred_width = item_width_;
        card->flex().preferred_height = item_height_;
        card->flex().flex_shrink = 0.0f;
        content_->add_child(std::move(card));
        ++item_count_;
    }

    void on_resized() override { update_content_extent(); }

    void layout_children() override {
        update_content_extent();
        view::ScrollView::layout_children();
    }

private:
    void update_content_extent() {
        const float viewport_width = std::max(1.0f, local_bounds().width);
        const float usable = std::max(item_width_, viewport_width - 2.0f * padding_);
        const int columns = std::max(
            1, static_cast<int>(std::floor((usable + gap_) / (item_width_ + gap_))));
        const int rows = item_count_ == 0 ? 0 : (item_count_ + columns - 1) / columns;
        const float content_height = 2.0f * padding_ +
            static_cast<float>(rows) * item_height_ +
            static_cast<float>(std::max(0, rows - 1)) * gap_;

        content_->flex().preferred_width = viewport_width;
        content_->flex().preferred_height = content_height;
        set_content_size({viewport_width, content_height});
    }

    view::View* content_ = nullptr;
    float item_width_ = 0.0f;
    float item_height_ = 0.0f;
    float gap_ = 0.0f;
    float padding_ = 0.0f;
    int item_count_ = 0;
};

class PulpKitEditor final : public view::View {
public:
    explicit PulpKitEditor(state::StateStore& store) : store_(store) {
        set_bounds({0, 0, 1200, 480});
        set_theme(view::Theme::dark());
        set_background_color(canvas::Color::rgba8(0x12, 0x17, 0x1D));
        flex().direction = view::FlexDirection::column;
        flex().padding = 12.0f;
        flex().gap = 8.0f;

        auto heading = std::make_unique<view::Label>("PulpKit");
        heading->set_font_size(20.0f);
        heading->set_font_weight(700);
        heading->flex().preferred_height = 28.0f;
        add_child(std::move(heading));

        auto tabs = std::make_unique<view::TabPanel>();
        tabs->set_id("surface-tabs");
        tabs->set_tab_bar_style(view::TabPanel::TabBarStyle::underline);
        tabs->flex().flex_grow = 1.0f;
        tabs->add_tab("Classic 808 Controls", build_classic_page());
        tabs->add_tab("Extended", build_extended_page());
        add_child(std::move(tabs));
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

    void add_knob(view::View& parent, state::ParamID id, std::string_view label,
                  std::string_view surface, float size) {
        const bool classic_level = surface == "classic" && label == "Level";
        auto knob = std::make_unique<view::Knob>();
        auto* knob_raw = knob.get();
        knob->set_id(std::string(surface) + ":" + std::to_string(id));
        knob->set_label(std::string(label));
        knob->set_format([this, id, classic_level](float normalized) {
            return format_value(id, classic_level ? normalized * 0.5f : normalized);
        });
        knob->flex().preferred_width = size;
        knob->flex().preferred_height = size;
        if (classic_level) {
            // Released Level params retain their 0..200% creative range. The
            // Classic panel maps its full knob travel onto 0..100%, matching
            // the original panel; the +6 dB headroom remains Extended-only.
            knob->set_value(std::min(1.0f, store_.get_normalized(id) * 2.0f));
            knob->on_gesture_begin = [this, id] { store_.begin_gesture(id); };
            knob->on_change = [this, id](float v) {
                store_.set_normalized(id, std::clamp(v, 0.0f, 1.0f) * 0.5f);
            };
            knob->on_gesture_end = [this, id] { store_.end_gesture(id); };
            bindings_.emplace_back(store_.add_listener(
                [this, knob_raw, id](state::ParamID changed, float) {
                    if (changed == id)
                        knob_raw->set_value(
                            std::min(1.0f, store_.get_normalized(id) * 2.0f));
                },
                state::ListenerThread::Main));
        } else {
            bindings_.push_back(view::bind_parameter(*knob, store_, id));
        }
        parent.add_child(std::move(knob));
    }

    std::unique_ptr<view::View> make_classic_strip(
        std::string title, std::initializer_list<Control> controls) {
        auto strip = std::make_unique<view::GroupBox>();
        strip->set_title(std::move(title));
        strip->flex().direction = view::FlexDirection::column;
        strip->flex().align_items = view::FlexAlign::center;
        strip->flex().padding = 4.0f;
        strip->flex().padding_top = 36.0f;
        strip->flex().gap = 6.0f;
        for (const auto& [id, label] : controls)
            add_knob(*strip, id, label, "classic", 64.0f);
        return strip;
    }

    std::unique_ptr<view::View> make_classic_selector_strip(
        std::string title, std::string first_label, state::ParamID first_id,
        std::string second_label, state::ParamID second_id,
        std::string selector_id) {
        auto strip = std::make_unique<view::GroupBox>();
        auto* strip_raw = strip.get();
        strip->set_title(std::move(title));
        strip->flex().direction = view::FlexDirection::column;
        strip->flex().align_items = view::FlexAlign::center;
        strip->flex().padding = 4.0f;
        strip->flex().padding_top = 36.0f;
        strip->flex().gap = 8.0f;

        auto selector = std::make_unique<view::SegmentedControl>();
        selector->set_id("classic-selector:" + selector_id);
        selector->set_segments({std::move(first_label), std::move(second_label)});
        selector->flex().preferred_width = 72.0f;
        selector->flex().preferred_height = 28.0f;

        auto first = std::make_unique<view::View>();
        auto* first_raw = first.get();
        add_knob(*first, first_id, "Level", "classic", 64.0f);
        auto second = std::make_unique<view::View>();
        auto* second_raw = second.get();
        add_knob(*second, second_id, "Level", "classic", 64.0f);
        second->set_visible(false);

        selector->on_change = [first_raw, second_raw, strip_raw](int selected) {
            first_raw->set_visible(selected == 0);
            second_raw->set_visible(selected == 1);
            strip_raw->request_repaint();
        };

        strip->add_child(std::move(selector));
        strip->add_child(std::move(first));
        strip->add_child(std::move(second));
        return strip;
    }

    std::unique_ptr<view::View> build_classic_page() {
        auto page = std::make_unique<WrappingPage>(100.0f, 276.0f, 6.0f, 8.0f);
        page->set_id("classic-page");
        page->add_card(make_classic_strip("Bass Drum", {
            {kPulpKitKickLevel, "Level"}, {kPulpKitKickTone, "Tone"},
            {kPulpKitKickDecay, "Decay"}}));
        page->add_card(make_classic_strip("Snare", {
            {kPulpKitSnareLevel, "Level"}, {kPulpKitSnareTone, "Tone"},
            {kPulpKitSnareSnappy, "Snappy"}}));
        page->add_card(make_classic_strip("Low Tom", {
            {kPulpKitLowTomLevel, "Level"}, {kPulpKitLowTomTune, "Tuning"}}));
        page->add_card(make_classic_strip("Mid Tom", {
            {kPulpKitMidTomLevel, "Level"}, {kPulpKitMidTomTune, "Tuning"}}));
        page->add_card(make_classic_strip("High Tom", {
            {kPulpKitHiTomLevel, "Level"}, {kPulpKitHiTomTune, "Tuning"}}));
        page->add_card(make_classic_selector_strip(
            "RS / CL", "RS", kPulpKitRimLevel,
            "CL", kPulpKitClaveLevel, "rim-claves"));
        page->add_card(make_classic_selector_strip(
            "CP / MA", "CP", kPulpKitClapLevel,
            "MA", kPulpKitMaracasLevel, "clap-maracas"));
        page->add_card(make_classic_strip("Cowbell", {{kPulpKitCowbellLevel, "Level"}}));
        page->add_card(make_classic_strip("Cymbal", {
            {kPulpKitCymbalLevel, "Level"}, {kPulpKitCymbalTone, "Tone"},
            {kPulpKitCymbalDecay, "Decay"}}));
        page->add_card(make_classic_strip("Open Hat", {
            {kPulpKitOpenHatLevel, "Level"}, {kPulpKitOpenHatDecay, "Decay"}}));
        page->add_card(make_classic_strip("Closed Hat", {
            {kPulpKitClosedHatLevel, "Level"}}));
        return page;
    }

    std::unique_ptr<view::View> make_extended_group(
        std::string title, std::initializer_list<Control> controls) {
        auto group = std::make_unique<view::GroupBox>();
        group->set_title(std::move(title));
        group->flex().direction = view::FlexDirection::row;
        group->flex().align_items = view::FlexAlign::center;
        group->flex().justify_content = view::FlexJustify::center;
        group->flex().padding = 4.0f;
        group->flex().padding_top = 36.0f;
        group->flex().gap = 4.0f;
        for (const auto& [id, label] : controls)
            add_knob(*group, id, label, "extended", 44.0f);
        return group;
    }

    void add_uniform_group(WrappingPage& page, std::string title,
                           state::ParamID base) {
        page.add_card(make_extended_group(std::move(title), {
            {base + 0, "Level"}, {base + 1, "Tune"},
            {base + 2, "Decay"}, {base + 3, "Tone"}}));
    }

    std::unique_ptr<view::View> build_extended_page() {
        auto page = std::make_unique<WrappingPage>(300.0f, 126.0f, 10.0f, 10.0f);
        page->set_id("extended-page");
        add_uniform_group(*page, "Bass Drum", kPulpKitKickLevel);
        page->add_card(make_extended_group("Snare", {
            {kPulpKitSnareLevel, "Level"}, {kPulpKitSnareTune, "Tune"},
            {kPulpKitSnareDecay, "Decay"}, {kPulpKitSnareTone, "Tone"},
            {kPulpKitSnareBalance, "Balance"},
            {kPulpKitSnareSnappy, "Snappy"}}));
        add_uniform_group(*page, "Low Tom", kPulpKitLowTomLevel);
        add_uniform_group(*page, "Mid Tom", kPulpKitMidTomLevel);
        add_uniform_group(*page, "High Tom", kPulpKitHiTomLevel);
        add_uniform_group(*page, "Rim Shot", kPulpKitRimLevel);
        add_uniform_group(*page, "Claves", kPulpKitClaveLevel);
        add_uniform_group(*page, "Hand Clap", kPulpKitClapLevel);
        add_uniform_group(*page, "Closed Hat", kPulpKitClosedHatLevel);
        add_uniform_group(*page, "Open Hat", kPulpKitOpenHatLevel);
        add_uniform_group(*page, "Cymbal", kPulpKitCymbalLevel);
        add_uniform_group(*page, "Cowbell", kPulpKitCowbellLevel);
        add_uniform_group(*page, "Maracas", kPulpKitMaracasLevel);
        return page;
    }

    state::StateStore& store_;
    // Destroy listeners before View's base destructor destroys their widgets.
    std::vector<view::ParameterBinding> bindings_;
};

}  // namespace pulp_kit_ui

inline std::unique_ptr<view::View> PulpKit::create_view() {
    return std::make_unique<pulp_kit_ui::PulpKitEditor>(state());
}

}  // namespace pulp::examples
