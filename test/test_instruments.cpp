// A smoke test per instrument: each one loads, renders real audio through its
// Processor, and is measured -- non-silent, finite, bounded, and (where the
// instrument is pitched) at the note's pitch. The deep physics tests for each
// voice live with the DSP primitives in the Pulp core; these prove the shipped
// plugins actually make sound and behave.
#include "support/instrument_probe.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <pulp/view/ui_components.hpp>
#include <pulp/view/screenshot.hpp>
#include <pulp/view/widgets.hpp>

#include <array>
#include <fstream>

#include "va-drum/va_drum.hpp"
#include "pulp-kit/pulp_kit.hpp"
#include "modal-instrument/modal_instrument.hpp"
#include "prepared-piano/prepared_piano.hpp"
#include "bowed-string/bowed_string_instrument.hpp"
#include "gong/gong_instrument.hpp"

using namespace physmod::test;
using pulp::examples::create_va_drum;
using pulp::examples::create_pulp_kit;
using pulp::examples::create_modal_instrument;
using pulp::examples::create_prepared_piano;
using pulp::examples::create_bowed_string;
using pulp::examples::create_gong;

namespace {
double cents(double f, double ref) { return 1200.0 * std::log2(f / ref); }

pulp::view::View* find_view(pulp::view::View& root, const std::string& id) {
    if (root.id() == id) return &root;
    for (std::size_t i = 0; i < root.child_count(); ++i) {
        if (auto* found = find_view(*root.child_at(i), id)) return found;
    }
    return nullptr;
}

std::size_t count_knobs(pulp::view::View& root) {
    std::size_t count = dynamic_cast<pulp::view::Knob*>(&root) ? 1u : 0u;
    for (std::size_t i = 0; i < root.child_count(); ++i)
        count += count_knobs(*root.child_at(i));
    return count;
}

template <std::size_t N>
bool strictly_increasing(const std::array<double, N>& values) {
    for (std::size_t i = 1; i < N; ++i)
        if (!(values[i] > values[i - 1])) return false;
    return true;
}

float classic_position_value(pulp::state::ParamID id, int position) {
    using namespace pulp::examples;
    if (id == kPulpKitKickTune)
        return 0.5f + 1.5f * static_cast<float>(position) / 100.0f;
    return static_cast<float>(position);
}

std::vector<float> render_pulpkit_position(pulp::state::ParamID id, int position) {
    auto kit = create_pulp_kit();
    return render(*kit, {{0, 36, 100}}, 2.5,
                  {{id, classic_position_value(id, position)}});
}
}  // namespace

TEST_CASE("VaDrum renders a bounded, non-silent bass drum", "[instrument][va-drum]") {
    auto p = create_va_drum();
    const auto y = render(*p, {{int(0.01 * kFs), 36, 110}}, 1.5);
    REQUIRE(all_finite(y));
    REQUIRE(peak(y) > 0.01);       // it speaks
    REQUIRE(peak(y) <= 1.5);       // and is not blowing up
    // MIDI 36 is one octave above VaDrum's reference root after chromatic
    // playback landed, so the stock ~48 Hz circuit pitch appears near 96 Hz.
    REQUIRE(estimate_f0(y, 0.1, 0.4, 30.0, 120.0) ==
            Catch::Approx(96.39).margin(2.0));
}

TEST_CASE("all processor descriptors carry the release version", "[release][version]") {
    const std::array processors{
        create_va_drum(), create_pulp_kit(), create_modal_instrument(),
        create_prepared_piano(), create_bowed_string(), create_gong()};
    for (const auto& processor : processors)
        CHECK(processor->descriptor().version == PULP_PHYSMOD_VERSION);
}

TEST_CASE("PulpKit routes notes to voices and stays bounded", "[instrument][pulp-kit]") {
    auto p = create_pulp_kit();
    // kick, snare, closed hat at different times
    const auto y = render(*p, {{int(0.01 * kFs), 36, 120},
                               {int(0.3 * kFs), 38, 120},
                               {int(0.6 * kFs), 42, 120}}, 1.2);
    REQUIRE(all_finite(y));
    REQUIRE(peak(y) > 0.01);
    REQUIRE(peak(y) <= 1.5);
}

