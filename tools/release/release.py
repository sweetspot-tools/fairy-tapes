"""Fairy Tapes release helper, used by .github/workflows/release.yml (and runnable by hand).

One GitHub release per plug-in. The tag is `<plugin>-v<MAJOR.MINOR.PATCH>` (the folder name under src/, e.g.
`the-stripper-v1.2.0`), and each release carries one zip per platform named `<Bundle>-<version>-<platform>.zip`
(e.g. `TheStripper-1.2.0-windows.zip`, platform = windows | macos | linux). The website builds its download buttons from
these names, so they are checked before anything is published.

The version lives in two places that must agree with the tag: `project(<Bundle> VERSION x.y.z)` in
src/<plugin>/CMakeLists.txt and the version string in src/<plugin>/src/version.h.

Commands:
  release.py info <tag>                      key=value lines for $GITHUB_OUTPUT (slug, bundle, target, version, title)
  release.py pack <bundle.vst3> <out.zip>    zip the bundle plus the licence texts (LICENCE_FILES) from the repo root
  release.py check <file>...                 fail if a file name breaks the asset naming rule
"""

import re
import sys
import zipfile
from pathlib import Path

PLATFORMS = ("windows", "macos", "linux")
TAG_RE = re.compile(r"^([a-z0-9]+(?:-[a-z0-9]+)*)-v(\d+\.\d+\.\d+)$")
ASSET_RE = re.compile(r"^[A-Z][A-Za-z0-9]*-\d+\.\d+\.\d+-(?:windows|macos|linux)\.zip$")

# Licence texts shipped next to every bundle: path inside the zip -> path in the repo. One list for all plug-ins (the
# union of what they use, see THIRD_PARTY.md); the build submodules must be checked out.
LICENCE_FILES = {
    "LICENSE": "LICENSE",
    "THIRD_PARTY.md": "THIRD_PARTY.md",
    "LICENSES/Airwindows-MIT.txt": "LICENSES/Airwindows-MIT.txt",
    "LICENSES/libebur128-MIT.txt": "LICENSES/libebur128-MIT.txt",
    "LICENSES/VST3SDK-MIT.txt": "vendors/vst3sdk/LICENSE.txt",
    "LICENSES/VSTGUI-BSD-3-Clause.txt": "vendors/vst3sdk/vstgui4/LICENSE",
    "LICENSES/NeuralAmpModelerCore-MIT.txt": "vendors/NeuralAmpModelerCore/LICENSE",
    "LICENSES/AudioDSPTools-MIT.txt": "vendors/NeuralAmpModelerCore/Dependencies/AudioDSPTools/LICENSE",
    "LICENSES/Eigen-MPL-2.0.txt": "vendors/NeuralAmpModelerCore/Dependencies/eigen/COPYING.MPL2",
    "LICENSES/Zeyada-OFL-1.1.txt": "shared/ui/assets/fonts/LICENSE-Zeyada-OFL.txt",
    "LICENSES/RockSalt-Apache-2.0.txt": "shared/ui/assets/fonts/LICENSE-RockSalt-Apache-2.0.txt",
    "LICENSES/HomemadeApple-Apache-2.0.txt": "shared/ui/assets/fonts/LICENSE-HomemadeApple-Apache-2.0.txt",
}


class ReleaseError(Exception):
    pass


def parse_tag(tag):
    m = TAG_RE.match(tag)
    if not m:
        raise ReleaseError(f"tag '{tag}' is not <plugin>-v<MAJOR.MINOR.PATCH> (e.g. the-stripper-v1.2.0)")
    return m.group(1), m.group(2)


def asset_name(bundle, version, platform):
    if platform not in PLATFORMS:
        raise ReleaseError(f"platform '{platform}' is not one of {', '.join(PLATFORMS)}")
    name = f"{bundle}-{version}-{platform}.zip"
    check_asset_name(name)
    return name


def check_asset_name(name):
    if not ASSET_RE.match(name):
        raise ReleaseError(f"asset '{name}' is not <Bundle>-<MAJOR.MINOR.PATCH>-<windows|macos|linux>.zip")


def _cmake(root, slug):
    path = Path(root) / "src" / slug / "CMakeLists.txt"
    if not path.is_file():
        raise ReleaseError(f"no plug-in '{slug}': {path} does not exist")
    return path, path.read_text(encoding="utf-8")


def cmake_version(root, slug):
    path, text = _cmake(root, slug)
    m = re.search(r"project\(\s*\w+\s+VERSION\s+(\d+\.\d+\.\d+)", text)
    if not m:
        raise ReleaseError(f"{path}: no project(<Bundle> VERSION x.y.z)")
    return m.group(1)


def plugin_info(root, slug, version):
    path, text = _cmake(root, slug)
    found = cmake_version(root, slug)
    if found != version:
        raise ReleaseError(f"{path} says version {found}, the tag says {version}")
    m = re.search(r'set_target_properties\(\s*(\w+)\s+PROPERTIES\s+OUTPUT_NAME\s+"(\w+)"', text)
    if not m:
        raise ReleaseError(f'{path}: no set_target_properties(<target> PROPERTIES OUTPUT_NAME "<Bundle>")')
    target, bundle = m.group(1), m.group(2)
    header = Path(root) / "src" / slug / "src" / "version.h"
    if not header.is_file() or f'"{version}"' not in header.read_text(encoding="utf-8"):
        raise ReleaseError(f'{header} does not hold the version "{version}"')
    title = " ".join(w.capitalize() for w in slug.split("-"))
    return {"slug": slug, "bundle": bundle, "target": target, "version": version, "title": title}


def pack(root, bundle_dir, out_zip, licences=None):
    """Zip `bundle_dir` (kept as the top folder) plus the licence texts; file modes are kept, so executables stay so."""
    root, bundle_dir = Path(root), Path(bundle_dir)
    licences = LICENCE_FILES if licences is None else licences
    missing = [src for src in licences.values() if not (root / src).is_file()]
    if missing:
        raise ReleaseError("licence texts missing: " + ", ".join(missing))
    if not bundle_dir.is_dir():
        raise ReleaseError(f"bundle {bundle_dir} not found")
    with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(bundle_dir.rglob("*")):
            if f.is_file():
                z.write(f, Path(bundle_dir.name) / f.relative_to(bundle_dir))
        for dest, src in licences.items():
            z.write(root / src, dest)


def main(argv):
    root = Path(__file__).resolve().parent.parent.parent
    try:
        if len(argv) == 2 and argv[0] == "info":
            slug, version = parse_tag(argv[1])
            for k, v in plugin_info(root, slug, version).items():
                print(f"{k}={v}")
        elif len(argv) == 3 and argv[0] == "pack":
            check_asset_name(Path(argv[2]).name)
            pack(root, argv[1], argv[2])
        elif len(argv) >= 2 and argv[0] == "check":
            for name in argv[1:]:
                check_asset_name(Path(name).name)
        else:
            print(__doc__, file=sys.stderr)
            return 2
    except ReleaseError as e:
        print(f"release: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
