/**
 * Fairy Tapes · Hotline — tests for the front panel decor (pure, no VSTGUI): paper tapes and corner screws from the
 * instance seed (ui/decor.h), the screws' stored rotations (ui/screw_angles.h), the writing on the tapes (ui/tapes.h),
 * the panel and control wear seeds, and the user preferences file (ui/prefs.h). The wear marks themselves come from
 * shared/ui (fairy_ui/wear.h) and are tested there. Build target: hotline_decor_tests.
 */
#include "ui/decor.h"
#include "ui/prefs.h"
#include "ui/screw_angles.h"
#include "ui/tapes.h"

#include "fairy_ui/back_panel.h"
#include "fairy_ui/screws.h"
#include "fairy_ui/tape_paper.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>
static unsigned long long processId() { return static_cast<unsigned long long>(_getpid()); }
#else
#include <unistd.h>
static unsigned long long processId() { return static_cast<unsigned long long>(::getpid()); }
#endif

using namespace FairyTapes::Hotline;
using namespace FairyTapes::Hotline::Ui;
using FairyTapes::Ui::TapeFontId;
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
#define RUN_TEST(fn) do { const int before_ = g_failed; fn(); if (g_failed == before_) ++g_passed; } while(0)

const Rect kPanel{0, 0, static_cast<float>(kEditorWidth), static_cast<float>(kEditorHeight)};

// ---- Tapes and screws ---------------------------------------------------------------------------------------------
bool tapeInside(const TapePlacement& t, const Rect& p) {
  const float r = t.rotDeg * 3.14159265f / 180.f, c = std::cos(r), s = std::sin(r);
  for (int i = 0; i < 4; ++i) {
    const float lx = (i & 1 ? 0.5f : -0.5f) * t.w, ly = (i & 2 ? 0.5f : -0.5f) * t.h;
    const float x = t.cx + lx * c - ly * s, y = t.cy + lx * s + ly * c;
    if (x < p.x || x > p.x + p.w || y < p.y || y > p.y + p.h) return false;
  }
  return true;
}

void layoutInsidePanelAndOneScrewMissingAtMost() {
  const Rect panels[] = {kPanel, {0, 0, 2000, 940}, {10, 20, 500, 235}};
  int sawMissing = 0;
  for (uint32_t seed = 0; seed < 3000; ++seed)
    for (WearLevel lv : {WearLevel::Off, WearLevel::Light, WearLevel::Heavy})
      for (const Rect& p : panels) {
        const WearLayout l = layoutFromSeed(seed, lv, p);
        ASSERT_TRUE(l.screws.size() == 4);
        int missing = 0;
        for (const auto& s : l.screws) {
          missing += s.missing;
          ASSERT_TRUE(s.x >= p.x && s.x <= p.x + p.w && s.y >= p.y && s.y <= p.y + p.h);
        }
        ASSERT_TRUE(missing <= 1);
        sawMissing += missing;
        for (const auto& t : l.tapes) ASSERT_TRUE(tapeInside(t, p));
        const size_t n = l.tapes.size();
        if (lv == WearLevel::Off) ASSERT_TRUE(n == 0);
        if (lv == WearLevel::Light) ASSERT_TRUE(n >= 1 && n <= 2);   // the preview: n(1) + a coin toss
        if (lv == WearLevel::Heavy) ASSERT_TRUE(n >= 2 && n <= 3);
      }
  ASSERT_TRUE(sawMissing > 0);   // the rare missing screw does occur
}

void layoutDeterministicAndSeedDependent() {
  const WearLayout a = layoutFromSeed(5u, WearLevel::Heavy, kPanel), b = layoutFromSeed(5u, WearLevel::Heavy, kPanel);
  ASSERT_TRUE(a.tapes.size() == b.tapes.size());
  for (size_t i = 0; i < a.tapes.size(); ++i)
    ASSERT_TRUE(a.tapes[i].cx == b.tapes[i].cx && a.tapes[i].cy == b.tapes[i].cy && a.tapes[i].rotDeg == b.tapes[i].rotDeg &&
                a.tapes[i].slot == b.tapes[i].slot);
  bool differs = false;
  for (uint32_t s = 6; s < 30 && !differs; ++s) {
    const WearLayout c = layoutFromSeed(s, WearLevel::Heavy, kPanel);
    differs = c.tapes.size() != a.tapes.size() || c.tapes[0].cx != a.tapes[0].cx || c.screws[0].angleDeg != a.screws[0].angleDeg;
  }
  ASSERT_TRUE(differs);
}

