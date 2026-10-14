# Tape fonts: sources and licenses

The editor draws the handwriting of the tape (model name, tape pieces, optional values) from **vector glyph paths**
generated into `src/ui/tape_glyphs.h` by `tools/gen_tape_fonts.py`. No font file is shipped or installed: only the
outlines of the printable ASCII characters and the ellipsis are embedded.

| Font | License | Source | License text |
|---|---|---|---|
| Zeyada (Kimberly Geswein) | SIL Open Font License 1.1 | https://github.com/google/fonts/blob/main/ofl/zeyada/Zeyada.ttf | `LICENSE-Zeyada-OFL.txt` |
| Rock Salt (Font Diner / Sideshow) | Apache License 2.0 | https://github.com/google/fonts/blob/main/apache/rocksalt/RockSalt-Regular.ttf | `LICENSE-RockSalt-Apache-2.0.txt` |
| Homemade Apple (Font Diner) | Apache License 2.0 | https://github.com/google/fonts/blob/main/apache/homemadeapple/HomemadeApple-Regular.ttf | `LICENSE-HomemadeApple-Apache-2.0.txt` |

Downloaded on 2026-10-14; SHA-256 of the source files used:

```
09f23d0d78b6e166ddb8480793bb550ab4c2aaf6602eda47a394fee93d2a9667  Zeyada.ttf
c5421d1ced1949488f29d11b75b7ef967abe2d870f816246bf18fdc0f3e29c96  RockSalt-Regular.ttf
dd1baaca3cde1b1f8415aed3b0aea6808655c9d9ca5a99c7282d9accc16c1a58  HomemadeApple-Regular.ttf
```

Regenerate (fontTools needed):

```bash
python3 src/the-stripper/tools/gen_tape_fonts.py --zeyada Zeyada.ttf --rocksalt RockSalt-Regular.ttf \
  --homemade HomemadeApple-Regular.ttf --out src/the-stripper/src/ui/tape_glyphs.h
```

The license texts must be shown in the plug-in's Credits (back panel, task 13), and the copyright notices kept. Whether the
OFL and Apache 2.0 terms fit the distribution of the plug-in under GPL-3.0 is to be rechecked before release, together with
the other components.
