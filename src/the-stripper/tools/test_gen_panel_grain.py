import hashlib
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import gen_panel_grain as g  # noqa: E402


def run(tmp_path, name, seed):
    out = tmp_path / name
    subprocess.run([sys.executable, str(TOOLS / "gen_panel_grain.py"), "--out", str(out), "--seed", str(seed)], check=True)
    return out


def test_size_and_mode(tmp_path):
    im = Image.open(run(tmp_path, "a.png", 4))
    assert im.size == (256, 256) and im.mode == "RGBA"


def test_same_seed_same_file(tmp_path):
    a = hashlib.sha256(run(tmp_path, "a.png", 4).read_bytes()).hexdigest()
    b = hashlib.sha256(run(tmp_path, "b.png", 4).read_bytes()).hexdigest()
    assert a == b


def test_different_seed_different_file(tmp_path):
    a = run(tmp_path, "a.png", 4).read_bytes()
    b = run(tmp_path, "b.png", 5).read_bytes()
    assert a != b


def test_nearly_invisible():
    tile = g.make_tile(4)
    alpha = tile[..., 3] / 255.0
    assert alpha.max() <= 0.10
    assert alpha.mean() <= 0.04
    assert alpha.max() > 0.03          # not empty: there are visible grains
    assert (tile[..., :3] == 255).all()  # white pixels, the alpha carries the grain


def test_seamless():
    a = g.make_tile(4)[..., 3].astype(float)
    inner_cols = np.abs(a[:, 1:] - a[:, :-1]).mean()
    inner_rows = np.abs(a[1:, :] - a[:-1, :]).mean()
    wrap_cols = np.abs(a[:, 0] - a[:, -1]).mean()
    wrap_rows = np.abs(a[0, :] - a[-1, :]).mean()
    assert wrap_cols <= inner_cols * 1.15
    assert wrap_rows <= inner_rows * 1.15