void screwsIndependentOfWearLevelAndShared() {
  for (uint32_t seed : {1u, 3u, 7u, 555u, 31337u, 4203449416u}) {
    const auto shared = FairyTapes::Ui::generateScrews(seed);
    const WearLayout a = layoutFromSeed(seed, WearLevel::Off, kPanel), b = layoutFromSeed(seed, WearLevel::Heavy, kPanel);
    for (int i = 0; i < 4; ++i) {
      ASSERT_TRUE(a.screws[i].angleDeg == b.screws[i].angleDeg && a.screws[i].missing == b.screws[i].missing &&
                  a.screws[i].cross == b.screws[i].cross);
      ASSERT_TRUE(a.screws[i].cross == shared[i].cross && a.screws[i].missing == shared[i].missing &&
                  a.screws[i].angleDeg == static_cast<float>(shared[i].initialAngle));
      ASSERT_TRUE(a.screws[i].x == kScrewPos[i][0] && a.screws[i].y == kScrewPos[i][1]);
    }
  }
}

// The corners of the 1000 x 470 panel, 18 px in (docs/DESIGN_SYSTEM.md; the preview still has the 628 template's 610).
void screwsSitInTheCornersOfThePanel() {
  ASSERT_TRUE(kScrewPos[0][0] == 18.f && kScrewPos[0][1] == 18.f);
  ASSERT_TRUE(kScrewPos[1][0] == kEditorWidth - 18.f && kScrewPos[1][1] == 18.f);
  ASSERT_TRUE(kScrewPos[2][0] == 18.f && kScrewPos[2][1] == kEditorHeight - 18.f);
  ASSERT_TRUE(kScrewPos[3][0] == kEditorWidth - 18.f && kScrewPos[3][1] == kEditorHeight - 18.f);
}

// What a tape must never cover: every control view, the LEVEL rows, the clip LEDs, the corner screws and "BACK ->".
std::vector<Rect> tapeObstacles() {
  std::vector<Rect> out;
  for (const ControlSpec& c : kControls) out.push_back(viewRect(c));
  for (const MeterRow& r : kMeterRows) out.push_back(meterBarRect(r));
  for (const ClipSpec& c : kClips) out.push_back(clipRect(c));
  for (const auto& s : kScrewPos) {
    const float r = static_cast<float>(FairyTapes::Ui::kScrewHitRadius);
    out.push_back(Rect{s[0] - r, s[1] - r, 2 * r, 2 * r});
  }
  const FairyTapes::Ui::BackBox link = FairyTapes::Ui::frontBackLinkBox();
  out.push_back(Rect{static_cast<float>(link.x), static_cast<float>(link.y), static_cast<float>(link.w), static_cast<float>(link.h)});
  return out;
}
bool overlaps(const FairyTapes::Ui::TapeBox& b, const Rect& r) {
  return b.left < r.x + r.w && b.right > r.x && b.top < r.y + r.h && b.bottom > r.y;
}
// The drawn tape: rotated rectangle plus its ragged ends and shadow (1 px more for safety).
FairyTapes::Ui::TapeBox drawnBox(const TapePlacement& t) {
  return FairyTapes::Ui::tapeBounds(t.cx, t.cy, t.w, t.h, t.rotDeg, FairyTapes::Ui::kTapeRaggedX + 1.f);
}

