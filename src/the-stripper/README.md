# Fairy Tapes · The Stripper

VST3 **bus-oriented** channel strip: **Channel** = 4K-EQ + ZLCompressor (+ gain staging, not implemented); **Mix bus** = Pultec (not implemented) + Logical4 (SSL bus compressor clone). LP/HP, **Gain/Drive** = **NAM with GPU extension** (TapeHack2 when NAM off), limiter (BlockParty), gate (Denoise). Real, analog-style tone; Tone3000 integration planned.

**Design & decisions:** [docs/OVERVIEW.md](docs/OVERVIEW.md) (one-page summary) · [docs/FEATURES.md](docs/FEATURES.md) (full requirements).

## Build

Usa l’SDK Steinberg **nella root del progetto** (una volta per tutta la suite), non dentro questa cartella. Dalla root **fairy-tapes**:

```bash
mkdir -p vendors && git clone https://github.com/steinbergmedia/vst3sdk vendors/vst3sdk
```

Then from **fairy-tapes**:

```bash
mkdir build && cd build
cmake .. -DVST3_SDK_PATH=$(pwd)/../vendors/vst3sdk
cmake --build .
```

Or from **src/the-stripper** with an absolute SDK path (from repo root):

```bash
cd src/the-stripper
mkdir build && cd build
cmake .. -DVST3_SDK_PATH=/absolute/path/to/fairy-tapes/vendors/vst3sdk
cmake --build .
```

Output: `TheStripper.vst3` (bundle). Copy it into your host’s VST3 folder to use.

## Status

- [x] Minimal VST3 skeleton (pass-through)
- [x] Parameters and state (strip mode, bypass per stage, gain staging trim, compress amount/mix)
- [x] Processing chain: gain staging (Channel only, not implemented), ZLCompressor (Channel), Logical4 (Mix bus), 4K-EQ (Channel), stubs for Pultec (Mix bus EQ), Drive, Limiter, Gate
- [x] Custom VSTGUI editor (1000 × 628, drawn by the code): layout, IN buttons, meters, NAM model picker; still to do: wear layers, tape fonts, back panel, screws (see `docs/superpowers/plans/`)
- [ ] Gain staging stage (Channel): RMS + trim
- [ ] Pultec (Mix bus EQ)
- [ ] NAM with GPU extension (Drive); TapeHack2 when NAM off; BlockParty, Denoise
- [x] Custom strip UI (first version, see above)