TEST_CASE("PulpKit appends authentic Snappy without moving released ids",
          "[instrument][pulp-kit][parameters]") {
    using namespace pulp::examples;
    STATIC_REQUIRE(kPulpKitKickLevel == 0);
    STATIC_REQUIRE(kPulpKitMaracasTone == 52);
    STATIC_REQUIRE(kPulpKitSnareSnappy == 53);
    STATIC_REQUIRE(kPulpKitParamCount == 13 * 4 + 2);

    PulpKit kit;
    pulp::state::StateStore store;
    kit.set_state_store(&store);
    kit.define_parameters(store);

    REQUIRE(store.param_count() == kPulpKitParamCount);
    REQUIRE(store.all_groups().size() == 13);
    REQUIRE(store.info(kPulpKitSnareSnappy) != nullptr);
    REQUIRE(store.info(kPulpKitSnareSnappy)->name == "Snare Snappy");
    REQUIRE(store.get_value(kPulpKitSnareSnappy) == Catch::Approx(100.0f));

    // Exercise the exact host-state contract across all released ids plus the
    // appended Snappy id. A position-dependent pattern catches omissions and
    // accidental id aliases without depending on any one parameter's range.
    for (int id = 0; id < kPulpKitParamCount; ++id)
        store.set_normalized(id, static_cast<float>(id + 1) /
                                  static_cast<float>(kPulpKitParamCount + 1));
    const auto blob = store.serialize();

    PulpKit restored_kit;
    pulp::state::StateStore restored;
    restored_kit.set_state_store(&restored);
    restored_kit.define_parameters(restored);
    REQUIRE(restored.deserialize(blob));
    REQUIRE(restored.param_count() == kPulpKitParamCount);
    for (int id = 0; id < kPulpKitParamCount; ++id)
        CHECK(restored.get_normalized(id) == Catch::Approx(store.get_normalized(id)));
}

TEST_CASE("PulpKit Snappy is neutral at default and scales only the noise path",
          "[instrument][pulp-kit][snappy]") {
    using pulp::examples::SnareVoice;
    SnareVoice released_default;
    SnareVoice explicit_neutral;
    released_default.prepare(kFs);
    explicit_neutral.prepare(kFs);
    explicit_neutral.set_snappy_level(1.0f);
    released_default.trigger(0.8);
    explicit_neutral.trigger(0.8);

    for (int i = 0; i < 4096; ++i)
        REQUIRE(released_default.process() == explicit_neutral.process());

    SnareVoice shell_snappy_off;
    SnareVoice shell_snappy_on;
    shell_snappy_off.prepare(kFs);
    shell_snappy_on.prepare(kFs);
    shell_snappy_off.set_balance(0.0f);
    shell_snappy_on.set_balance(0.0f);
    shell_snappy_off.set_snappy_level(0.0f);
    shell_snappy_on.set_snappy_level(1.0f);
    shell_snappy_off.trigger(0.8);
    shell_snappy_on.trigger(0.8);
    for (int i = 0; i < 4096; ++i)
        REQUIRE(shell_snappy_off.process() == shell_snappy_on.process());

    SnareVoice noise_off;
    SnareVoice noise_on;
    noise_off.prepare(kFs);
    noise_on.prepare(kFs);
    noise_off.set_balance(1.0f);
    noise_on.set_balance(1.0f);
    noise_off.set_snappy_level(0.0f);
    noise_on.set_snappy_level(1.0f);
    noise_off.trigger(0.8);
    noise_on.trigger(0.8);
    double off_energy = 0.0;
    double on_energy = 0.0;
    for (int i = 0; i < 4096; ++i) {
        const double off = noise_off.process();
        const double on = noise_on.process();
        off_energy += off * off;
        on_energy += on * on;
    }
    REQUIRE(off_energy == 0.0);
    REQUIRE(on_energy > 0.0);
}

