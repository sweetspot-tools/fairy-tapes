/**
 * Fairy Tapes · The Stripper — user preferences file: defaults, corrupt input, atomic write, round trip.
 * Header-only, no SDK.
 */

#include "preferences.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace FairyTapes::TheStripper;
namespace fs = std::filesystem;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

struct TempDir {
  fs::path root;
  TempDir() {
    root = fs::temp_directory_path() / ("stripper_prefs_" + std::to_string(std::random_device{}()));
    fs::create_directories(root);
  }
  ~TempDir() { std::error_code ec; fs::remove_all(root, ec); }
};

void write(const fs::path& p, const std::string& content) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << content;
}

bool isDefault(const Preferences& p) {
  const Preferences d;
  return p.wear == d.wear && p.notes == d.notes && p.meterHoldSeconds == d.meterHoldSeconds &&
         p.meterReleaseDbPerSec == d.meterReleaseDbPerSec && p.tapeFont == d.tapeFont && p.valuesFont == d.valuesFont &&
         p.modelFolders.empty() && !p.modelFoldersConfigured;
}

void test_defaults_match_the_spec() {
  const Preferences d;
  CHECK(d.wear == WearLevel::Light);
  CHECK(d.notes == NotesLevel::Show);
  CHECK(d.tapeFont == TapeFont::Zeyada);
  CHECK(d.valuesFont == ValuesFont::Tape);   // decided 2026-10-10
  CHECK(d.meterHoldSeconds == 1.5);
  CHECK(d.meterReleaseDbPerSec == 20.0);
  CHECK(!d.modelFoldersConfigured && d.modelFolders.empty());
}

void test_missing_file_gives_defaults() {
  TempDir t;
  CHECK(isDefault(loadPreferences(t.root / "nope" / "x.prefs")));
  CHECK(isDefault(loadPreferences("")));
}

void test_corrupt_files_give_defaults_without_crashing() {
  TempDir t;
  std::string garbage;
  for (int i = 0; i < 4096; ++i) garbage.push_back(static_cast<char>((i * 131 + 7) & 0xFF));
  write(t.root / "garbage.prefs", garbage);
  CHECK(isDefault(loadPreferences(t.root / "garbage.prefs")));
  write(t.root / "empty.prefs", "");
  CHECK(isDefault(loadPreferences(t.root / "empty.prefs")));
  write(t.root / "text.prefs", "this is not a preferences file\n=\n====\n\n#\nwear\n");
  CHECK(isDefault(loadPreferences(t.root / "text.prefs")));
  // a directory where the file should be
  fs::create_directories(t.root / "dir.prefs");
  CHECK(isDefault(loadPreferences(t.root / "dir.prefs")));
  // far too large: refused, not read
  write(t.root / "huge.prefs", std::string(2 * 1024 * 1024, 'a'));
  CHECK(isDefault(loadPreferences(t.root / "huge.prefs")));
}

void test_invalid_values_fall_back_per_key() {
  const Preferences p = parsePreferences("wear=Heavy\nnotes=Maybe\ntapeFont=Comic\nvaluesFont=Standard\nmeterHoldSeconds=abc\nmeterReleaseDbPerSec=nan\n");
  CHECK(p.wear == WearLevel::Heavy);              // the valid ones survive
  CHECK(p.valuesFont == ValuesFont::Standard);
  CHECK(p.notes == NotesLevel::Show);             // the invalid ones take their default
  CHECK(p.tapeFont == TapeFont::Zeyada);
  CHECK(p.meterHoldSeconds == 1.5);
  CHECK(p.meterReleaseDbPerSec == 20.0);
}

void test_back_tab() {
  CHECK(Preferences().backTab == BackTab::Settings);                       // default: SETTINGS
  CHECK(parsePreferences("backTab=Credits\n").backTab == BackTab::Credits);
  CHECK(parsePreferences("backTab=Buttons\n").backTab == BackTab::Buttons);
  CHECK(parsePreferences("backTab=Nonsense\n").backTab == BackTab::Settings);
  Preferences in; in.backTab = BackTab::Buttons;
  CHECK(parsePreferences(serializePreferences(in)).backTab == BackTab::Buttons);   // round trip
}

void test_numbers_are_clamped() {
  Preferences p = parsePreferences("meterHoldSeconds=9999\nmeterReleaseDbPerSec=0.0001\n");
  CHECK(p.meterHoldSeconds == kMaxMeterHoldSeconds);
  CHECK(p.meterReleaseDbPerSec == kMinMeterReleaseDbPerSec);
  p = parsePreferences("meterHoldSeconds=-3\nmeterReleaseDbPerSec=99999\n");
  CHECK(p.meterHoldSeconds == 0.0);
  CHECK(p.meterReleaseDbPerSec == kMaxMeterReleaseDbPerSec);
}

