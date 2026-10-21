/**
 * Fairy Tapes · Pillow Talk — tests for the user preferences file (ui/prefs.h): defaults on a missing, empty or
 * corrupt file, per-field fallback, round trip, failed writes, the per-OS location. Uses a temporary folder only.
 * Build target: pillow_talk_prefs_tests.
 */
#include "ui/prefs.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

using namespace FairyTapes::PillowTalk::Ui;
using FairyTapes::Ui::WearLevel;
namespace fs = std::filesystem;

namespace {
int g_failed = 0, g_passed = 0;

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
    ++g_failed; return; \
  } \
} while(0)
#define RUN_TEST(fn) do { const int before = g_failed; fn(); if (g_failed == before) ++g_passed; } while(0)

fs::path tempDir() {
  static fs::path dir;
  if (dir.empty()) {
    std::error_code ec;
    dir = fs::temp_directory_path(ec) / ("pillow_talk_prefs_test_" + std::to_string(std::rand()));
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
  }
  return dir;
}

void writeFile(const fs::path& p, const std::string& text) { std::ofstream(p, std::ios::binary) << text; }

bool isDefault(const Prefs& p) {
  const Prefs d;
  return p.wear == d.wear && p.notes == d.notes && p.tapeFont == d.tapeFont && p.handwrittenValues == d.handwrittenValues;
}

void test_defaults() {
  const Prefs p;
  ASSERT_TRUE(p.wear == WearLevel::Light);
  ASSERT_TRUE(p.notes);
  ASSERT_TRUE(p.tapeFont == TapeFont::Zeyada);
  ASSERT_TRUE(p.handwrittenValues);
}

void test_missing_file_gives_defaults() {
  ASSERT_TRUE(isDefault(loadPrefs(tempDir() / "does_not_exist.prefs")));
  ASSERT_TRUE(isDefault(loadPrefs(fs::path())));
  ASSERT_TRUE(isDefault(loadPrefs(tempDir())));   // a folder, not a file
}

void test_empty_file_gives_defaults() {
  const fs::path f = tempDir() / "empty.prefs";
  writeFile(f, "");
  ASSERT_TRUE(isDefault(loadPrefs(f)));
}

void test_corrupt_file_gives_defaults() {
  const fs::path f = tempDir() / "corrupt.prefs";
  writeFile(f, "{\"wear\": \"Heavy\"}\n=Heavy\nwear\n\x01\x02garbage=\n");
  ASSERT_TRUE(isDefault(loadPrefs(f)));
  writeFile(f, std::string("wear=Off\n\0\0\0binary", 18));   // binary content: the whole file is ignored
  ASSERT_TRUE(isDefault(loadPrefs(f)));
  writeFile(f, std::string(300 * 1024, 'x'));                 // oversized
  ASSERT_TRUE(isDefault(loadPrefs(f)));
}

void test_bad_values_fall_back_one_by_one() {
  const Prefs p = parsePrefs("wear=Rusty\nnotes=Hide\ntapeFont=Comic\nvaluesFont=Standard\n");
  ASSERT_TRUE(p.wear == WearLevel::Light);       // invalid: default
  ASSERT_TRUE(!p.notes);                         // valid
  ASSERT_TRUE(p.tapeFont == TapeFont::Zeyada);   // invalid: default
  ASSERT_TRUE(!p.handwrittenValues);             // valid
}

void test_missing_fields_keep_defaults() {
  const Prefs p = parsePrefs("# only the wear\n  wear = Heavy  \r\nunknownKey=1\n");
  ASSERT_TRUE(p.wear == WearLevel::Heavy);
  ASSERT_TRUE(p.notes && p.tapeFont == TapeFont::Zeyada && p.handwrittenValues);
}

void test_round_trip() {
  const fs::path f = tempDir() / "sub" / "dir" / "PillowTalk.prefs";   // folders are created
  const WearLevel wears[] = {WearLevel::Off, WearLevel::Light, WearLevel::Heavy};
  const TapeFont fonts[] = {TapeFont::Zeyada, TapeFont::RockSalt, TapeFont::HomemadeApple};
  for (WearLevel w : wears)
    for (TapeFont t : fonts)
      for (int b = 0; b < 4; ++b) {
        Prefs p;
        p.wear = w;
        p.tapeFont = t;
        p.notes = (b & 1) != 0;
        p.handwrittenValues = (b & 2) != 0;
        ASSERT_TRUE(savePrefs(f, p));
        const Prefs q = loadPrefs(f);
        ASSERT_TRUE(q.wear == w && q.tapeFont == t && q.notes == p.notes && q.handwrittenValues == p.handwrittenValues);
      }
  std::error_code ec;
  ASSERT_TRUE(!fs::exists(fs::path(f) += ".tmp", ec));   // no temporary file left behind
}

void test_save_fails_on_unwritable_path() {
  ASSERT_TRUE(!savePrefs(fs::path(), Prefs{}));
  const fs::path blocker = tempDir() / "blocker";
  writeFile(blocker, "a file, not a folder");
  ASSERT_TRUE(!savePrefs(blocker / "PillowTalk.prefs", Prefs{}));   // parent is a file
  const fs::path dirAsFile = tempDir() / "is_a_dir";
  std::error_code ec;
  fs::create_directories(dirAsFile, ec);
  ASSERT_TRUE(!savePrefs(dirAsFile, Prefs{}));                       // target is a folder
}

void test_path_per_os() {
#if defined(_WIN32)
  ASSERT_TRUE(prefsPathFor("C:/Users/u", "", "C:/Users/u/AppData/Roaming") ==
              fs::path("C:/Users/u/AppData/Roaming") / "FairyTapes" / "PillowTalk.prefs");
  ASSERT_TRUE(prefsPathFor("", "", "").empty());
#elif defined(__APPLE__)
  ASSERT_TRUE(prefsPathFor("/Users/u", "", "") ==
              fs::path("/Users/u/Library/Application Support/FairyTapes/PillowTalk.prefs"));
  ASSERT_TRUE(prefsPathFor("", "", "").empty());
#else
  ASSERT_TRUE(prefsPathFor("/home/u", "", "") == fs::path("/home/u/.config/FairyTapes/PillowTalk.prefs"));
  ASSERT_TRUE(prefsPathFor("/home/u", "/xdg", "") == fs::path("/xdg/FairyTapes/PillowTalk.prefs"));
  ASSERT_TRUE(prefsPathFor("/home/u", "relative/xdg", "") == fs::path("/home/u/.config/FairyTapes/PillowTalk.prefs"));
  ASSERT_TRUE(prefsPathFor("", "", "").empty());   // no home: no file (defaults, writes fail)
#endif
  // Own file, never The Stripper's.
  ASSERT_TRUE(prefsPath().empty() || prefsPath().filename() == "PillowTalk.prefs");
}

} // namespace

int main() {
  RUN_TEST(test_defaults);
  RUN_TEST(test_missing_file_gives_defaults);
  RUN_TEST(test_empty_file_gives_defaults);
  RUN_TEST(test_corrupt_file_gives_defaults);
  RUN_TEST(test_bad_values_fall_back_one_by_one);
  RUN_TEST(test_missing_fields_keep_defaults);
  RUN_TEST(test_round_trip);
  RUN_TEST(test_save_fails_on_unwritable_path);
  RUN_TEST(test_path_per_os);
  std::error_code ec;
  fs::remove_all(tempDir(), ec);
  std::printf("pillow_talk_prefs_tests: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