TEST_CASE("PulpKit editor exposes the classic panel and every extended control",
          "[instrument][pulp-kit][editor]") {
    using namespace pulp::examples;
    PulpKit kit;
    pulp::state::StateStore store;
    kit.set_state_store(&store);
    kit.define_parameters(store);

    auto editor = kit.create_view();
    REQUIRE(editor != nullptr);
    editor->set_bounds({0, 0, 1200, 640});
    editor->layout_children();

    REQUIRE(find_view(*editor, "classic:" + std::to_string(kPulpKitKickLevel)) != nullptr);
    REQUIRE(find_view(*editor, "classic:" + std::to_string(kPulpKitSnareSnappy)) != nullptr);
    REQUIRE(find_view(*editor, "classic:" + std::to_string(kPulpKitClosedHatLevel)) != nullptr);
    auto* rim_selector = dynamic_cast<pulp::view::SegmentedControl*>(
        find_view(*editor, "classic-selector:rim-claves"));
    auto* clap_selector = dynamic_cast<pulp::view::SegmentedControl*>(
        find_view(*editor, "classic-selector:clap-maracas"));
    REQUIRE(rim_selector != nullptr);
    REQUIRE(clap_selector != nullptr);
    CHECK(rim_selector->segments() == std::vector<std::string>({"RS", "CL"}));
    CHECK(clap_selector->segments() == std::vector<std::string>({"CP", "MA"}));

    editor->set_bounds({0, 0, 1200, 480});
    editor->layout_children();
    auto* classic_page = dynamic_cast<pulp::view::ScrollView*>(
        find_view(*editor, "classic-page"));
    auto* extended_page = dynamic_cast<pulp::view::ScrollView*>(
        find_view(*editor, "extended-page"));
    REQUIRE(classic_page != nullptr);
    REQUIRE(extended_page != nullptr);
    CHECK_FALSE(classic_page->wants_wheel_scroll());
    auto* tabs = dynamic_cast<pulp::view::TabPanel*>(
        find_view(*editor, "surface-tabs"));
    REQUIRE(tabs != nullptr);

    const auto classic_png = pulp::view::render_to_png(
        *editor, 1200, 480, 1.0f, pulp::view::ScreenshotBackend::skia);
    REQUIRE(classic_png.size() > 1000);
    std::ofstream("/tmp/pulpkit-classic.png", std::ios::binary)
        .write(reinterpret_cast<const char*>(classic_png.data()),
               static_cast<std::streamsize>(classic_png.size()));

    REQUIRE(tabs->set_active_tab("Extended", pulp::view::Notify::none));
    editor->layout_children();
    CHECK(extended_page->content_size().height > extended_page->local_bounds().height);
    CHECK(extended_page->wants_wheel_scroll());

    const auto extended_png = pulp::view::render_to_png(
        *editor, 1200, 480, 1.0f, pulp::view::ScreenshotBackend::skia);
    REQUIRE(extended_png.size() > 1000);
    std::ofstream("/tmp/pulpkit-extended.png", std::ios::binary)
        .write(reinterpret_cast<const char*>(extended_png.data()),
               static_cast<std::streamsize>(extended_png.size()));

    extended_page->scroll_by(0.0f, 100.0f, false);
    CHECK(extended_page->target_scroll_y() > 0.0f);

    auto* classic_level = dynamic_cast<pulp::view::Knob*>(
        find_view(*editor, "classic:" + std::to_string(kPulpKitKickLevel)));
    auto* extended_level = dynamic_cast<pulp::view::Knob*>(
        find_view(*editor, "extended:" + std::to_string(kPulpKitKickLevel)));
    REQUIRE(classic_level != nullptr);
    REQUIRE(extended_level != nullptr);
    classic_level->on_change(1.0f);
    CHECK(store.get_value(kPulpKitKickLevel) == Catch::Approx(100.0f));
    extended_level->on_change(1.0f);
    CHECK(store.get_value(kPulpKitKickLevel) == Catch::Approx(200.0f));

    for (int id = 0; id < kPulpKitParamCount; ++id)
        REQUIRE(find_view(*editor, "extended:" + std::to_string(id)) != nullptr);

    // 23 original-style controls plus all 54 controls in Extended.
    REQUIRE(count_knobs(*editor) == 23 + kPulpKitParamCount);
}

