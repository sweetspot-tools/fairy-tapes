/** Fairy Tapes · shared TONE3000 client — the stored login and the worker, against a fake server (no network, no browser). */
#include "tone3000/client.h"
#include "tone3000/file_util.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#ifndef _WIN32
#include <sys/stat.h>
#endif

using namespace FairyTapes::Tone3000;
namespace fs = std::filesystem;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

const char* kTone = R"({"id":42,"title":"Fender Twin","url":"https://www.tone3000.com/tones/fender-twin-42","gear":"amp","format":"nam",
  "license":"cc-by","sizes":["standard"],"user":{"id":7,"username":"jane","display_name":"Jane Doe","is_verified":true},
  "models_count":2,"a1_models_count":1,"a2_models_count":1,"is_favorite":false})";
const char* kModels = R"({"data":[
  {"id":100,"name":"Twin standard","size":"standard","tone_id":42,"architecture_version":"2","model_url":"https://www.tone3000.com/api/v1/models/100/file"},
  {"id":101,"name":"Twin lite","size":"lite","tone_id":42,"architecture_version":"1","model_url":"https://www.tone3000.com/api/v1/models/101/file"}],
  "page":1,"page_size":100,"total":2,"total_pages":1})";
const char* kList = R"({"data":[{"id":5,"title":"A"},{"id":6,"title":"B"}],"page":1,"page_size":25,"total":2,"total_pages":1})";
const std::string kNam = "{\"version\":\"0.5.4\",\"architecture\":\"WaveNet\",\"config\":{},\"weights\":[0.1,0.2,0.3,0.4,0.5,0.6,0.7]}";

// A fake server: answers by exact URL, remembers what it was asked and which token came with it.
struct FakeServer : HttpClient {
  std::mutex m;
  std::map<std::string, HttpResponse> routes;
  std::vector<std::string> asked;
  std::vector<std::string> tokens;                 // "Bearer ..." header of each request ("" when none)
  int unauthorizedFirst = 0;                       // the next N answers are 401
  HttpResponse perform(const HttpRequest& r, const HttpSink& sink, const std::atomic<bool>*) override {
    std::lock_guard<std::mutex> lk(m);
    asked.push_back(r.url);
    std::string tok;
    for (const auto& h : r.headers) if (h.first == "Authorization") tok = h.second;
    tokens.push_back(tok);
    if (unauthorizedFirst > 0) { --unauthorizedFirst; HttpResponse x; x.status = 401; return x; }
    const auto it = routes.find(r.url);
    HttpResponse res;
    if (it == routes.end()) { res.status = 404; return res; }
    res = it->second;
    if (sink && res.ok()) {
      const std::string body = res.body;
      res.body.clear();
      if (!sink(body.data(), body.size())) { res.error = "download refused by the receiver"; res.status = 0; }
    }
    return res;
  }
  size_t count() { std::lock_guard<std::mutex> lk(m); return asked.size(); }
};

HttpResponse ok(const std::string& body) { HttpResponse r; r.status = 200; r.body = body; return r; }

const char* B = "https://www.tone3000.com/api/v1";

void populate(FakeServer& s) {
  s.routes[std::string(B) + "/user"] = ok("{\"id\":7,\"username\":\"jane\",\"display_name\":\"Jane Doe\"}");
  s.routes[std::string(B) + "/tones/42"] = ok(kTone);
  s.routes[std::string(B) + "/models?tone_id=42&page=1&page_size=100"] = ok(kModels);
  s.routes[std::string(B) + "/tones/favorited?page=1&page_size=25"] = ok(kList);
  s.routes[std::string(B) + "/models/100/file"] = ok(kNam);
  s.routes[std::string(B) + "/models/101/file"] = ok("<html>not a model</html> padding padding padding padding padding padding padding");
}

fs::path tempDir(const char* name) {
  const fs::path p = fs::temp_directory_path() / (std::string("fairytapes_t3k_client_") + name);
  std::error_code ec;
  fs::remove_all(p, ec);
  fs::create_directories(p);
  return p;
}

template <class P>
bool waitFor(P pred, double seconds = 8.0) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
  while (std::chrono::steady_clock::now() < end) {
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return pred();
}

