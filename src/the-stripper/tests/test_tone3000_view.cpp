/** Fairy Tapes · The Stripper — the texts of the TONE3000 tab (no VSTGUI). */

#include "ui/tone3000_view.h"

#include <cstdio>

using namespace FairyTapes::TheStripper::Ui;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

Tone tone() {
  Tone t;
  t.id = "42"; t.title = "Fender Twin"; t.license = "cc-by-nc"; t.format = "nam";
  t.author.username = "jane"; t.author.displayName = "Jane Doe"; t.author.verified = true;
  return t;
}

void test_rows_carry_author_and_licence() {
  CHECK(toneRowText(tone()) == "Fender Twin" && toneRowText(Tone{}) == "Untitled tone");
  CHECK(toneRowMeta(tone()) == "Jane Doe \xC2\xB7 CC BY-NC");
  Tone anon = tone(); anon.author = {};
  CHECK(toneRowMeta(anon) == "unknown author \xC2\xB7 CC BY-NC");
  Tone nolic = tone(); nolic.license.clear();
  CHECK(toneRowMeta(nolic) == "Jane Doe \xC2\xB7 ?");                       // an unknown licence is shown as unknown, never hidden
  Model m; m.id = "100"; m.name = "Twin standard"; m.size = "standard"; m.architecture = "A2";
  CHECK(modelRowText(m) == "Twin standard" && modelRowMeta(m) == "STANDARD \xC2\xB7 A2");
  Model bare; bare.id = "7";
  CHECK(modelRowText(bare) == "Model 7" && modelRowMeta(bare) == "? \xC2\xB7 ?");
}

void test_the_credit_names_author_and_licence() {
  CHECK(authorText(tone()) == "Jane Doe (verified)");
  Tone t = tone(); t.author.verified = false; t.license = "cc-by";
  CHECK(authorText(t) == "Jane Doe" && badgeText(t) == "CC BY");
  CHECK(creditLine(t, authorText(t)) == "by Jane Doe \xC2\xB7 CC BY");
  CHECK(creditLine(t, "Jane D\xE2\x80\xA6") == "by Jane D\xE2\x80\xA6 \xC2\xB7 CC BY");     // a cut author keeps the licence
  Tone anon = tone(); anon.author = {};
  CHECK(authorText(anon) == "unknown author");
  Tone nolic = tone(); nolic.license.clear();
  CHECK(badgeText(nolic) == "?");
  CHECK(licenseNote("cc-by-nc").find("Non-commercial") == 0 && licenseNote("cc-by-nc-sa").size() > 0);
  CHECK(licenseNote("cc-by").empty() && licenseNote("t3k").empty() && licenseNote("").empty());
}

void test_list_labels() {
  CHECK(std::string(listKindLabel(ListKind::Favorited)) == "FAVORITES" && std::string(listKindLabel(ListKind::Latest)) == "LATEST");
  CHECK(listKindIsPaged(ListKind::Favorited) && listKindIsPaged(ListKind::Downloaded) && listKindIsPaged(ListKind::Created));
  CHECK(!listKindIsPaged(ListKind::Trending) && !listKindIsPaged(ListKind::Latest));
}

void test_byte_text() {
  CHECK(byteText(0) == "0 KB" && byteText(1) == "1 KB" && byteText(1024) == "1 KB" && byteText(1025) == "2 KB");
  CHECK(byteText(1024 * 1024) == "1.0 MB" && byteText(1024 * 1024 * 3 / 2) == "1.5 MB" && byteText(50ull * 1024 * 1024) == "50.0 MB");
}

void test_when_a_model_can_be_downloaded() {
  ClientState s;
  s.phase = ClientState::Phase::SignedIn;
  s.tone = tone();
  Model a, b; a.id = "1"; b.id = "2";
  s.models = {a, b};
  CHECK(canDownload(s, 0) && canDownload(s, 1));
  CHECK(!canDownload(s, -1) && !canDownload(s, 2));
  ClientState busy = s; busy.busy = true;
  CHECK(!canDownload(busy, 0));
  ClientState out = s; out.phase = ClientState::Phase::SignedOut;
  CHECK(!canDownload(out, 0));
  ClientState none = s; none.tone.reset();
  CHECK(!canDownload(none, 0));
  ClientState ir = s; ir.tone->format = "ir";                      // impulse responses cannot be loaded as a NAM model
  CHECK(!canDownload(ir, 0));
  ClientState unknown = s; unknown.tone->format.clear();
  CHECK(canDownload(unknown, 0));
}

void test_status_text() {
  ClientState s;
  CHECK(statusText(s).empty());
  s.busy = true; s.busyText = "Loading the models...";
  CHECK(statusText(s) == "Loading the models...");
  s.busyText.clear();
  CHECK(statusText(s) == "Working...");
  s.busy = false; s.download = ClientState::DownloadPhase::Running; s.downloadBytes = 2 * 1024 * 1024;
  CHECK(statusText(s) == "Downloading... 2.0 MB");
  s.download = ClientState::DownloadPhase::Failed; s.downloadMessage = "The model file is too big";
  CHECK(statusText(s) == "The model file is too big");
  s.download = ClientState::DownloadPhase::None; s.message = "Sign in to TONE3000 again";
  CHECK(statusText(s) == "Sign in to TONE3000 again");
  s.message.clear(); s.download = ClientState::DownloadPhase::Done;
  CHECK(statusText(s) == "Saved. Loading the model...");
}
} // namespace

int main() {
  test_rows_carry_author_and_licence();
  test_the_credit_names_author_and_licence();
  test_list_labels();
  test_byte_text();
  test_when_a_model_can_be_downloaded();
  test_status_text();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("test_tone3000_view: ok");
  return 0;
}
