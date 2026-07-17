// A smoke test per instrument: each one loads, renders real audio through its
// Processor, and is measured -- non-silent, finite, bounded, and (where the
// instrument is pitched) at the note's pitch. The deep physics tests for each
// voice live with the DSP primitives in the Pulp core; these prove the shipped
// plugins actually make sound and behave.
#include "support/instrument_probe.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

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
}  // namespace

TEST_CASE("VaDrum renders a bounded, non-silent bass drum", "[instrument][va-drum]") {
    auto p = create_va_drum();
    const auto y = render(*p, {{int(0.01 * kFs), 36, 110}}, 1.5);
    REQUIRE(all_finite(y));
    REQUIRE(peak(y) > 0.01);       // it speaks
    REQUIRE(peak(y) <= 1.5);       // and is not blowing up
    // A bass drum sits low.
    REQUIRE(estimate_f0(y, 0.1, 0.4, 30.0, 120.0) < 90.0);
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
