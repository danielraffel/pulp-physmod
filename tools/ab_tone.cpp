// A/B: render each instrument with the Tone knob low (10%) vs the default (50%),
// write both to WAV, and measure, to see where a very dark tone setting struggles.
#include "support/instrument_probe.hpp"

#include "va-drum/va_drum.hpp"
#include "pulp-kit/pulp_kit.hpp"
#include "modal-instrument/modal_instrument.hpp"
#include "prepared-piano/prepared_piano.hpp"
#include "bowed-string/bowed_string_instrument.hpp"
#include "gong/gong_instrument.hpp"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

using namespace physmod::test;

static void write_wav(const std::string& path, const std::vector<float>& x) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    uint32_t n = x.size(), sr = uint32_t(kFs), br = sr * 4, dsz = n * 4, rsz = 36 + dsz, s16 = 16;
    uint16_t fmt = 3, ch = 1, ba = 4, bps = 32;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&rsz, 4, 1, f); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); std::fwrite(&s16, 4, 1, f); std::fwrite(&fmt, 2, 1, f);
    std::fwrite(&ch, 2, 1, f); std::fwrite(&sr, 4, 1, f); std::fwrite(&br, 4, 1, f);
    std::fwrite(&ba, 2, 1, f); std::fwrite(&bps, 2, 1, f);
    std::fwrite("data", 1, 4, f); std::fwrite(&dsz, 4, 1, f);
    std::fwrite(x.data(), 4, n, f); std::fclose(f);
}

// Render with one parameter overridden before processing.
static std::vector<float> render_param(pulp::format::Processor& proc,
                                       pulp::state::ParamID tone_id, float tone,
                                       std::vector<Note> notes, double seconds) {
    pulp::state::StateStore store;
    proc.set_state_store(&store);
    proc.define_parameters(store);
    store.set_value(tone_id, tone);
    pulp::format::PrepareContext ctx;
    ctx.sample_rate = kFs;
    ctx.max_buffer_size = kBlock;
    proc.prepare(ctx);

    const int total = static_cast<int>(seconds * kFs);
    std::vector<float> out(static_cast<std::size_t>(total), 0.0f);
    std::vector<float> lbuf(kBlock, 0.0f), rbuf(kBlock, 0.0f), zero(kBlock, 0.0f);
    int pos = 0;
    std::size_t ni = 0;
    while (pos < total) {
        const int nn = std::min(kBlock, total - pos);
        // Re-apply Tone every block, as the real Processor reads it per block.
        store.set_value(tone_id, tone);
        float* op[2] = {lbuf.data(), rbuf.data()};
        const float* ip[2] = {zero.data(), zero.data()};
        pulp::audio::BufferView<float> ov(op, 2, nn);
        pulp::audio::BufferView<const float> iv(ip, 2, nn);
        pulp::midi::MidiBuffer min, mout;
        while (ni < notes.size() && notes[ni].sample < pos + nn) {
            auto e = pulp::midi::MidiEvent::note_on(0, uint8_t(notes[ni].note), uint8_t(notes[ni].velocity));
            e.sample_offset = notes[ni].sample - pos;
            min.add(e); ++ni;
        }
        pulp::format::ProcessContext pc; pc.sample_rate = kFs; pc.num_samples = nn;
        proc.process(ov, iv, min, mout, pc);
        for (int i = 0; i < nn; ++i) out[pos + i] = lbuf[i];
        pos += nn;
    }
    return out;
}

struct Case {
    const char* name;
    std::function<std::unique_ptr<pulp::format::Processor>()> make;
    pulp::state::ParamID tone_id;
    float tone_lo, tone_hi;  // "knob at ~10%" and default, in each param's own units
    int note, vel;
    double seconds;
};

// Brightness proxy: RMS of the first difference over RMS of the signal. A
// first difference is a crude high-pass, so more high-frequency content raises
// it. Monotonic and robust -- unlike a hand-rolled DFT centroid.
static double brightness(const std::vector<float>& x) {
    double sig = 0.0, diff = 0.0;
    for (std::size_t i = 1; i < x.size(); ++i) {
        sig += double(x[i]) * x[i];
        const double d = double(x[i]) - x[i - 1];
        diff += d * d;
    }
    return sig > 0.0 ? std::sqrt(diff / sig) : 0.0;
}

int main(int argc, char** argv) {
    using namespace pulp::examples;
    const std::string out = argc > 1 ? argv[1] : "/tmp/physmod-tone-ab";
    // Only instruments with a Tone control (the ModalInstrument uses strike/
    // pickup position instead, so it has no Tone knob to sweep).
    // tone_lo = knob at ~10% of range; tone_hi = the default. Gong's Tone is a
    // 0..1 control; the rest are 0..100.
    const std::vector<Case> cases = {
        {"VaDrum",        create_va_drum,        kVaDrumTone,      10.0f, 50.0f, 36, 110, 1.5},
        {"PulpKit-kick",  create_pulp_kit,       kPulpKitKickTone, 10.0f, 50.0f, 36, 120, 1.5},
        {"PreparedPiano", create_prepared_piano, kPrepTone,        10.0f, 60.0f, 60, 110, 1.5},
        {"BowedString",   create_bowed_string,   kBowTone,         10.0f, 55.0f, 57, 100, 2.0},
        {"Gong",          create_gong,           kGongTone,         0.1f,  0.5f, 48, 120, 2.0},
    };
    std::printf("%-16s %8s %8s %10s %10s\n", "instrument", "tone", "peak", "rms", "bright");
    for (const auto& c : cases) {
        for (float tone : {c.tone_lo, c.tone_hi}) {
            auto p = c.make();
            auto y = render_param(*p, c.tone_id, tone, {{int(0.01 * kFs), c.note, c.vel}}, c.seconds);
            const double pk = peak(y), rm = rms(y, 0.05, c.seconds - 0.1), br = brightness(y);
            const char* tag = (tone == c.tone_lo) ? "lo(~10%)" : "default";
            std::printf("%-16s %8s %8.4f %10.5f %10.4f\n", c.name, tag, pk, rm, br);
            write_wav(out + "/" + c.name + "_tone_" + tag + ".wav", y);
        }
    }
    std::printf("\nWAVs written to %s/\n", out.c_str());
    return 0;
}
