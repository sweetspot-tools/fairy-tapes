import sys
import zipfile
from pathlib import Path

import pytest

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import release as r  # noqa: E402

REPO = TOOLS.parent.parent


def fake_plugin(root, slug="the-stripper", bundle="TheStripper", target="the_stripper", version="1.2.0", header=None):
    """A src/<slug>/ with the two places a version lives: project(VERSION) in CMakeLists.txt and src/version.h."""
    d = root / "src" / slug
    (d / "src").mkdir(parents=True)
    (d / "CMakeLists.txt").write_text(
        f"cmake_minimum_required(VERSION 3.25)\nproject({bundle} VERSION {version} LANGUAGES CXX)\n"
        f'set_target_properties({target} PROPERTIES OUTPUT_NAME "{bundle}" PREFIX "")\n'
    )
    (d / "src" / "version.h").write_text(header if header is not None else f'#define X_VERSION "{version}"\n')
    return d


def test_parse_tag():
    assert r.parse_tag("the-stripper-v1.2.0") == ("the-stripper", "1.2.0")
    assert r.parse_tag("hotline-v10.0.3") == ("hotline", "10.0.3")
    for bad in ["the-stripper-1.2.0", "v1.2.0", "the-stripper-v1.2", "The-Stripper-v1.2.0", "the-stripper-v1.2.0-rc1"]:
        with pytest.raises(r.ReleaseError):
            r.parse_tag(bad)


def test_asset_name_and_check():
    assert r.asset_name("TheStripper", "1.2.0", "windows") == "TheStripper-1.2.0-windows.zip"
    with pytest.raises(r.ReleaseError):
        r.asset_name("TheStripper", "1.2.0", "win64")
    for good in ["TheStripper-1.2.0-windows.zip", "HappyEnding-1.0.0-macos.zip", "Hotline-2.10.0-linux.zip"]:
        r.check_asset_name(good)
    for bad in ["the-stripper-1.2.0-windows.zip", "TheStripper-1.2-linux.zip", "TheStripper-1.2.0-win.zip",
                "Pillow Talk-1.0.0-linux.zip", "TheStripper-1.2.0-linux.tar.gz"]:
        with pytest.raises(r.ReleaseError):
            r.check_asset_name(bad)


def test_plugin_info_reads_cmake(tmp_path):
    fake_plugin(tmp_path)
    info = r.plugin_info(tmp_path, "the-stripper", "1.2.0")
    assert info == {"slug": "the-stripper", "bundle": "TheStripper", "target": "the_stripper", "version": "1.2.0",
                    "title": "The Stripper"}


def test_plugin_info_rejects_mismatches(tmp_path):
    with pytest.raises(r.ReleaseError, match="no plug-in"):
        r.plugin_info(tmp_path, "nothing", "1.0.0")
    fake_plugin(tmp_path, version="1.1.0")
    with pytest.raises(r.ReleaseError, match="CMakeLists"):
        r.plugin_info(tmp_path, "the-stripper", "1.2.0")


def test_plugin_info_needs_version_h(tmp_path):
    fake_plugin(tmp_path, header='#define X_VERSION "1.0.0"\n')
    with pytest.raises(r.ReleaseError, match="version.h"):
        r.plugin_info(tmp_path, "the-stripper", "1.2.0")


def test_every_plugin_of_the_repo_resolves():
    """The real tree: every plug-in folder with a build gives a PascalCase bundle and a matching version.h."""
    slugs = [p.parent.name for p in (REPO / "src").glob("*/CMakeLists.txt")]
    assert "the-stripper" in slugs
    for slug in slugs:
        version = r.cmake_version(REPO, slug)
        info = r.plugin_info(REPO, slug, version)
        r.check_asset_name(r.asset_name(info["bundle"], version, "linux"))


def test_pack_puts_bundle_and_licences_in_the_zip(tmp_path):
    root = tmp_path / "repo"
    (root / "LICENSES").mkdir(parents=True)
    (root / "LICENSE").write_text("GPL")
    (root / "THIRD_PARTY.md").write_text("tp")
    (root / "LICENSES" / "Airwindows-MIT.txt").write_text("mit")
    bundle = tmp_path / "TheStripper.vst3"
    (bundle / "Contents" / "x86_64-linux").mkdir(parents=True)
    so = bundle / "Contents" / "x86_64-linux" / "TheStripper.so"
    so.write_text("bin")
    so.chmod(0o755)
    licences = {"LICENSE": "LICENSE", "THIRD_PARTY.md": "THIRD_PARTY.md",
                "LICENSES/Airwindows-MIT.txt": "LICENSES/Airwindows-MIT.txt"}
    out = tmp_path / "TheStripper-1.2.0-linux.zip"
    r.pack(root, bundle, out, licences)
    with zipfile.ZipFile(out) as z:
        names = set(z.namelist())
        assert "TheStripper.vst3/Contents/x86_64-linux/TheStripper.so" in names
        assert {"LICENSE", "THIRD_PARTY.md", "LICENSES/Airwindows-MIT.txt"} <= names
        mode = z.getinfo("TheStripper.vst3/Contents/x86_64-linux/TheStripper.so").external_attr >> 16
        assert mode & 0o111   # the executable bit survives (macOS and Linux bundles)


def test_pack_refuses_a_missing_licence(tmp_path):
    bundle = tmp_path / "B.vst3"
    bundle.mkdir()
    with pytest.raises(r.ReleaseError, match="missing"):
        r.pack(tmp_path, bundle, tmp_path / "B-1.0.0-linux.zip", {"LICENSE": "LICENSE"})


def test_licence_list_points_at_real_files():
    """Every licence text the release ships exists in a checkout with the build submodules."""
    if not (REPO / "vendors" / "vst3sdk" / "LICENSE.txt").exists():
        pytest.skip("build submodules not checked out")
    for src in r.LICENCE_FILES.values():
        assert (REPO / src).is_file(), src
