import sys
from pathlib import Path

import pytest
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import gen_tape_fonts as g  # noqa: E402


def tiny_font(upem=2048):
    """A font with .notdef, space and the characters 0-9 and H, each a square plus (for H) a quadratic bump."""
    names = [".notdef", "space"] + [f"uni{ord(c):04X}" for c in "0123456789H"]
    fb = FontBuilder(upem, isTTF=True)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap({32: "space", **{ord(c): f"uni{ord(c):04X}" for c in "0123456789H"}})
    glyphs = {}
    for n in names:
        pen = TTGlyphPen(None)
        if n not in (".notdef", "space"):
            top = 1400 if n != "uni0048" else 1500
            pen.moveTo((0, 0))
            pen.lineTo((0, top))
            pen.qCurveTo((500, top + 200), (1000, top))
            pen.lineTo((1000, 0))
            pen.closePath()
        glyphs[n] = pen.glyph()
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics({n: (1100 if n != "space" else 600, 0) for n in names})
    fb.setupHorizontalHeader(ascent=1600, descent=-400)
    fb.setupNameTable({"familyName": "Tiny", "styleName": "Regular"})
    fb.setupOS2()
    fb.setupPost()
    return fb.font


def test_stream_encoding_and_scaling():
    data = g.convert_font(tiny_font())
    by_cp = {cp: (adv, stream) for cp, adv, stream in data["glyphs"]}
    adv, stream = by_cp[ord("0")]
    assert adv == round(1100 * 1000 / 2048)                    # advance scaled to 1000 units per em
    assert stream[:3] == [g.OP_MOVE, 0, 0]                    # move to the origin
    assert g.OP_LINE in stream and g.OP_QUAD in stream
    assert stream[-1] == g.OP_CLOSE
    q = stream.index(g.OP_QUAD)
    assert len(stream[q:q + 5]) == 5                          # a quadratic has op + 4 coordinates
    assert max(stream[1::1]) < 32767                          # fits int16


def test_metrics_in_em_units():
    data = g.convert_font(tiny_font())
    # the bump of the quadratic (control at top + 200) peaks at top + 100
    assert data["digit_height"] == pytest.approx(1500 * 1000 / 2048, abs=1.0)
    assert data["cap_height"] == pytest.approx(1600 * 1000 / 2048, abs=1.0)


def test_missing_codepoints_are_skipped():
    data = g.convert_font(tiny_font())
    cps = {cp for cp, _a, _s in data["glyphs"]}
    assert ord("0") in cps and ord(" ") in cps
    assert ord("A") not in cps                                # not in the tiny font: skipped, no crash


def test_header_is_deterministic_and_well_formed(tmp_path):
    converted = {k: g.convert_font(tiny_font()) for k, *_ in g.FONTS}
    a = g.render_header(converted)
    b = g.render_header(converted)
    assert a == b
    assert "GENERATED" in a and "kFonts[]" in a and "Zeyada" in a and "Rock Salt" in a and "Homemade Apple" in a
    assert a.count("inline constexpr Glyph") == 3 and a.count("inline constexpr std::int16_t") == 3
    assert "SIL Open Font License 1.1" in a and "Apache License 2.0" in a


def test_header_is_plugin_independent():
    converted = {k: g.convert_font(tiny_font()) for k, *_ in g.FONTS}
    a = g.render_header(converted)
    assert "namespace FairyTapes::Ui::tape_glyphs {" in a           # the shared namespace, not a plug-in's
    assert "}  // namespace FairyTapes::Ui::tape_glyphs" in a
    assert "TheStripper" not in a and "PillowTalk" not in a
    assert "shared/ui/tools/gen_tape_fonts.py" in a and "shared/ui/assets/fonts/" in a


def test_every_printable_ascii_glyph_is_converted():
    # a font with every printable ASCII character: all of them end up in the table, in order
    names = [".notdef"] + [f"uni{cp:04X}" for cp in range(32, 127)]
    fb = FontBuilder(1000, isTTF=True)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap({cp: f"uni{cp:04X}" for cp in range(32, 127)})
    glyphs = {}
    for n in names:
        pen = TTGlyphPen(None)
        if n not in (".notdef", "uni0020"):
            pen.moveTo((0, 0))
            pen.lineTo((0, 700))
            pen.lineTo((500, 700))
            pen.closePath()
        glyphs[n] = pen.glyph()
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics({n: (600, 0) for n in names})
    fb.setupHorizontalHeader(ascent=800, descent=-200)
    fb.setupNameTable({"familyName": "Full", "styleName": "Regular"})
    fb.setupOS2()
    fb.setupPost()
    data = g.convert_font(fb.font)
    assert [cp for cp, _a, _s in data["glyphs"]] == list(range(32, 127))


def test_cubic_outlines_are_approximated():
    from fontTools.pens.recordingPen import RecordingPen  # noqa: F401  (only to be sure fontTools is complete)
    pen = g.StreamPen(None, 1.0)
    pen._moveTo((0, 0))
    pen._curveToOne((0, 10), (10, 10), (10, 0))
    pen._closePath()
    assert pen.stream[3] == g.OP_QUAD and pen.stream[-1] == g.OP_CLOSE