TEST_CASE("PulpKit bass-drum sweeps pin numeric reference-facing behavior",
          "[instrument][pulp-kit][parity]") {
    using namespace pulp::examples;
    constexpr std::array<int, 5> positions{0, 25, 50, 75, 100};

    std::array<double, 5> tone{};
    std::array<double, 5> decay{};
    std::array<double, 5> tune{};
    std::array<double, 5> level{};
    for (std::size_t i = 0; i < positions.size(); ++i) {
        tone[i] = difference_brightness(
            render_pulpkit_position(kPulpKitKickTone, positions[i]));
        decay[i] = estimate_t60(render_pulpkit_position(kPulpKitKickDecay, positions[i]));
        tune[i] = estimate_f0(render_pulpkit_position(kPulpKitKickTune, positions[i]),
                              0.08, 0.35, 15.0, 120.0);
        level[i] = rms(render_pulpkit_position(kPulpKitKickLevel, positions[i]), 0.0, 2.5);
    }

    // All four shipped controls must remain audibly effective across the
    // surface; these numeric anchors turn a disconnected/no-op knob into a CI
    // failure rather than relying on listening or committed reference audio.
    REQUIRE(strictly_increasing(tone));
    REQUIRE(strictly_increasing(decay));
    REQUIRE(strictly_increasing(tune));
    REQUIRE(level[0] == 0.0);
    REQUIRE(strictly_increasing(level));
    constexpr std::array<double, 5> expected_tone{
        0.009728, 0.009994, 0.010438, 0.011427, 0.020552};
    constexpr std::array<double, 5> expected_decay{
        0.3261, 0.5137, 1.0350, 1.7642, 2.6578};
    constexpr std::array<double, 5> expected_tune{
        23.976, 42.819, 60.683, 80.0, 96.579};
    constexpr std::array<double, 5> expected_level{
        0.0, 0.007482, 0.014964, 0.022445, 0.029927};
    constexpr std::array<double, 5> tone_margin{
        0.0002, 0.0002, 0.0002, 0.0002, 0.0004};
    constexpr std::array<double, 5> decay_margin{0.02, 0.03, 0.05, 0.08, 0.12};
    constexpr std::array<double, 5> tune_margin{0.5, 0.5, 0.7, 0.8, 1.0};
    constexpr std::array<double, 5> level_margin{1e-8, 0.0002, 0.0003, 0.0004, 0.0005};
    for (std::size_t i = 0; i < positions.size(); ++i) {
        CHECK(tone[i] == Catch::Approx(expected_tone[i]).margin(tone_margin[i]));
        CHECK(decay[i] == Catch::Approx(expected_decay[i]).margin(decay_margin[i]));
        CHECK(tune[i] == Catch::Approx(expected_tune[i]).margin(tune_margin[i]));
        CHECK(level[i] == Catch::Approx(expected_level[i]).margin(level_margin[i]));
    }

    // Trusted black-box Roland AU 1.1.4 sweep, kept sample-free: from 25%
    // upward PulpKit's T60 follows the measured Decay curve within 10%. The
    // minimum remains an explicitly measured gap (ours 0.326 s vs 0.128 s),
    // so it is pinned above but not mislabeled as matching.
    constexpr std::array<double, 5> reference_t60{
        0.12772, 0.47612, 1.01911, 1.77928, 2.57992};
    for (std::size_t i = 1; i < decay.size(); ++i)
        CHECK(decay[i] == Catch::Approx(reference_t60[i]).epsilon(0.10));

    // PulpKit's released Tune span is wider than the Roland software sweep,
    // but its shipped default (plain 1.0) is calibrated to the measured centre.
    auto default_tune = create_pulp_kit();
    const auto default_y = render(*default_tune, {{0, 36, 100}}, 2.5,
                                  {{kPulpKitKickTune, 1.0f}});
    CHECK(estimate_f0(default_y, 0.08, 0.35, 15.0, 120.0) ==
          Catch::Approx(48.1928).margin(0.5));
}

TEST_CASE("ModalInstrument renders at the note's pitch", "[instrument][modal]") {
    auto p = create_modal_instrument();
    const auto y = render(*p, {{int(0.01 * kFs), 60, 110}}, 1.0);
    REQUIRE(all_finite(y));
    REQUIRE(peak(y) > 0.005);
    const double f0 = estimate_f0(y, 0.05, 0.5, 200.0, 340.0);
    REQUIRE(std::fabs(cents(f0, 261.63)) < 50.0);  // within a semitone of middle C
}

TEST_CASE("PreparedPiano renders a pitched string", "[instrument][prepared-piano]") {
    auto p = create_prepared_piano();
    const auto y = render(*p, {{int(0.01 * kFs), 60, 110}}, 1.0);
    REQUIRE(all_finite(y));
    REQUIRE(peak(y) > 0.005);
    REQUIRE(peak(y) <= 2.0);
    const double f0 = estimate_f0(y, 0.05, 0.5, 200.0, 340.0);
    REQUIRE(std::fabs(cents(f0, 261.63)) < 60.0);
}

TEST_CASE("BowedString sustains rather than decaying", "[instrument][bowed-string]") {
    auto p = create_bowed_string();
    // Hold the note for the whole render.
    const auto y = render(*p, {{int(0.01 * kFs), 57, 100}}, 2.0);
    REQUIRE(all_finite(y));
    REQUIRE(peak(y) <= 2.0);
    // A bow holds energy in: late RMS is not a small fraction of mid RMS the way
    // a plucked/struck decay would be.
    const double mid = rms(y, 0.5, 0.8);
    const double late = rms(y, 1.5, 1.8);
    REQUIRE(mid > 0.005);
    REQUIRE(late > 0.5 * mid);
}

TEST_CASE("Gong rings and stays bounded", "[instrument][gong]") {
    auto p = create_gong();
    const auto y = render(*p, {{int(0.01 * kFs), 48, 120}}, 2.0);
    REQUIRE(all_finite(y));
    REQUIRE(peak(y) > 0.005);
    REQUIRE(peak(y) <= 2.0);
    // Still ringing well after the strike.
    REQUIRE(rms(y, 1.0, 1.5) > 0.001);
}