void tapesNeverCoverControlsOrEachOther() {
  const std::vector<Rect> obstacles = tapeObstacles();
  for (uint32_t seed = 0; seed < 5000; ++seed)
    for (WearLevel lv : {WearLevel::Light, WearLevel::Heavy}) {
      const WearLayout l = layoutFromSeed(seed, lv, kPanel);
      for (size_t i = 0; i < l.tapes.size(); ++i) {
        const auto b = drawnBox(l.tapes[i]);
        for (const Rect& r : obstacles) ASSERT_TRUE(!overlaps(b, r));
        for (size_t j = i + 1; j < l.tapes.size(); ++j) {
          const auto o = drawnBox(l.tapes[j]);
          ASSERT_TRUE(!overlaps(b, Rect{static_cast<float>(o.left), static_cast<float>(o.top),
                                        static_cast<float>(o.right - o.left), static_cast<float>(o.bottom - o.top)}));
        }
      }
    }
}

// Each tape names its slot (the index of its stored text): valid, distinct, and the same tape keeps its slot and place
// when the wear goes from Light to Heavy (Light is a prefix of Heavy).
void tapeSlotsAreDistinctAndStable() {
  ASSERT_TRUE(kNumTapeSlots == kMaxTapeTexts);
  for (uint32_t seed = 0; seed < 2000; ++seed) {
    const WearLayout light = layoutFromSeed(seed, WearLevel::Light, kPanel), heavy = layoutFromSeed(seed, WearLevel::Heavy, kPanel);
    std::set<int> seen;
    for (const auto& t : heavy.tapes) {
      ASSERT_TRUE(t.slot >= 0 && t.slot < kNumTapeSlots);
      ASSERT_TRUE(seen.insert(t.slot).second);
    }
    ASSERT_TRUE(light.tapes.size() <= heavy.tapes.size());
    for (size_t i = 0; i < light.tapes.size(); ++i)
      ASSERT_TRUE(light.tapes[i].slot == heavy.tapes[i].slot && light.tapes[i].cx == heavy.tapes[i].cx);
  }
}

// Every slot is used by some seed (no dead slot).
void everyTapeSlotIsReachable() {
  std::set<int> used;
  for (uint32_t seed = 0; seed < 2000; ++seed)
    for (const auto& t : layoutFromSeed(seed, WearLevel::Heavy, kPanel).tapes) used.insert(t.slot);
  ASSERT_TRUE(static_cast<int>(used.size()) == kNumTapeSlots);
}

// ---- Wear seeds ----------------------------------------------------------------------------------------------------
void panelWearIsDeterministicAndFitsThePanel() {
  ASSERT_TRUE(panelWear(1234u, WearLevel::Off).empty());
  const auto a = panelWear(1234u, WearLevel::Light), b = panelWear(1234u, WearLevel::Light);
  ASSERT_TRUE(!a.empty() && a.size() == b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    ASSERT_TRUE(a[i].kind == b[i].kind && a[i].pts.size() == b[i].pts.size() && a[i].rgb == b[i].rgb);
    for (size_t k = 0; k < a[i].pts.size(); ++k) ASSERT_TRUE(a[i].pts[k].x == b[i].pts[k].x && a[i].pts[k].y == b[i].pts[k].y);
  }
  ASSERT_TRUE(panelWear(1234u, WearLevel::Heavy).size() > a.size());
  const auto c = panelWear(1235u, WearLevel::Light);
  ASSERT_TRUE(c.size() != a.size() || c[0].pts[0].x != a[0].pts[0].x);
  // scaled to the 470 px panel (the shared generator is written for 628): the specks' centres stay on it
  for (uint32_t seed = 0; seed < 200; ++seed)
    for (const auto& m : panelWear(seed, WearLevel::Heavy))
      if (m.kind == FairyTapes::Ui::WearMark::Kind::Speck)
        ASSERT_TRUE(m.pts[0].y >= 0 && m.pts[0].y <= kEditorHeight && m.pts[0].x >= 0 && m.pts[0].x <= kEditorWidth);
}

void controlWearSeedsDifferPerControl() {
  std::set<uint32_t> seen;
  for (const ControlSpec& c : kControls) ASSERT_TRUE(seen.insert(controlWearSeed(99u, c.id)).second);
  ASSERT_TRUE(controlWearSeed(99u, kDrive) == controlWearSeed(99u, kDrive));
  ASSERT_TRUE(controlWearSeed(99u, kDrive) != controlWearSeed(100u, kDrive));
  // the preview's ctrlSeed: seed ^ (index * 2654435761)
  ASSERT_TRUE(controlWearSeed(99u, kTone) == (99u ^ (static_cast<uint32_t>(kTone) * 2654435761u)));
}

