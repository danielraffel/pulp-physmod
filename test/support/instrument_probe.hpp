// Self-contained harness for driving any Pulp instrument Processor through its
// real process() interface and measuring the render. Depends only on the Pulp
// SDK (pulp::format / pulp::audio / pulp::midi / pulp::state) and the standard
// library -- no internal test infrastructure -- so it travels with this repo.
#pragma once

#include <pulp/audio/buffer.hpp>
#include <pulp/format/processor.hpp>
#include <pulp/midi/buffer.hpp>
#include <pulp/state/store.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace physmod::test {

inline constexpr double kFs = 48000.0;
inline constexpr int kBlock = 128;

struct Note {
    int sample;   ///< sample offset from the start of the render
    int note;     ///< MIDI note number
    int velocity; ///< 1..127
};

/// Render `seconds` of an instrument's left channel, sending the given note-ons,
/// block by block through the Processor's real process() path.
inline std::vector<float> render(pulp::format::Processor& proc,
                                 std::vector<Note> notes, double seconds) {
    pulp::state::StateStore store;
    proc.set_state_store(&store);
    proc.define_parameters(store);
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
        const int n = std::min(kBlock, total - pos);
        float* op[2] = {lbuf.data(), rbuf.data()};
        const float* ip[2] = {zero.data(), zero.data()};
        pulp::audio::BufferView<float> ov(op, 2, n);
        pulp::audio::BufferView<const float> iv(ip, 2, n);
        pulp::midi::MidiBuffer min, mout;
        while (ni < notes.size() && notes[ni].sample < pos + n) {
            auto e = pulp::midi::MidiEvent::note_on(
                0, static_cast<uint8_t>(notes[ni].note),
                static_cast<uint8_t>(notes[ni].velocity));
            e.sample_offset = notes[ni].sample - pos;
            min.add(e);
            ++ni;
        }
        pulp::format::ProcessContext pc;
        pc.sample_rate = kFs;
        pc.num_samples = n;
        proc.process(ov, iv, min, mout, pc);
        for (int i = 0; i < n; ++i)
            out[static_cast<std::size_t>(pos + i)] = lbuf[static_cast<std::size_t>(i)];
        pos += n;
    }
    return out;
}

inline double peak(const std::vector<float>& x) {
    double m = 0.0;
    for (float v : x) m = std::max(m, std::fabs(static_cast<double>(v)));
    return m;
}

inline double rms(const std::vector<float>& x, double t0, double t1) {
    const auto a = static_cast<std::size_t>(t0 * kFs);
    const auto b = std::min(static_cast<std::size_t>(t1 * kFs), x.size());
    if (a >= b) return 0.0;
    double s = 0.0;
    for (std::size_t i = a; i < b; ++i) s += static_cast<double>(x[i]) * x[i];
    return std::sqrt(s / static_cast<double>(b - a));
}

inline bool all_finite(const std::vector<float>& x) {
    return std::all_of(x.begin(), x.end(), [](float v) { return std::isfinite(v); });
}

/// Fundamental frequency over a window by autocorrelation -- self-contained, no
/// FFT. Returns 0 if no clear period is found.
inline double estimate_f0(const std::vector<float>& x, double t0, double t1,
                          double f_lo = 40.0, double f_hi = 1500.0) {
    const auto a = static_cast<std::size_t>(t0 * kFs);
    const auto b = std::min(static_cast<std::size_t>(t1 * kFs), x.size());
    if (b <= a + 4) return 0.0;
    const int lag_lo = static_cast<int>(kFs / f_hi);
    const int lag_hi = static_cast<int>(kFs / f_lo);
    double best = 0.0;
    int best_lag = 0;
    for (int lag = lag_lo; lag <= lag_hi; ++lag) {
        double s = 0.0;
        for (std::size_t i = a; i + static_cast<std::size_t>(lag) < b; ++i)
            s += static_cast<double>(x[i]) * x[i + static_cast<std::size_t>(lag)];
        if (s > best) { best = s; best_lag = lag; }
    }
    return best_lag > 0 ? kFs / best_lag : 0.0;
}

/// Spectral centroid (Hz) via a direct real DFT over a window -- self-contained.
inline double centroid(const std::vector<float>& x, double t0, double t1,
                       int bins = 512) {
    const auto a = static_cast<std::size_t>(t0 * kFs);
    const auto b = std::min(static_cast<std::size_t>(t1 * kFs), x.size());
    if (b <= a + 2) return 0.0;
    const std::size_t n = b - a;
    double num = 0.0, den = 0.0;
    for (int k = 1; k < bins; ++k) {
        const double f = k * kFs / (2.0 * bins);
        double re = 0.0, im = 0.0;
        const double w = M_PI * k / (bins * static_cast<double>(n));
        for (std::size_t i = 0; i < n; ++i) {
            const double ph = 2.0 * w * static_cast<double>(i) * n / bins;
            re += x[a + i] * std::cos(ph);
            im -= x[a + i] * std::sin(ph);
        }
        const double mag = std::sqrt(re * re + im * im);
        num += f * mag;
        den += mag;
    }
    return den > 0.0 ? num / den : 0.0;
}

template <typename Factory>
std::unique_ptr<pulp::format::Processor> make(Factory f) {
    return f();
}

}  // namespace physmod::test
