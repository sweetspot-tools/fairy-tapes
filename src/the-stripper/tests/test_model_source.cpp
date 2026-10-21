/**
 * Fairy Tapes · The Stripper — local model source: folder scan, filtering, name display (header-only, no VSTGUI).
 */

#include "ui/model_source.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace FairyTapes::TheStripper::Ui;
namespace fs = std::filesystem;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

void touch(const fs::path& p, const std::string& content = "x") {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << content;
}

struct TempDir {
  fs::path root;
  TempDir() {
    root = fs::temp_directory_path() / ("stripper_models_" + std::to_string(std::random_device{}()));
    fs::create_directories(root);
  }
  ~TempDir() { std::error_code ec; fs::remove_all(root, ec); }
};

void test_scan_finds_nam_files_recursively() {
  TempDir t;
  touch(t.root / "a" / "Plexi.nam", "1234");
  touch(t.root / "a" / "deep" / "er" / "Twin.NAM");
  touch(t.root / "a" / "notes.txt");
  touch(t.root / "a" / "model.nam.bak");
  touch(t.root / "b" / "Fuzz.nam");
  const auto found = scanFolders({(t.root / "a").string(), (t.root / "b").string()});
  CHECK(found.size() == 3);
  bool plexi = false, twin = false, fuzz = false;
  for (const auto& e : found) {
    if (e.name == "Plexi.nam") { plexi = true; CHECK(e.bytes == 4); CHECK(e.folder == (t.root / "a").string()); }
    if (e.name == "Twin.NAM") twin = true;
    if (e.name == "Fuzz.nam") fuzz = true;
    CHECK(fs::path(e.path).is_absolute());
  }
  CHECK(plexi && twin && fuzz);
}

void test_scan_lets_a_hook_decorate_each_model() {
  TempDir t;
  touch(t.root / "Plexi.nam", "1234");
  touch(t.root / "Twin.nam");
  touch(t.root / "Twin.t3k.json", "{}");                         // the file kept next to a model downloaded from TONE3000
  std::vector<std::string> seen;
  const auto found = scanFolders({t.root.string()}, 5000, 8, [&](ModelEntry& e) {
    seen.push_back(e.name);
    if (fs::exists(fs::path(e.path).replace_extension(".t3k.json"))) e.badge = "CC BY";
  });
  CHECK(found.size() == 2 && seen.size() == 2);
  for (const auto& e : found) CHECK(e.badge == (e.name == "Twin.nam" ? "CC BY" : ""));
  // the row text of the picker: the licence first when there is one
  CHECK(modelRowMeta("", 2048) == "2 KB" && modelRowMeta("CC BY-NC", 2048) == "CC BY-NC \xC2\xB7 2 KB");
  CHECK(modelRowMeta("", 3 * 1024 * 1024) == "3 MB");
  // no hook: the same entries, no badge
  for (const auto& e : scanFolders({t.root.string()})) CHECK(e.badge.empty());
}

void test_scan_is_sorted_by_name_ignoring_case() {
  TempDir t;
  touch(t.root / "zeta.nam");
  touch(t.root / "Alpha.nam");
  touch(t.root / "beta.nam");
  const auto found = scanFolders({t.root.string()});
  CHECK(found.size() == 3);
  CHECK(found[0].name == "Alpha.nam" && found[1].name == "beta.nam" && found[2].name == "zeta.nam");
}

void test_scan_tolerates_missing_and_duplicate_folders() {
  TempDir t;
  touch(t.root / "x.nam");
  const auto found = scanFolders({t.root.string(), t.root.string(), (t.root / "nope").string(), ""});
  CHECK(found.size() == 1);   // the same file is not listed twice
}

void test_scan_respects_limits() {
  TempDir t;
  for (int i = 0; i < 20; ++i) touch(t.root / ("m" + std::to_string(i) + ".nam"));
  CHECK(scanFolders({t.root.string()}, /*maxEntries*/ 5).size() == 5);
  touch(t.root / "l1" / "l2" / "l3" / "deep.nam");
  CHECK(scanFolders({t.root.string()}, 100, /*maxDepth*/ 1).size() == 20);   // depth 1 = root plus one level
}

