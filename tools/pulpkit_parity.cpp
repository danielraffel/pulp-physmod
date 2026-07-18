// Print sample-free PulpKit bass-drum control metrics at the five normalized
// positions used by the licensed-reference bench sweep. This renders the
// Processor directly: it does not load an installed PulpKit plugin.
#include "support/instrument_probe.hpp"
#include "pulp-kit/pulp_kit.hpp"

#include <array>
#include <cstdio>
#include <memory>

using namespace physmod::test;
using namespace pulp::examples;

namespace {
constexpr std::array<int, 5> kPositions{0, 25, 50, 75, 100};

float plain_value(pulp::state::ParamID id, int position) {
    if (id == kPulpKitKickTune)
        return 0.5f + 1.5f * static_cast<float>(position) / 100.0f;
    return static_cast<float>(position);
}

std::vector<float> render_position(pulp::state::ParamID id, int position) {
    auto kit = create_pulp_kit();
    return render(*kit, {{0, 36, 100}}, 2.5,
                  {{id, plain_value(id, position)}});
}
}  // namespace

int main() {
    std::puts("control,position,metric,value");
    for (const int position : kPositions) {
        const auto y = render_position(kPulpKitKickTone, position);
        std::printf("tone,%d,brightness,%.12g\n", position,
                    difference_brightness(y));
    }
    for (const int position : kPositions) {
        const auto y = render_position(kPulpKitKickDecay, position);
        std::printf("decay,%d,t60_seconds,%.12g\n", position, estimate_t60(y));
    }
    for (const int position : kPositions) {
        const auto y = render_position(kPulpKitKickTune, position);
        std::printf("tune,%d,f0_hz,%.12g\n", position,
                    estimate_f0(y, 0.08, 0.35, 15.0, 120.0));
    }
    for (const int position : kPositions) {
        const auto y = render_position(kPulpKitKickLevel, position);
        std::printf("level,%d,rms,%.12g\n", position, rms(y, 0.0, 2.5));
    }
    return 0;
}
