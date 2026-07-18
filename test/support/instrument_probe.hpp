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
#include <limits>
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

struct ParamValue {
    pulp::state::ParamID id;
    float value;  ///< plain parameter-domain value, not normalized
};

/// Render `seconds` of an instrument's left channel, sending the given note-ons,
/// block by block through the Processor's real process() path.
inline std::vector<float> render(pulp::format::Processor& proc,
                                 std::vector<Note> notes, double seconds,
                                 std::vector<ParamValue> params = {}) {
    pulp::state::StateStore store;
    proc.set_state_store(&store);
    proc.define_parameters(store);
    for (const auto& param : params) store.set_value(param.id, param.value);
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

/// First-difference energy over signal energy. This robust brightness proxy
/// avoids an FFT dependency; it is a monotonic proxy, not the reference
/// corpus's spectral-centroid measurement.
inline double difference_brightness(const std::vector<float>& x) {
    double signal = 0.0;
    double difference = 0.0;
    for (std::size_t i = 1; i < x.size(); ++i) {
        signal += static_cast<double>(x[i]) * x[i];
        const double delta = static_cast<double>(x[i]) - x[i - 1];
        difference += delta * delta;
    }
    return signal > 0.0 ? std::sqrt(difference / signal) : 0.0;
}

/// T60 estimate from 5 ms RMS frames and a least-squares fit over the -5 to
/// -35 dB decay region, matching the machine-local Roland corpus analyzer.
inline double estimate_t60(const std::vector<float>& x) {
    const std::size_t frame = static_cast<std::size_t>(0.005 * kFs);
    const std::size_t count = frame > 0 ? x.size() / frame : 0;
    if (count < 8) return std::numeric_limits<double>::quiet_NaN();

    std::vector<double> envelope(count, 0.0);
    for (std::size_t block = 0; block < count; ++block) {
        double sum = 0.0;
        for (std::size_t i = 0; i < frame; ++i) {
            const double sample = x[block * frame + i];
            sum += sample * sample;
        }
        envelope[block] = std::sqrt(sum / static_cast<double>(frame) + 1e-20);
    }
    const auto peak_it = std::max_element(envelope.begin(), envelope.end());
    const std::size_t peak_index = static_cast<std::size_t>(peak_it - envelope.begin());
    const double peak_value = *peak_it;

    double sum_t = 0.0, sum_db = 0.0, sum_tt = 0.0, sum_tdb = 0.0;
    std::size_t n = 0;
    for (std::size_t i = peak_index; i < count; ++i) {
        const double db = 20.0 * std::log10(envelope[i] / std::max(peak_value, 1e-20));
        if (db > -5.0 || db < -35.0) continue;
        const double t = (static_cast<double>(i) + 0.5) * frame / kFs;
        sum_t += t;
        sum_db += db;
        sum_tt += t * t;
        sum_tdb += t * db;
        ++n;
    }
    if (n < 8) return std::numeric_limits<double>::quiet_NaN();
    const double denom = static_cast<double>(n) * sum_tt - sum_t * sum_t;
    if (std::abs(denom) < 1e-20) return std::numeric_limits<double>::quiet_NaN();
    const double slope = (static_cast<double>(n) * sum_tdb - sum_t * sum_db) / denom;
    return slope < 0.0 ? -60.0 / slope : std::numeric_limits<double>::infinity();
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

template <typename Factory>
std::unique_ptr<pulp::format::Processor> make(Factory f) {
    return f();
}

}  // namespace physmod::test
