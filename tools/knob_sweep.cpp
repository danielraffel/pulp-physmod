// Render the VaDrum bass drum across a fine sweep of each knob, holding the
// others at the same positions the reference sweep used, and write WAVs. A
// single measurement pass (shared with the reference renders) then computes
// per-knob-position parity, so "ours matches the reference" is a number, not an
// impression.
#include "support/instrument_probe.hpp"
#include "va-drum/va_drum.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

using namespace physmod::test;
using pulp::examples::create_va_drum;

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

// Render one hit with all four knobs set, re-applied every block.
static std::vector<float> render4(float tune, float decay, float tone, float level, double secs) {
    auto p = create_va_drum();
    pulp::state::StateStore store;
    p->set_state_store(&store);
    p->define_parameters(store);
    auto apply = [&] {
        store.set_value(0, tune); store.set_value(1, decay);
        store.set_value(2, tone); store.set_value(3, level);
    };
    apply();
    pulp::format::PrepareContext ctx; ctx.sample_rate = kFs; ctx.max_buffer_size = kBlock;
    p->prepare(ctx);
    const int total = int(secs * kFs);
    std::vector<float> out(total, 0.0f), lb(kBlock, 0.0f), rb(kBlock, 0.0f), z(kBlock, 0.0f);
    int pos = 0; bool fired = false;
    while (pos < total) {
        const int n = std::min(kBlock, total - pos);
        apply();
        float* op[2] = {lb.data(), rb.data()}; const float* ip[2] = {z.data(), z.data()};
        pulp::audio::BufferView<float> ov(op, 2, n);
        pulp::audio::BufferView<const float> iv(ip, 2, n);
        pulp::midi::MidiBuffer mi, mo;
        if (!fired) { auto e = pulp::midi::MidiEvent::note_on(0, 36, 110); e.sample_offset = 0; mi.add(e); fired = true; }
        pulp::format::ProcessContext pc; pc.sample_rate = kFs; pc.num_samples = n;
        p->process(ov, iv, mi, mo, pc);
        for (int i = 0; i < n; ++i) out[pos + i] = lb[i];
        pos += n;
    }
    return out;
}

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : "/tmp/knob-parity/ours-fine";
    // Holds match the reference sweep: Decay 50%, Tone 50%, Level ~78%, Tune 50%.
    const float HOLD_TUNE = 0.5f + 0.5f * 1.5f;  // knob 50% -> ratio 1.25? no: map below
    (void)HOLD_TUNE;
    // Tune knob% -> ratio 0.5..2.0
    auto tune_of = [](int pct) { return 0.5f + pct / 100.0f * 1.5f; };
    const float hold_tune = tune_of(50), hold_decay = 50.0f, hold_tone = 50.0f, hold_level = 78.0f;

    const int pts[] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    for (int pct : pts) {
        write_wav(out + "/TUNE_"  + std::to_string(pct) + ".wav", render4(tune_of(pct), hold_decay, hold_tone, hold_level, 3.8));
        write_wav(out + "/DECAY_" + std::to_string(pct) + ".wav", render4(hold_tune, float(pct), hold_tone, hold_level, 3.8));
        write_wav(out + "/TONE_"  + std::to_string(pct) + ".wav", render4(hold_tune, hold_decay, float(pct), hold_level, 3.8));
        write_wav(out + "/LEVEL_" + std::to_string(pct) + ".wav", render4(hold_tune, hold_decay, hold_tone, float(pct), 3.8));
    }
    std::printf("wrote ours knob sweeps to %s\n", out.c_str());
    return 0;
}