// ---- Screw rotations in the instance decor --------------------------------------------------------------------------
bool near(double a, double b) { return std::fabs(a - b) < 1e-4; }

void shownAngleIsInitialPlusUserRotation() {
  ASSERT_TRUE(near(shownScrewAngle(30.f, 0.f), 30));
  ASSERT_TRUE(near(shownScrewAngle(30.f, 45.5f), 75.5));
  ASSERT_TRUE(near(shownScrewAngle(170.f, 300.f), 110));
  ASSERT_TRUE(near(shownScrewAngle(30.f, std::numeric_limits<float>::quiet_NaN()), 30));
  ASSERT_TRUE(near(userScrewAngle(75.5, 30.f), 45.5));
  ASSERT_TRUE(near(userScrewAngle(10, 30.f), 340));
  for (float initial : {0.f, 8.f, 91.f, 180.f})
    for (double shown : {0.0, 0.25, 33.3, 179.9, 359.75}) {
      const float user = userScrewAngle(shown, initial);
      ASSERT_TRUE(user >= 0.f && user < 360.f);
      const double back = shownScrewAngle(initial, user);
      ASSERT_TRUE(near(back, shown) || near(std::fabs(back - shown), 360));
    }
}

void withScrewAngleChangesOnlyThatScrew() {
  Decor d;
  d.seed = 77;
  d.screwAngles[1] = 12.f;
  d.tapeTexts = {"VOX"};
  const Decor e = withScrewAngle(d, 2, 400.f);
  ASSERT_TRUE(e.seed == 77 && e.tapeTexts == d.tapeTexts);
  ASSERT_TRUE(e.screwAngles[0] == 0.f && e.screwAngles[1] == 12.f && e.screwAngles[2] == 40.f && e.screwAngles[3] == 0.f);
  ASSERT_TRUE(withScrewAngle(d, -1, 5.f).screwAngles[0] == 0.f);
  ASSERT_TRUE(withScrewAngle(d, kNumScrews, 5.f).screwAngles[3] == 0.f);
  ASSERT_TRUE(withScrewAngle(d, 0, std::nanf("")).screwAngles[0] == 0.f);
}

void screwSpecsForTheViews() {
  for (uint32_t seed : {1u, 3u, 7u, 555u, 123456789u}) {
    const auto shared = FairyTapes::Ui::generateScrews(seed);
    const auto specs = screwSpecs(seed);
    for (int i = 0; i < kNumScrews; ++i) {
      ASSERT_TRUE(specs[i].x == kScrewPos[i][0] && specs[i].y == kScrewPos[i][1]);
      ASSERT_TRUE(specs[i].cross == shared[i].cross && specs[i].missing == shared[i].missing &&
                  specs[i].initialAngle == shared[i].initialAngle);
    }
  }
}

// ---- Writing on the tapes -------------------------------------------------------------------------------------------
void tapeTextsPerSlot() {
  ASSERT_TRUE(tapeFontIdOf(TapeFont::Zeyada) == TapeFontId::Zeyada);
  ASSERT_TRUE(tapeFontIdOf(TapeFont::RockSalt) == TapeFontId::RockSalt);
  ASSERT_TRUE(tapeFontIdOf(TapeFont::HomemadeApple) == TapeFontId::HomemadeApple);
  Decor d;
  d.seed = 77;
  ASSERT_TRUE(tapeTextFor(d, 0).empty());
  Decor e = withTapeText(d, 4, "  TAKE 3  ");
  ASSERT_TRUE(e.seed == 77 && e.tapeTexts.size() == 5 && e.tapeTexts[4] == "TAKE 3" && tapeTextFor(e, 4) == "TAKE 3");
  e = withTapeText(e, 4, "a very long text indeed");
  ASSERT_TRUE(e.tapeTexts[4] == "a very long te");
  ASSERT_TRUE(withTapeText(e, kNumTapeSlots, "x").tapeTexts == e.tapeTexts);
  ASSERT_TRUE(withTapeText(e, -1, "x").tapeTexts == e.tapeTexts);
  ASSERT_TRUE(tapeTextFor(e, -1).empty() && tapeTextFor(e, 99).empty());
}

