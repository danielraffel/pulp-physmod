# PulpPhysmod

Six physical-modeling instrument plugins built on the [Pulp](https://github.com/danielraffel/pulp)
audio framework. Every instrument computes its sound from a simulated circuit or
vibrating object in real time — **no samples, no lookup tables**. Each builds as
an **AU (v2), VST3, and CLAP** plugin.

For end-user descriptions of each instrument and its controls, see
[INSTRUMENTS.md](INSTRUMENTS.md).

## The instruments

| Plugin | What it models |
|--------|----------------|
| **VaDrum** | An analog bass drum — a bridged-T resonator circuit |
| **PulpKit** | A 13-voice analog drum machine (each voice its own circuit) |
| **ModalInstrument** | Modal mallets and strings (marimba, vibraphone, steel string) |
| **PreparedPiano** | A struck string with a movable collision preparation |
| **BowedString** | A self-sustaining waveguide + stick-slip friction bow |
| **Gong** | A dense inharmonic plate with a nonlinear energy-cascade bloom |

## Architecture

The reusable DSP lives in the **Pulp core** (`pulp::signal` — the modal bank,
the bridged-T resonator, the square-oscillator bank, the modal-spec format) and
is consumed from the installed SDK. **This repository is the instruments only.**
Each instrument is a `pulp::format::Processor` wrapped by the framework's VST3 /
AU / CLAP adapters.

## Building

You need an installed Pulp SDK (build the Pulp repo and `cmake --install` it):

```bash
# in a Pulp checkout:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(getconf _NPROCESSORS_ONLN)
cmake --install build --prefix /path/to/pulp-sdk

# in this repo:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPulp_DIR=/path/to/pulp-sdk/lib/cmake/Pulp
cmake --build build -j$(getconf _NPROCESSORS_ONLN)
```

The built bundles land in `build/{AU,CLAP,VST3}/`.

## Testing

Each instrument has a smoke test that renders it through its real `process()`
path and measures the output — non-silent, finite, bounded, and (for pitched
instruments) at the note's pitch. Bowed sustains rather than decaying; the gong
rings. The deep per-voice physics tests (inharmonicity, mode-splitting,
calibration) live with the DSP primitives in the Pulp core.

```bash
ctest --test-dir build --output-on-failure
```

## Packaging an installer

`tools/package.sh` builds every instrument and assembles a single
component-selectable macOS `.pkg` (pick which instruments to install). Signing
and notarization reuse the Pulp packaging recipe when a Developer ID and a Pulp
checkout are supplied:

```bash
# unsigned, local install:
tools/package.sh --sdk /path/to/pulp-sdk

# signed + notarized (installs on any Mac):
PULP_REPO=/path/to/pulp tools/package.sh --sdk /path/to/pulp-sdk \
  --sign-app <Developer ID Application hash> \
  --sign-installer <Developer ID Installer hash> --notarize
```

## License

MIT. See [LICENSE](LICENSE).
