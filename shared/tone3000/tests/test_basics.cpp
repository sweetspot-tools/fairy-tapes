/** Fairy Tapes · shared TONE3000 client — PKCE, rate limiter, safe file names, model metadata (no network). */
#include "tone3000/file_util.h"
#include "tone3000/metadata.h"
#include "tone3000/pkce.h"
#include "tone3000/rate_limiter.h"
#include "tone3000/safe_name.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using namespace FairyTapes::Tone3000;
namespace fs = std::filesystem;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

std::string hex(const std::array<std::uint8_t, 32>& d) {
  static const char* k = "0123456789abcdef";
  std::string s;
  for (auto b : d) { s += k[b >> 4]; s += k[b & 15]; }
  return s;
}

void test_sha256_known_vectors() {
  CHECK(hex(sha256("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(hex(sha256("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(hex(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==   // two blocks
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  CHECK(hex(sha256(std::string(1000000, 'a'))) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  // the padding edge: 55, 56 and 64 bytes
  CHECK(hex(sha256(std::string(55, 'a'))) == "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
  CHECK(hex(sha256(std::string(56, 'a'))) == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
  CHECK(hex(sha256(std::string(64, 'a'))) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

void test_base64url() {
  const std::uint8_t a[] = {0xfb, 0xff, 0xfe};
  CHECK(base64Url(a, 3) == "-__-");                       // the URL alphabet: - and _ instead of + and /
  CHECK(base64Url(a, 1) == "-w" && base64Url(a, 2) == "-_8");   // no padding
  CHECK(base64Url(a, 0).empty());
}

void test_pkce_rfc7636_example() {
  // RFC 7636, appendix B
  CHECK(challengeFor("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
}

void test_pkce_from_a_random_source() {
  int calls = 0;
  const RandomBytes fake = [&](std::uint8_t* out, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<std::uint8_t>(i * 7 + calls);
    ++calls;
    return true;
  };
  const Pkce p = makePkce(fake);
  CHECK(p.ok() && p.verifier.size() == 43 && p.challenge.size() == 43);
  CHECK(p.challenge == challengeFor(p.verifier));
  for (char c : p.verifier + p.challenge) CHECK(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_');
  const std::string s = makeState(fake);
  CHECK(s.size() == 22);
  const RandomBytes broken = [](std::uint8_t*, std::size_t) { return false; };
  CHECK(!makePkce(broken).ok() && makeState(broken).empty());   // a failing random source never gives a predictable value
  CHECK(!makePkce(RandomBytes{}).ok());
}

void test_system_random_differs() {
  const Pkce a = makePkce(), b = makePkce();
  CHECK(a.ok() && b.ok() && a.verifier != b.verifier);
  CHECK(makeState() != makeState());
}

void test_same_secret() {
  CHECK(sameSecret("abc", "abc") && !sameSecret("abc", "abd") && !sameSecret("abc", "ab") && sameSecret("", ""));
}

void test_rate_limiter_window() {
  double now = 100.0;
  RateLimiter lim(3, 10.0, [&] { return now; });
  CHECK(lim.tryAcquire());
  now = 101.0;
  CHECK(lim.tryAcquire());
  now = 102.0;
  CHECK(lim.tryAcquire());
  CHECK(!lim.tryAcquire());                                   // the fourth in the window is refused
  CHECK(lim.secondsUntilAvailable() > 7.9 && lim.secondsUntilAvailable() <= 8.0);   // the first stamp (t = 100) leaves at 110
  now = 109.9;
  CHECK(!lim.tryAcquire());
  now = 110.0;                                                // the first stamp is now 10 s old
  CHECK(lim.secondsUntilAvailable() == 0.0 && lim.tryAcquire());
  CHECK(!lim.tryAcquire());                                   // 101, 102 and 110 are inside the window
  now = 130.0;                                                // everything has expired
  CHECK(lim.tryAcquire() && lim.tryAcquire() && lim.tryAcquire() && !lim.tryAcquire());
  RateLimiter strange(0, -1.0, [&] { return now; });          // nonsense limits fall back to something usable
  CHECK(strange.tryAcquire() && !strange.tryAcquire());
}

void test_safe_components() {
  CHECK(sanitizeFileComponent("Marshall JCM 800") == "Marshall_JCM_800");
  CHECK(sanitizeFileComponent("../../etc/passwd") == "etc_passwd");
  CHECK(sanitizeFileComponent("a/b\\c:d*e?f\"g<h>i|j") == "a_b_c_d_e_f_g_h_i_j");
  CHECK(sanitizeFileComponent("..") == "item" && sanitizeFileComponent("...") == "item" && sanitizeFileComponent("") == "item");
  CHECK(sanitizeFileComponent("a..b") == "a.b");
  CHECK(sanitizeFileComponent("name.") == "name" && sanitizeFileComponent(" name ") == "name");
  CHECK(sanitizeFileComponent("caf\xC3\xA9 \xE2\x82\xAC") == "caf");                  // non-ASCII bytes become separators
  CHECK(sanitizeFileComponent("CON") == "item" && sanitizeFileComponent("com1.txt") == "item" && sanitizeFileComponent("nul") == "item");
  CHECK(sanitizeFileComponent("console") == "console");                                 // only the exact names are reserved
  CHECK(sanitizeFileComponent("CON", "tone") == "tone");
  CHECK(sanitizeFileComponent(std::string(500, 'x')).size() == kMaxNameComponent);
  CHECK(sanitizeFileComponent(std::string("a\0b", 3)) == "a_b");
  CHECK(sanitizeFileComponent("a\r\nb") == "a_b");
}

void test_no_component_can_escape_a_folder() {
  const char* nasty[] = {"..", "../x", "x/..", "/abs", "C:\\win", "a/../../b", "....//....", "%2e%2e/x", ".hidden", "~/x", "x\x01y"};
  for (const char* n : nasty) {
    const std::string c = sanitizeFileComponent(n);
    CHECK(!c.empty() && c.find('/') == std::string::npos && c.find('\\') == std::string::npos && c.find("..") == std::string::npos);
    CHECK(c[0] != '.' && c.back() != '.');
  }
  const std::string rel = downloadRelativePath("42", "Fender Twin", "m-7");
  CHECK(rel == "42-Fender_Twin/m-7.nam");
  const std::string evil = downloadRelativePath("../../x", "../..", "..");
  CHECK(evil.find("..") == std::string::npos && evil.find('/') == evil.rfind('/') && evil.size() > 5);   // one folder, one file
  CHECK(evil.compare(evil.size() - 4, 4, ".nam") == 0);
}

fs::path tempDir(const char* name) {
  const fs::path p = fs::temp_directory_path() / (std::string("fairytapes_t3k_") + name);
  std::error_code ec;
  fs::remove_all(p, ec);
  fs::create_directories(p);
  return p;
}

void test_atomic_write_and_limited_read() {
  const fs::path dir = tempDir("atomic");
  const fs::path f = dir / "sub" / "a.txt";
  CHECK(writeFileAtomic(f, "hello"));
  CHECK(readFileLimited(f, 100).value_or("") == "hello");
  CHECK(writeFileAtomic(f, "world!!"));                                // replaces
  CHECK(readFileLimited(f, 100).value_or("") == "world!!");
  CHECK(!fs::exists(fs::path(f).concat(".part")));                     // no temporary left behind
  CHECK(!readFileLimited(f, 3).has_value());                           // longer than the limit
  CHECK(!readFileLimited(dir / "missing", 100).has_value());
  CHECK(writeFileAtomic(dir / "empty", "") && readFileLimited(dir / "empty", 10).value_or("x").empty());
  fs::remove_all(dir);
}

ModelMeta sample() {
  ModelMeta m;
  m.toneId = "42"; m.modelId = "m-7"; m.title = "Fender Twin"; m.authorName = "Jane \xC3\xA8"; m.authorVerified = true;
  m.license = "cc-by"; m.pageUrl = "https://www.tone3000.com/tones/42"; m.size = "standard"; m.architecture = "A2";
  m.downloadedAt = "2026-10-21T10:00:00Z";
  return m;
}

void test_metadata_roundtrip_and_sidecar_name() {
  CHECK(sidecarPathFor("/x/y/model.nam") == fs::path("/x/y/model.t3k.json"));
  CHECK(sidecarPathFor("/x/y/a.b.nam") == fs::path("/x/y/a.b.t3k.json"));
  const fs::path dir = tempDir("meta");
  const fs::path model = dir / "42-Fender_Twin" / "m-7.nam";
  CHECK(writeMeta(model, sample()));
  const auto back = readMeta(model);
  CHECK(back.has_value());
  if (back) {
    const ModelMeta s = sample();
    CHECK(back->toneId == s.toneId && back->modelId == s.modelId && back->title == s.title && back->authorName == s.authorName);
    CHECK(back->authorVerified && back->license == "cc-by" && back->pageUrl == s.pageUrl && back->size == "standard");
    CHECK(back->architecture == "A2" && back->downloadedAt == s.downloadedAt);
  }
  CHECK(!readMeta(dir / "nothing.nam").has_value());
  fs::remove_all(dir);
}

void test_metadata_is_tolerant_and_bounded() {
  CHECK(!metaFromJson("not json").has_value());
  CHECK(!metaFromJson("[1,2]").has_value());
  CHECK(!metaFromJson("").has_value());
  auto m = metaFromJson("{\"title\":\"T\",\"future_field\":{\"a\":1},\"author_verified\":\"yes\"}");
  CHECK(m && m->title == "T" && m->license.empty() && !m->authorVerified);          // unknown ignored, wrong type ignored
  m = metaFromJson("{\"title\":5,\"license\":null}");
  CHECK(m && m->title.empty() && m->license.empty());
  const std::string huge = "{\"title\":\"" + std::string(5000, 'x') + "\"}";
  m = metaFromJson(huge);
  CHECK(m && m->title.size() == kMaxMetaString);
  m = metaFromJson("{\"title\":\"a\\u0007b\\nc\"}");
  CHECK(m && m->title == "a b c");                                                   // control characters become spaces
  std::string cutme = "{\"title\":\"" + std::string(199, 'a') + "\xC3\xA8\"}";       // a 2-byte character across the limit
  m = metaFromJson(cutme);
  CHECK(m && m->title.size() == 199);                                                // never cut in the middle of a character
  CHECK(!metaFromJson(std::string(kMaxMetaFileBytes + 1, ' ')).has_value());
}

void test_license_badges() {
  CHECK(licenseBadge("t3k") == "T3K" && licenseBadge("cc-by") == "CC BY" && licenseBadge("cc-by-nc") == "CC BY-NC");
  CHECK(licenseBadge("cc-by-sa") == "CC BY-SA" && licenseBadge("cc-by-nc-sa") == "CC BY-NC-SA" && licenseBadge("cco") == "CC0");
  CHECK(licenseBadge("").size() == 1 && licenseBadge("something-very-long-indeed").size() == 12);
  CHECK(isNonCommercialLicense("cc-by-nc") && isNonCommercialLicense("cc-by-nc-sa") && !isNonCommercialLicense("cc-by") &&
        !isNonCommercialLicense("t3k") && !isNonCommercialLicense(""));
}
} // namespace

int main() {
  std::puts("tone3000 basics tests");
  test_sha256_known_vectors();
  test_base64url();
  test_pkce_rfc7636_example();
  test_pkce_from_a_random_source();
  test_system_random_differs();
  test_same_secret();
  test_rate_limiter_window();
  test_safe_components();
  test_no_component_can_escape_a_folder();
  test_atomic_write_and_limited_read();
  test_metadata_roundtrip_and_sidecar_name();
  test_metadata_is_tolerant_and_bounded();
  test_license_badges();
  if (g_failed) { std::fprintf(stderr, "FAILED (%d)\n", g_failed); return 1; }
  std::puts("All tone3000 basics tests passed");
  return 0;
}