void test_unknown_keys_and_comments_are_ignored() {
  const Preferences p = parsePreferences("# comment\nfutureKey=1\n\n  wear = Off  \r\nrandom\n");
  CHECK(p.wear == WearLevel::Off);   // spaces and CRLF tolerated
}

void test_round_trip() {
  TempDir t;
  Preferences in;
  in.wear = WearLevel::Heavy;
  in.notes = NotesLevel::Hide;
  in.tapeFont = TapeFont::RockSalt;
  in.valuesFont = ValuesFont::Standard;
  in.meterHoldSeconds = 3.25;
  in.meterReleaseDbPerSec = 40.5;
  in.modelFoldersConfigured = true;
  in.modelFolders = {"/home/utente/Modelli/Chitarra \xC3\xA8 \xE2\x82\xAC", "C:\\Users\\x\\NAM", "/tmp/a b/c"};
  const fs::path file = t.root / "sub" / "dir" / "the-stripper.prefs";   // parent folders are created
  CHECK(savePreferences(file, in));
  const Preferences out = loadPreferences(file);
  CHECK(out.wear == in.wear && out.notes == in.notes && out.tapeFont == in.tapeFont && out.valuesFont == in.valuesFont);
  CHECK(out.meterHoldSeconds == in.meterHoldSeconds && out.meterReleaseDbPerSec == in.meterReleaseDbPerSec);
  CHECK(out.modelFoldersConfigured && out.modelFolders == in.modelFolders);
}

void test_configured_but_empty_folder_list_is_remembered() {
  TempDir t;
  Preferences in; in.modelFoldersConfigured = true;   // the user removed every folder
  CHECK(savePreferences(t.root / "p.prefs", in));
  const Preferences out = loadPreferences(t.root / "p.prefs");
  CHECK(out.modelFoldersConfigured && out.modelFolders.empty());   // not confused with "never configured"
}

void test_save_is_atomic_and_leaves_no_temp_files() {
  TempDir t;
  const fs::path file = t.root / "p.prefs";
  write(file, "wear=Off\n");
  Preferences in; in.wear = WearLevel::Heavy;
  CHECK(savePreferences(file, in));
  CHECK(loadPreferences(file).wear == WearLevel::Heavy);   // replaced, not appended
  int files = 0;
  for (const auto& e : fs::directory_iterator(t.root)) { (void)e; ++files; }
  CHECK(files == 1);   // no temporary file left behind
}

void test_save_fails_cleanly_when_the_folder_is_not_writable() {
  TempDir t;
  write(t.root / "blocker", "x");   // a file where a folder is needed
  Preferences in;
  CHECK(!savePreferences(t.root / "blocker" / "sub" / "p.prefs", in));   // reports failure, no crash
  CHECK(!savePreferences("", in));
}

void test_hostile_folder_entries() {
  // newlines in a path would break the line format: such entries are dropped when saving
  TempDir t;
  Preferences in; in.modelFoldersConfigured = true;
  in.modelFolders = {"/ok/one", "/bad\npath", "/ok/two", std::string("/nul\0byte", 9), ""};
  CHECK(savePreferences(t.root / "p.prefs", in));
  const Preferences out = loadPreferences(t.root / "p.prefs");
  CHECK(out.modelFolders.size() == 2 && out.modelFolders[0] == "/ok/one" && out.modelFolders[1] == "/ok/two");
  // and the list is capped
  std::string many = "modelFolders=set\n";
  for (int i = 0; i < 1000; ++i) many += "modelFolder=/f" + std::to_string(i) + "\n";
  CHECK(parsePreferences(many).modelFolders.size() == kMaxModelFolders);
}

void test_default_path_is_per_platform_and_per_plugin() {
  const fs::path p = preferencesPath("TheStripper", "/home/u", "/home/u/.config");
  CHECK(p.filename() == "TheStripper.prefs");
  CHECK(p.parent_path().filename() == "FairyTapes");
}
} // namespace

int main() {
  test_defaults_match_the_spec();
  test_missing_file_gives_defaults();
  test_corrupt_files_give_defaults_without_crashing();
  test_invalid_values_fall_back_per_key();
  test_back_tab();
  test_numbers_are_clamped();
  test_unknown_keys_and_comments_are_ignored();
  test_round_trip();
  test_configured_but_empty_folder_list_is_remembered();
  test_save_is_atomic_and_leaves_no_temp_files();
  test_save_fails_cleanly_when_the_folder_is_not_writable();
  test_hostile_folder_entries();
  test_default_path_is_per_platform_and_per_plugin();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("test_preferences: ok");
  return 0;
}
