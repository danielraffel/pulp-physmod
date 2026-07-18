# PulpPhysmod — Physical-Modeling Instruments

Six instruments, each a **physical model that generates every sample from a
simulated circuit or vibrating object** — no samples, no lookup tables. Built on
the Pulp audio framework.

Each installs as an **AU (v2), VST3, and CLAP** plugin. The installer lets you
pick which instruments to install.

---

## The instruments

### VaDrum — analog bass drum
A circuit-modeled analog bass drum: a bridged-T resonator whose ringing *is* the
sound, kicked by a pulse shaper and shaped by a feedback loop. The pitch "sigh"
after the attack emerges from the circuit itself, not a scripted envelope.
- **Controls:** Tune, Decay, Tone, Level (plus pulse width, attack, and a pitch-sigh switch).
- Play it: any MIDI note triggers the drum; velocity is the accent (a hard hit is
  a different timbre, not just louder).

### PulpKit — analog drum machine (13 voices)
A 13-voice analog-modeling drum machine inspired by the TR-808 signal paths and
calibrated reference behavior. The Werner-derived kick is a bridged-T circuit
model; the remaining voices use purpose-built resonator, oscillator-cluster,
filter, noise, and envelope models. PulpKit does not yet claim a component-level
model of every original voice (the cymbal is intentionally simpler than the
published three-band/VCA/tone-stage circuit, for example).
- **Classic controls:** a dedicated editor surface follows the original
  voice-specific panel vocabulary — Bass Drum and Cymbal have Level/Tone/Decay;
  Snare has Level/Tone/Snappy; the toms have Level/Tuning; Open Hat has
  Level/Decay; the remaining voices have Level.
- **Extended controls:** every voice retains **Level, Tune, Decay, Tone**, plus
  the released shell/noise Balance on the snare. These creative extensions are
  kept separate from the classic panel.
- **Note map:** 36 kick · 37 rim · 38/40 snare · 39 clap · 41–50 low/mid/high toms ·
  42/44 closed hat · 46 open hat · 49 cymbal · 51/56 cowbell · 70/75 maracas/clave.

### ModalInstrument — mallets & strings (modal synthesis)
A polyphonic modal instrument driven by a data file describing a resonant object.
Ships tuned as a **marimba**, a **vibraphone** (longer metal sustain), and a
**stiff steel string** with real inharmonicity. Strike and pickup position are
physical controls — striking a mode's node silences it, a pickup at a node combs
it out.
- **Controls:** Level, Tune, Decay, Tone, Strike Position, Pickup Position.

### PreparedPiano — struck string with a preparation
A struck stiff string with an object — a bolt, a felt mute, or a mass — placed at
a movable point on the string. With the preparation off it's a clean piano-like
string; turn it up and the object buzzes (a genuine collision, so it gets brighter
the harder you play), mutes, or splits the modes into a detuned, metallic tone.
This is the "a sample library can't hold this" instrument: the preparation's type,
position, and strength are continuous and per-note.
- **Controls:** Level, Tune, Decay, Tone, Preparation Type, Position, Strength.

### BowedString — a self-sustaining bowed string
A digital-waveguide string driven by a stick-slip friction bow. Hold a note and
it sustains (Helmholtz motion), like a bowed cello string — it does not decay like
a pluck. Bow position changes brightness; bow force sets the onset and, below a
minimum, the string won't speak. An honest working bowed-string model.
- **Controls:** Level, Tune, Decay, Tone, Bow Force, Bow Position.

### Gong — inharmonic plate with a nonlinear bloom
A struck metal plate: a dense cloud of inharmonic modes that rings for seconds,
with a nonlinear "bloom" — struck hard, energy cascades up the spectrum over a
second or two, the way a real gong swells after the strike. Struck soft, it
doesn't bloom. The bloom is real physics keyed on how hard you hit, not a canned
brightness sweep.
- **Controls:** Level, Tune, Decay, Tone, Strike Hardness, Bloom.

---

## Installing

Open **PulpPhysmod-1.0.0.pkg** and follow the installer. Use the **Customize**
pane to choose which instruments to install. Plugins install to the standard
locations:

- AU: `/Library/Audio/Plug-Ins/Components/`
- VST3: `/Library/Audio/Plug-Ins/VST3/`
- CLAP: `/Library/Audio/Plug-Ins/CLAP/`

After installing, rescan plugins in your DAW (or restart it). In Logic/GarageBand
the instruments appear under **AU Instruments → Pulp**.

## Requirements

macOS on Apple Silicon. Any AU/VST3/CLAP host (Logic, GarageBand, MainStage,
Ableton Live, Bitwig, REAPER, …).

---

*Every instrument is a physical model — the sound is computed from the simulated
circuit or object in real time, sample by sample.*