struct Rig {
  fs::path dir;
  FakeServer server;
  std::atomic<double> now{1000000.0};
  int signIns = 0, refreshes = 0;
  bool signInOk = true, refreshOk = true;
  std::string pickedTone;                       // what the fake browser login returns as the chosen tone
  ClientBackends backends() {
    ClientBackends b;
    b.http = &server;
    b.nowUnix = [this] { return now.load(); };
    b.signIn = [this](Prompt p, double, const std::atomic<bool>*) {
      ++signIns;
      OAuthOutcome o;
      if (!signInOk) { o.message = "The login did not work"; return o; }
      o.ok = true;
      o.tokens.access = "AT" + std::to_string(signIns);
      o.tokens.refresh = "RT1";
      o.tokens.expiresInSeconds = 3600;
      if (p == Prompt::SelectTone) o.toneId = pickedTone;
      return o;
    };
    b.refresh = [this](const std::string&, const std::atomic<bool>*) {
      ++refreshes;
      OAuthOutcome o;
      if (!refreshOk) { o.message = "The login expired, sign in again"; return o; }
      o.ok = true;
      o.tokens.access = "NEWAT" + std::to_string(refreshes);
      o.tokens.expiresInSeconds = 3600;
      return o;
    };
    return b;
  }
  ClientPaths paths() { return {dir / "login.json", dir / "tones"}; }
};

void test_starts_signed_out_or_with_the_stored_login() {
  Rig r;
  r.dir = tempDir("start");
  populate(r.server);
  {
    Tone3000Client c(r.paths(), r.backends());
    CHECK(c.snapshot().phase == ClientState::Phase::SignedOut && !c.snapshot().busy);
  }
  StoredLogin l;
  l.tokens.access = "AT0"; l.tokens.refresh = "RT0"; l.expiresAtUnix = r.now + 3600; l.userName = "Jane Doe";
  CHECK(saveLogin(r.dir / "login.json", l));
  Tone3000Client c(r.paths(), r.backends());
  const auto s = c.snapshot();
  CHECK(s.phase == ClientState::Phase::SignedIn && s.userName == "Jane Doe");
  CHECK(r.server.count() == 0);                                   // no network until the user does something
  fs::remove_all(r.dir);
}