void tapeInkStaysOnTheTape() {
  for (TapeFontId f : {TapeFontId::Zeyada, TapeFontId::RockSalt, TapeFontId::HomemadeApple})
    for (const char* t : {"VOX", "WWWWWWWWWWWWWW", "7/12 keep!"})
      for (double w : {58.0, 96.0})
        for (uint32_t seed = 0; seed < 20; ++seed) {
          const TapeLook look = tapeLook(seed, static_cast<int>(seed % kNumTapeSlots));
          ASSERT_TRUE(look.textSize >= 11 && look.textSize <= 14 && std::fabs(look.textRotDeg) <= 3);
          const TapeInk ink = tapeInk(f, t, w, look);
          ASSERT_TRUE(ink.emPx > 0);
          double lo = 0, hi = 0;
          for (const auto& p : FairyTapes::Ui::layoutText(f, ink.text, ink.emPx, look.inkSeed, 1.0)) {
            lo = std::min(lo, p.x);
            hi = std::max(hi, p.x + p.glyph->advance / 1000.0 * ink.emPx * p.scale);
          }
          ASSERT_TRUE(hi - lo <= w - 2 * kTapeTextEdge);
        }
  ASSERT_TRUE(tapeInk(TapeFontId::Zeyada, "", 96, tapeLook(1, 0)).text.empty());
}

// ---- User preferences -----------------------------------------------------------------------------------------------
// Unique per process and per run: parallel runs and worktrees must not share the folder.
std::string uniqueSuffix() {
  return std::to_string(processId()) + "_" +
         std::to_string(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count())) + "_" +
         std::to_string(static_cast<unsigned long long>(std::random_device{}()));
}