void test_scan_does_not_follow_directory_symlinks() {
  TempDir t;
  touch(t.root / "real" / "a.nam");
  std::error_code ec;
  fs::create_directory_symlink(t.root, t.root / "real" / "loop", ec);
  if (ec) return;   // platform without symlinks
  const auto found = scanFolders({t.root.string()});
  CHECK(found.size() == 1);
}

void test_filter() {
  std::vector<ModelEntry> all = {{"JCM_clean.nam", "/m/JCM_clean.nam", "/m", 1}, {"Plexi_crunch.nam", "/m/Plexi_crunch.nam", "/m", 1},
                                 {"TS_drive.nam", "/p/TS_drive.nam", "/p", 1}};
  CHECK(filterModels(all, "").size() == 3);
  CHECK(filterModels(all, "plexi").size() == 1);   // case-insensitive
  CHECK(filterModels(all, "  PLEXI ").size() == 1);   // trimmed
  CHECK(filterModels(all, "zzz").empty());
  CHECK(filterModels(all, "", "/p").size() == 1);   // folder restriction
  CHECK(filterModels(all, "drive", "/m").empty());
}

void test_display_name() {
  CHECK(displayName("JCM_clean_v2.nam") == "JCM_clean_v2");
  CHECK(displayName("Twin.NAM") == "Twin");
  CHECK(displayName("/home/u/Amps/Plexi crunch.nam") == "Plexi crunch");
  CHECK(displayName("noextension") == "noextension");
  CHECK(displayName("") == "");
  CHECK(displayName(".nam") == ".nam");   // a name must remain
}

// Fake metrics: every character is 10 px wide, the ellipsis counts as one character.
void test_truncate_by_measured_width() {
  auto measure = [](const std::string& s) { return 10.0 * static_cast<double>(utf8Length(s)); };
  CHECK(truncateToWidth("abc", 100, measure) == "abc");
  CHECK(truncateToWidth("abcdefghij", 100, measure) == "abcdefghij");   // exactly fits
  CHECK(truncateToWidth("abcdefghijk", 100, measure) == "abcdefghi\xE2\x80\xA6");   // 9 chars + ellipsis
  CHECK(truncateToWidth("abc def_____", 70, measure) == "abc de\xE2\x80\xA6");
  CHECK(truncateToWidth("abc _____x", 50, measure) == "abc\xE2\x80\xA6");   // trailing separators dropped before the ellipsis
  CHECK(truncateToWidth("abc", 5, measure) == "\xE2\x80\xA6" || truncateToWidth("abc", 5, measure).empty());   // nothing fits: never loops
  CHECK(truncateToWidth("", 100, measure).empty());
  // multi-byte characters are not cut in half
  const std::string out = truncateToWidth("\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8", 30, measure);
  CHECK(out == "\xC3\xA8\xC3\xA8\xE2\x80\xA6");
}

void test_strip_tooltip() {
  // NAM on: the full file name (extension and all) once a model is loaded, the error text when it failed
  CHECK(stripTooltip(2, "JCM clean v2.nam", "", false) == "JCM clean v2.nam");
  CHECK(stripTooltip(3, "", "File not found", false) == "File not found");
  CHECK(stripTooltip(1, "", "", false) == "Loading the model");
  CHECK(stripTooltip(0, "", "", false) == "Click to choose a NAM model");
  CHECK(stripTooltip(2, "", "", false) == "Click to choose a NAM model");   // loaded but nameless: nothing to show
  CHECK(stripTooltip(3, "", "", false) == "Could not load the model");
  // NAM off: the strip says which engine works, whatever the model state
  CHECK(stripTooltip(2, "JCM clean v2.nam", "", true) == "4K saturation drives the stage (the NAM is off)");
  CHECK(stripTooltip(3, "", "File not found", true) == "4K saturation drives the stage (the NAM is off)");
}
} // namespace

int main() {
  test_scan_finds_nam_files_recursively();
  test_scan_lets_a_hook_decorate_each_model();
  test_scan_is_sorted_by_name_ignoring_case();
  test_scan_tolerates_missing_and_duplicate_folders();
  test_scan_respects_limits();
  test_scan_does_not_follow_directory_symlinks();
  test_filter();
  test_display_name();
  test_truncate_by_measured_width();
  test_strip_tooltip();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("test_model_source: ok");
  return 0;
}