void test_sign_in_shows_the_favourites_and_keeps_the_login_private() {
  Rig r;
  r.dir = tempDir("signin");
  populate(r.server);
  Tone3000Client c(r.paths(), r.backends());
  c.signIn(Prompt::Login);
  CHECK(waitFor([&] { auto s = c.snapshot(); return s.phase == ClientState::Phase::SignedIn && s.listLoaded && !s.busy; }));
  const auto s = c.snapshot();
  CHECK(s.userName == "Jane Doe" && s.list.size() == 2 && s.listKind == ListKind::Favorited && s.message.empty());
  const auto saved = loadLogin(r.dir / "login.json");
  CHECK(saved && saved->tokens.access == "AT1" && saved->tokens.refresh == "RT1" && saved->userName == "Jane Doe");
  CHECK(saved && saved->expiresAtUnix > r.now + 3000);
#ifndef _WIN32
  struct stat st{};
  CHECK(::stat((r.dir / "login.json").c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);   // owner only
#endif
  std::lock_guard<std::mutex> lk(r.server.m);
  for (const auto& t : r.server.tokens) CHECK(t == "Bearer AT1");
  fs::remove_all(r.dir);
}

void test_select_tone_opens_the_tone_and_its_models() {
  Rig r;
  r.dir = tempDir("select");
  populate(r.server);
  r.pickedTone = "42";
  Tone3000Client c(r.paths(), r.backends());
  c.signIn(Prompt::SelectTone);
  CHECK(waitFor([&] { auto s = c.snapshot(); return s.tone.has_value() && s.models.size() == 2 && !s.busy; }));
  const auto s = c.snapshot();
  CHECK(s.tone->title == "Fender Twin" && s.tone->license == "cc-by" && s.tone->author.shownName() == "Jane Doe");
  CHECK(s.models[0].architecture == "A2" && s.models[1].architecture == "A1" && s.models[0].toneId == "42");
  fs::remove_all(r.dir);
}

void test_failed_or_cancelled_sign_in() {
  Rig r;
  r.dir = tempDir("fail");
  populate(r.server);
  r.signInOk = false;
  Tone3000Client c(r.paths(), r.backends());
  c.signIn(Prompt::Login);
  CHECK(waitFor([&] { auto s = c.snapshot(); return !s.busy && s.phase == ClientState::Phase::SignedOut && !s.message.empty(); }));
  CHECK(c.snapshot().message == "The login did not work" && !fs::exists(r.dir / "login.json"));
  fs::remove_all(r.dir);
}

void test_a_401_renews_the_token_and_retries() {
  Rig r;
  r.dir = tempDir("renew");
  populate(r.server);
  StoredLogin l;
  l.tokens.access = "OLD"; l.tokens.refresh = "RT0"; l.expiresAtUnix = r.now + 3600; l.userName = "Jane";
  saveLogin(r.dir / "login.json", l);
  Tone3000Client c(r.paths(), r.backends());
  r.server.unauthorizedFirst = 1;                                 // the old token is refused once
  c.showTone("42");
  CHECK(waitFor([&] { auto s = c.snapshot(); return s.tone.has_value() && !s.busy; }));
  CHECK(r.refreshes == 1 && c.snapshot().phase == ClientState::Phase::SignedIn);
  std::lock_guard<std::mutex> lk(r.server.m);
  CHECK(r.server.tokens.front() == "Bearer OLD" && r.server.tokens[1] == "Bearer NEWAT1");   // the retry uses the new token
  CHECK(loadLogin(r.dir / "login.json")->tokens.access == "NEWAT1");
  fs::remove_all(r.dir);
}

void test_when_the_renewal_fails_the_user_is_signed_out_and_the_file_goes() {
  Rig r;
  r.dir = tempDir("signedout");
  populate(r.server);
  StoredLogin l;
  l.tokens.access = "OLD"; l.tokens.refresh = "RT0"; l.expiresAtUnix = r.now + 3600; l.userName = "Jane";
  saveLogin(r.dir / "login.json", l);
  r.refreshOk = false;
  Tone3000Client c(r.paths(), r.backends());
  r.server.unauthorizedFirst = 5;
  c.showTone("42");
  CHECK(waitFor([&] { auto s = c.snapshot(); return !s.busy && s.phase == ClientState::Phase::SignedOut; }));
  CHECK(c.snapshot().message == "Sign in to TONE3000 again" && !c.snapshot().tone.has_value());
  CHECK(!fs::exists(r.dir / "login.json"));
  fs::remove_all(r.dir);
}

void test_an_expiring_token_is_renewed_before_the_call() {
  Rig r;
  r.dir = tempDir("expiring");
  populate(r.server);
  StoredLogin l;
  l.tokens.access = "OLD"; l.tokens.refresh = "RT0"; l.expiresAtUnix = r.now + 20;   // less than a minute left
  saveLogin(r.dir / "login.json", l);
  Tone3000Client c(r.paths(), r.backends());
  c.showTone("42");
  CHECK(waitFor([&] { auto s = c.snapshot(); return s.tone.has_value() && !s.busy; }));
  std::lock_guard<std::mutex> lk(r.server.m);
  CHECK(r.refreshes == 1 && r.server.tokens.front() == "Bearer NEWAT1");           // the old token never went out
  fs::remove_all(r.dir);
}

void test_download_saves_the_file_and_the_sidecar_and_is_not_repeated() {
  Rig r;
  r.dir = tempDir("download");
  populate(r.server);
  r.pickedTone = "42";
  Tone3000Client c(r.paths(), r.backends());
  c.signIn(Prompt::SelectTone);
  CHECK(waitFor([&] { auto s = c.snapshot(); return s.models.size() == 2 && !s.busy; }));
  c.downloadModel("100");
  CHECK(waitFor([&] { return c.snapshot().download == ClientState::DownloadPhase::Done; }));
  const auto s = c.snapshot();
  const fs::path file = r.dir / "tones" / "42-Fender_Twin" / "100.nam";
  CHECK(s.downloadPath == file.string() && fs::exists(file) && fs::file_size(file) == kNam.size());
  CHECK(!fs::exists(fs::path(file).concat(".part")));
  const auto meta = readMeta(file);
  CHECK(meta && meta->toneId == "42" && meta->modelId == "100" && meta->title == "Fender Twin" && meta->authorName == "Jane Doe");
  CHECK(meta && meta->authorVerified && meta->license == "cc-by" && meta->architecture == "A2" && meta->size == "standard");
  CHECK(meta && meta->pageUrl == "https://www.tone3000.com/tones/fender-twin-42" && meta->downloadedAt.size() == 20);
  const size_t before = r.server.count();
  c.downloadModel("100");                                         // already here: nothing is fetched
  CHECK(waitFor([&] { return !c.snapshot().busy && c.snapshot().download == ClientState::DownloadPhase::Done; }));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(r.server.count() == before);
  fs::remove_all(r.dir);
}

void test_download_refuses_what_is_not_a_model_and_unknown_ids() {
  Rig r;
  r.dir = tempDir("notnam");
  populate(r.server);
  r.pickedTone = "42";
  Tone3000Client c(r.paths(), r.backends());
  c.signIn(Prompt::SelectTone);
  CHECK(waitFor([&] { auto s = c.snapshot(); return s.models.size() == 2 && !s.busy; }));
  c.downloadModel("101");                                         // the server sends an HTML page
  CHECK(waitFor([&] { return c.snapshot().download == ClientState::DownloadPhase::Failed; }));
  CHECK(c.snapshot().downloadMessage == "The downloaded file is not a NAM model");
  CHECK(!fs::exists(r.dir / "tones" / "42-Fender_Twin" / "101.nam") && !fs::exists(r.dir / "tones" / "42-Fender_Twin" / "101.nam.part"));
  const size_t before = r.server.count();
  c.downloadModel("999");                                         // not one of the models shown
  c.downloadModel("../../x");
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  CHECK(r.server.count() == before && !c.snapshot().busy);
  fs::remove_all(r.dir);
}

void test_download_without_a_tone_does_nothing() {
  Rig r;
  r.dir = tempDir("notone");
  populate(r.server);
  StoredLogin l;
  l.tokens.access = "AT0"; l.tokens.refresh = "RT0"; l.expiresAtUnix = r.now + 3600; l.userName = "Jane";
  saveLogin(r.dir / "login.json", l);
  Tone3000Client c(r.paths(), r.backends());
  c.downloadModel("100");                                         // no tone has been opened: nothing to download from
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  CHECK(r.server.count() == 0 && !c.snapshot().busy && c.snapshot().download == ClientState::DownloadPhase::None);
  fs::remove_all(r.dir);
}

void test_download_failure_leaves_nothing_behind() {
  Rig r;
  r.dir = tempDir("dlfail");
  populate(r.server);
  r.server.routes[std::string(B) + "/models/100/file"].status = 500;
  r.pickedTone = "42";
  Tone3000Client c(r.paths(), r.backends());
  c.signIn(Prompt::SelectTone);
  CHECK(waitFor([&] { auto s = c.snapshot(); return s.models.size() == 2 && !s.busy; }));
  c.downloadModel("100");
  CHECK(waitFor([&] { return c.snapshot().download == ClientState::DownloadPhase::Failed; }));
  CHECK(c.snapshot().downloadMessage == "TONE3000 is not answering");
  CHECK(!fs::exists(r.dir / "tones" / "42-Fender_Twin" / "100.nam") && !fs::exists(r.dir / "tones" / "42-Fender_Twin" / "100.nam.part"));
  fs::remove_all(r.dir);
}

void test_sign_out_forgets_everything() {
  Rig r;
  r.dir = tempDir("signout");
  populate(r.server);
  r.pickedTone = "42";
  Tone3000Client c(r.paths(), r.backends());
  c.signIn(Prompt::SelectTone);
  CHECK(waitFor([&] { auto s = c.snapshot(); return s.tone.has_value() && !s.busy; }));
  CHECK(fs::exists(r.dir / "login.json"));
  c.signOut();
  CHECK(waitFor([&] { auto s = c.snapshot(); return s.phase == ClientState::Phase::SignedOut && !s.tone.has_value() && !s.busy; }));
  CHECK(!fs::exists(r.dir / "login.json") && c.snapshot().list.empty() && c.snapshot().userName.empty());
  fs::remove_all(r.dir);
}

void test_cancel_stops_a_waiting_login() {
  Rig r;
  r.dir = tempDir("cancel");
  populate(r.server);
  ClientBackends b = r.backends();
  b.signIn = [](Prompt, double, const std::atomic<bool>* cancel) {
    OAuthOutcome o;
    for (int i = 0; i < 1000 && !(cancel && cancel->load()); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    o.cancelled = true;
    o.message = "Cancelled";
    return o;
  };
  Tone3000Client c(r.paths(), b);
  c.signIn(Prompt::Login);
  CHECK(waitFor([&] { return c.snapshot().phase == ClientState::Phase::SigningIn; }));
  c.cancel();
  CHECK(waitFor([&] { auto s = c.snapshot(); return !s.busy && s.phase == ClientState::Phase::SignedOut; }));
  CHECK(c.snapshot().message.empty());                            // a cancelled login is not an error
  fs::remove_all(r.dir);
}

void test_stored_login_survives_garbage_and_clears() {
  const fs::path dir = tempDir("storage");
  CHECK(!loadLogin(dir / "none.json").has_value());
  writeFileAtomic(dir / "bad.json", "not json");
  CHECK(!loadLogin(dir / "bad.json").has_value());
  writeFileAtomic(dir / "bad2.json", "{\"access_token\":\"has space\"}");
  CHECK(!loadLogin(dir / "bad2.json").has_value());
  writeFileAtomic(dir / "ok.json", "{\"access_token\":\"AT\",\"expires_at\":-5,\"user\":\"a\\nb\",\"extra\":1}");
  const auto l = loadLogin(dir / "ok.json");
  CHECK(l && l->tokens.access == "AT" && l->expiresAtUnix == 0 && l->userName == "a b" && l->tokens.refresh.empty());
  CHECK(clearLogin(dir / "ok.json") && !fs::exists(dir / "ok.json") && clearLogin(dir / "ok.json"));
  fs::remove_all(dir);
}

void test_looks_like_nam() {
  const fs::path dir = tempDir("namcheck");
  writeFileAtomic(dir / "a.nam", kNam);
  writeFileAtomic(dir / "b.nam", "  \n" + kNam + "\n");
  writeFileAtomic(dir / "c.nam", "<html>" + std::string(200, 'x') + "</html>");
  writeFileAtomic(dir / "d.nam", "{}");
  writeFileAtomic(dir / "e.nam", std::string(100, '{'));
  CHECK(looksLikeNamFile(dir / "a.nam") && looksLikeNamFile(dir / "b.nam"));
  CHECK(!looksLikeNamFile(dir / "c.nam") && !looksLikeNamFile(dir / "d.nam") && !looksLikeNamFile(dir / "e.nam") && !looksLikeNamFile(dir / "none.nam"));
  fs::remove_all(dir);
}
} // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::puts("tone3000 client tests");
#define RUN(t) do { std::fprintf(stderr, "  %s\n", #t); t(); } while (0)
  RUN(test_starts_signed_out_or_with_the_stored_login);
  RUN(test_sign_in_shows_the_favourites_and_keeps_the_login_private);
  RUN(test_select_tone_opens_the_tone_and_its_models);
  RUN(test_failed_or_cancelled_sign_in);
  RUN(test_a_401_renews_the_token_and_retries);
  RUN(test_when_the_renewal_fails_the_user_is_signed_out_and_the_file_goes);
  RUN(test_an_expiring_token_is_renewed_before_the_call);
  RUN(test_download_saves_the_file_and_the_sidecar_and_is_not_repeated);
  RUN(test_download_refuses_what_is_not_a_model_and_unknown_ids);
  RUN(test_download_without_a_tone_does_nothing);
  RUN(test_download_failure_leaves_nothing_behind);
  RUN(test_sign_out_forgets_everything);
  RUN(test_cancel_stops_a_waiting_login);
  RUN(test_stored_login_survives_garbage_and_clears);
  RUN(test_looks_like_nam);
  if (g_failed) { std::fprintf(stderr, "FAILED (%d)\n", g_failed); return 1; }
  std::puts("All tone3000 client tests passed");
  return 0;
}