fs::path tempDir() {
  static fs::path dir;
  if (dir.empty()) {
    std::error_code ec;
    dir = fs::temp_directory_path(ec) / ("hotline_prefs_test_" + uniqueSuffix());
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

void prefsDefaults() {
  const Prefs p;
  ASSERT_TRUE(p.wear == WearLevel::Light);   // the preview's default
  ASSERT_TRUE(p.notes && p.tapeFont == TapeFont::Zeyada && p.handwrittenValues);
}

void prefsMissingEmptyOrCorruptFileGivesDefaults() {
  ASSERT_TRUE(isDefault(loadPrefs(tempDir() / "does_not_exist.prefs")));
  ASSERT_TRUE(isDefault(loadPrefs(fs::path())));
  ASSERT_TRUE(isDefault(loadPrefs(tempDir())));   // a folder, not a file
  const fs::path f = tempDir() / "bad.prefs";
  writeFile(f, "");
  ASSERT_TRUE(isDefault(loadPrefs(f)));
  writeFile(f, "{\"wear\": \"Heavy\"}\n=Heavy\nwear\n\x01\x02garbage=\n");
  ASSERT_TRUE(isDefault(loadPrefs(f)));
  writeFile(f, std::string("wear=Off\n\0\0\0binary", 18));   // binary content: the whole file is ignored
  ASSERT_TRUE(isDefault(loadPrefs(f)));
  writeFile(f, std::string(300 * 1024, 'x'));                 // oversized
  ASSERT_TRUE(isDefault(loadPrefs(f)));
}

void prefsBadValuesFallBackOneByOne() {
  const Prefs p = parsePrefs("wear=Rusty\nnotes=Hide\ntapeFont=Comic\nvaluesFont=Standard\n");
  ASSERT_TRUE(p.wear == WearLevel::Light && !p.notes && p.tapeFont == TapeFont::Zeyada && !p.handwrittenValues);
  const Prefs q = parsePrefs("# only the wear\n  wear = Heavy  \r\nunknownKey=1\n");
  ASSERT_TRUE(q.wear == WearLevel::Heavy && q.notes && q.tapeFont == TapeFont::Zeyada && q.handwrittenValues);
  ASSERT_TRUE(parsePrefs("wear=Off\n").wear == WearLevel::Off);
}

void prefsRoundTrip() {
  const fs::path f = tempDir() / "sub" / "dir" / "Hotline.prefs";   // folders are created
  for (WearLevel w : {WearLevel::Off, WearLevel::Light, WearLevel::Heavy})
    for (TapeFont t : {TapeFont::Zeyada, TapeFont::RockSalt, TapeFont::HomemadeApple})
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
  std::ifstream in(f);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  ASSERT_TRUE(text.find("Hotline") != std::string::npos && text.find("wear=") != std::string::npos);
}

void prefsSaveFailsOnUnwritablePath() {
  ASSERT_TRUE(!savePrefs(fs::path(), Prefs{}));
  const fs::path blocker = tempDir() / "blocker";
  writeFile(blocker, "a file, not a folder");
  ASSERT_TRUE(!savePrefs(blocker / "Hotline.prefs", Prefs{}));   // parent is a file
  const fs::path dirAsFile = tempDir() / "is_a_dir";
  std::error_code ec;
  fs::create_directories(dirAsFile, ec);
  ASSERT_TRUE(!savePrefs(dirAsFile, Prefs{}));                    // target is a folder
}

void prefsOwnFilePerOs() {
#if defined(_WIN32)
  ASSERT_TRUE(prefsPathFor("C:/Users/u", "", "C:/Users/u/AppData/Roaming") ==
              fs::path("C:/Users/u/AppData/Roaming") / "FairyTapes" / "Hotline.prefs");
#elif defined(__APPLE__)
  ASSERT_TRUE(prefsPathFor("/Users/u", "", "") == fs::path("/Users/u/Library/Application Support/FairyTapes/Hotline.prefs"));
#else
  ASSERT_TRUE(prefsPathFor("/home/u", "", "") == fs::path("/home/u/.config/FairyTapes/Hotline.prefs"));
  ASSERT_TRUE(prefsPathFor("/home/u", "/xdg", "") == fs::path("/xdg/FairyTapes/Hotline.prefs"));
  ASSERT_TRUE(prefsPathFor("/home/u", "relative/xdg", "") == fs::path("/home/u/.config/FairyTapes/Hotline.prefs"));
#endif
  ASSERT_TRUE(prefsPathFor("", "", "").empty());
  // Own file, never another plug-in's (PillowTalk.prefs, the Stripper's).
  ASSERT_TRUE(prefsPath().empty() || prefsPath().filename() == "Hotline.prefs");
}

}  // namespace

int main() {
  RUN_TEST(layoutInsidePanelAndOneScrewMissingAtMost);
  RUN_TEST(layoutDeterministicAndSeedDependent);
  RUN_TEST(screwsIndependentOfWearLevelAndShared);
  RUN_TEST(screwsSitInTheCornersOfThePanel);
  RUN_TEST(tapesNeverCoverControlsOrEachOther);
  RUN_TEST(tapeSlotsAreDistinctAndStable);
  RUN_TEST(everyTapeSlotIsReachable);
  RUN_TEST(panelWearIsDeterministicAndFitsThePanel);
  RUN_TEST(controlWearSeedsDifferPerControl);
  RUN_TEST(shownAngleIsInitialPlusUserRotation);
  RUN_TEST(withScrewAngleChangesOnlyThatScrew);
  RUN_TEST(screwSpecsForTheViews);
  RUN_TEST(tapeTextsPerSlot);
  RUN_TEST(tapeInkStaysOnTheTape);
  RUN_TEST(prefsDefaults);
  RUN_TEST(prefsMissingEmptyOrCorruptFileGivesDefaults);
  RUN_TEST(prefsBadValuesFallBackOneByOne);
  RUN_TEST(prefsRoundTrip);
  RUN_TEST(prefsSaveFailsOnUnwritablePath);
  RUN_TEST(prefsOwnFilePerOs);
  std::error_code ec;
  fs::remove_all(tempDir(), ec);
  std::printf("hotline_decor_tests: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
