# Fairy Tapes · Doggystyle *(working title)*

Aggressive parallel compression for drum groups and for bass and guitar groups in rock, metal and grindcore. Two
branches with opposite characters run side by side and are blended with the dry signal: dense and loud, without losing
the hits.

**Status:** designed, not implemented. There is no code in this folder yet.

## The idea

- **Slap:** fast, brutal FET-style compression, up to "all buttons in" (about 20:1). It flattens the group and pushes
  the transients out.
- **Thump:** heavy VCA compression with a slow attack. It lets the kick and snare hit through, then adds body and
  density with tube saturation.
- A few controls: how much Slap, how much Thump, the balance between them and the mix with the original signal.
- An optional NAM stage at the end for saturation, shared with The Stripper (`shared/nam_drive`).
- Parallel only: no serial mode.

## Documents

- Design: [docs/superpowers/specs/2026-10-09-doggystyle-design.md](../../docs/superpowers/specs/2026-10-09-doggystyle-design.md) (in Italian)
- Plan: [docs/superpowers/plans/2026-10-09-doggystyle.md](../../docs/superpowers/plans/2026-10-09-doggystyle.md) (in Italian)
- First concept notes: [docs/CONCEPT.md](docs/CONCEPT.md). The design above replaces them where they differ.
