#!/usr/bin/env python3
"""Generate the seamless 256x256 grain tile of The Stripper's panel (spec 4, Sfondo).

White pixels whose alpha comes from lit noise (the same idea as the 'tfine' SVG filter of the preview
docs/mockups/stripper_rack.html): periodic fractal noise is read as a height map, lit from the top left, and only
the highlights are kept. The result is nearly invisible (max alpha ~7 %), deterministic for a given seed.

Dependencies: numpy, Pillow.
Usage: gen_panel_grain.py [--out FILE] [--seed N]
"""
import argparse
import math

import numpy as np
from PIL import Image

SIZE = 256
OCTAVES = 3
BASE_SIGMA = 0.6          # px, blur of the first octave (about one grain per pixel)
SURFACE_SCALE = 1.3       # as the preview filter
AZIMUTH, ELEVATION = 235.0, 58.0
THRESHOLD = 0.75          # diffuse value below which nothing is drawn
GAIN = 0.28               # alpha per unit of diffuse above the threshold -> max alpha 0.07


def periodic_noise(rng, sigma):
    """White noise blurred with a periodic gaussian (done in the Fourier domain, so it tiles), unit variance."""
    white = rng.standard_normal((SIZE, SIZE))
    f = np.fft.fftfreq(SIZE)
    fx, fy = np.meshgrid(f, f)
    kernel = np.exp(-2.0 * (math.pi * sigma) ** 2 * (fx ** 2 + fy ** 2))
    out = np.real(np.fft.ifft2(np.fft.fft2(white) * kernel))
    return out / out.std()


def height_map(seed):
    rng = np.random.default_rng(seed)
    h = np.zeros((SIZE, SIZE))
    for octave in range(OCTAVES):
        h += periodic_noise(rng, BASE_SIGMA * 2 ** octave) * 0.5 ** octave
    return h


def alpha_map(seed):
    h = height_map(seed) * 0.35   # normalised so that the slopes are about those of the SVG filter
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) / 2.0
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) / 2.0
    nx, ny, nz = -SURFACE_SCALE * dx, -SURFACE_SCALE * dy, np.ones_like(h)
    norm = np.sqrt(nx ** 2 + ny ** 2 + nz ** 2)
    az, el = math.radians(AZIMUTH), math.radians(ELEVATION)
    # light direction as in SVG feDistantLight (y axis points down)
    lx, ly, lz = math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el)
    diffuse = np.clip((nx * lx + ny * ly + nz * lz) / norm, 0.0, 1.0)
    return np.clip((diffuse - THRESHOLD) * GAIN, 0.0, 1.0)


def make_tile(seed):
    """RGBA uint8 array (SIZE x SIZE x 4): white with the grain alpha."""
    alpha = np.rint(alpha_map(seed) * 255.0).astype(np.uint8)
    tile = np.full((SIZE, SIZE, 4), 255, dtype=np.uint8)
    tile[..., 3] = alpha
    return tile


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default="panel_grain.png")
    ap.add_argument("--seed", type=int, default=4)
    args = ap.parse_args()
    Image.fromarray(make_tile(args.seed), "RGBA").save(args.out, optimize=False)


if __name__ == "__main__":
    main()
