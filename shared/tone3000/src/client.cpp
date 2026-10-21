// Fairy Tapes · shared TONE3000 client — the worker behind the interface (see client.h).
#include "tone3000/client.h"

#include "tone3000/file_util.h"
#include "tone3000/safe_name.h"

#include <algorithm>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>

namespace FairyTapes::Tone3000 {

namespace {
using Phase = ClientState::Phase;
using DlPhase = ClientState::DownloadPhase;

std::string utf8(const std::filesystem::path& p) {
  const std::u8string u = p.u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::string isoUtc(double secondsSince1970) {
  const std::time_t t = static_cast<std::time_t>(secondsSince1970);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}
} // namespace

bool looksLikeNamFile(const std::filesystem::path& file) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(file, ec);
  if (ec || size < 64 || size > kMaxModelBytes) return false;
  std::ifstream in(file, std::ios::binary);
  if (!in) return false;
  char head[256];
  in.read(head, sizeof head);
  const std::size_t n = static_cast<std::size_t>(in.gcount());
  std::size_t i = 0;
  while (i < n && (head[i] == ' ' || head[i] == '\n' || head[i] == '\r' || head[i] == '\t')) ++i;
  if (i >= n || head[i] != '{') return false;
  in.clear();
  in.seekg(-static_cast<std::streamoff>(std::min<std::uintmax_t>(size, 64)), std::ios::end);
  char tail[64];
  in.read(tail, sizeof tail);
  std::size_t m = static_cast<std::size_t>(in.gcount());
  while (m > 0 && (tail[m - 1] == ' ' || tail[m - 1] == '\n' || tail[m - 1] == '\r' || tail[m - 1] == '\t')) --m;
  return m > 0 && tail[m - 1] == '}';
}

struct Tone3000Client::Impl {
  struct Command {
    enum class Kind { SignIn, SignOut, ShowTone, ShowList, Download } kind;
    Prompt prompt = Prompt::Login;
    std::string id;
    ListKind list = ListKind::Favorited;
    int page = 1;
  };

  Impl(ClientPaths p, ClientBackends b)
  : paths(std::move(p)), be(std::move(b)), limiter(60, 60.0), api(*be.http, limiter, be.baseUrl) {
    if (!be.nowUnix) be.nowUnix = [] { return static_cast<double>(std::time(nullptr)); };
    if (auto stored = loadLogin(paths.loginFile)) {
      login = *stored;
      api.setAccessToken(login.tokens.access);
      state.phase = Phase::SignedIn;
      state.userName = login.userName;
    }
    worker = std::thread([this] { loop(); });
  }

  ~Impl() {
    {
      std::lock_guard<std::mutex> lk(m);
      quit = true;
      queue.clear();
    }
    cancelFlag = true;
    cv.notify_all();
    if (worker.joinable()) worker.join();
  }

  // ---- state, always under the mutex ----
  template <class F>
  void update(F f) {
    std::lock_guard<std::mutex> lk(m);
    f(state);
    ++state.version;
  }
  void setBusy(const std::string& text) { update([&](ClientState& s) { s.busy = !text.empty(); s.busyText = text; }); }
  void setMessage(const std::string& text) { update([&](ClientState& s) { s.message = text; }); }

  void post(Command c) {
    {
      std::lock_guard<std::mutex> lk(m);
      queue.push_back(std::move(c));
    }
    cv.notify_one();
  }

  void loop() {
    for (;;) {
      Command c;
      {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return quit || !queue.empty(); });
        if (quit) return;
        c = std::move(queue.front());
        queue.pop_front();
        cancelFlag = false;
      }
      handle(c);
      setBusy("");
    }
  }

  void handle(const Command& c) {
    switch (c.kind) {
    case Command::Kind::SignIn: doSignIn(c.prompt); break;
    case Command::Kind::SignOut: doSignOut(); break;
    case Command::Kind::ShowTone: doShowTone(c.id); break;
    case Command::Kind::ShowList: doShowList(c.list, c.page); break;
    case Command::Kind::Download: doDownload(c.id); break;
    }
  }

  // ---- login ----
  void signedOut(const std::string& message) {
    login = StoredLogin{};
    api.setAccessToken({});
    clearLogin(paths.loginFile);
    update([&](ClientState& s) {
      s.phase = Phase::SignedOut;
      s.userName.clear();
      s.message = message;
      s.tone.reset();
      s.models.clear();
      s.list.clear();
      s.listLoaded = false;
      s.download = DlPhase::None;
    });
  }

  void adopt(const Tokens& t, const std::string& userName) {
    login.tokens = t;
    login.expiresAtUnix = be.nowUnix() + t.expiresInSeconds;
    if (!userName.empty()) login.userName = userName;
    api.setAccessToken(login.tokens.access);
    saveLogin(paths.loginFile, login);
  }

  // A fresh access token when the old one is about to stop, or after a 401. False: the user must sign in again.
  bool refreshTokens() {
    if (login.tokens.refresh.empty() || !be.refresh) return false;
    const OAuthOutcome o = be.refresh(login.tokens.refresh, &cancelFlag);
    if (!o.ok) return false;
    adopt(o.tokens, {});
    return true;
  }

  bool ensureFresh() {
    if (login.tokens.access.empty()) return false;
    if (login.expiresAtUnix > 0 && login.expiresAtUnix - be.nowUnix() < 60.0) {
      if (!refreshTokens() && login.expiresAtUnix <= be.nowUnix()) return false;
    }
    return true;
  }

  // Runs an API call with the login; on 401 renews the token once and retries; if that fails, signs out.
  template <class R, class F>
  ApiResult<R> authed(F call) {
    if (!ensureFresh()) {
      signedOut("Sign in to TONE3000 again");
      ApiResult<R> r;
      r.error = ApiError{ApiError::Kind::Unauthorized, 401, "Sign in to TONE3000 again"};
      return r;
    }
    ApiResult<R> r = call();
    if (!r.ok() && r.error->kind == ApiError::Kind::Unauthorized) {
      if (refreshTokens()) r = call();
      if (!r.ok() && r.error->kind == ApiError::Kind::Unauthorized) signedOut("Sign in to TONE3000 again");
    }
    return r;
  }

  void doSignIn(Prompt prompt) {
    update([&](ClientState& s) { s.phase = Phase::SigningIn; s.message.clear(); });
    setBusy("Waiting for the browser...");
    const OAuthOutcome o = be.signIn ? be.signIn(prompt, 300.0, &cancelFlag) : OAuthOutcome{false, false, {}, {}, "TONE3000 is not set up in this build"};
    if (!o.ok) {
      update([&](ClientState& s) {
        s.phase = login.tokens.access.empty() ? Phase::SignedOut : Phase::SignedIn;
        s.message = o.cancelled ? std::string() : o.message;
      });
      return;
    }
    adopt(o.tokens, {});
    setBusy("Signing in...");
    const auto u = api.user(&cancelFlag);
    const std::string name = u.ok() ? (u.value.displayName.empty() ? u.value.username : u.value.displayName) : "TONE3000 user";
    adopt(o.tokens, name);
    update([&](ClientState& s) { s.phase = Phase::SignedIn; s.userName = name; s.message.clear(); });
    if (prompt == Prompt::SelectTone && !o.toneId.empty()) doShowTone(o.toneId);
    else doShowList(ListKind::Favorited, 1);
  }

  void doSignOut() {
    cancelFlag = true;
    signedOut({});
  }

  // ---- browsing ----
  void doShowTone(const std::string& toneId) {
    setBusy("Loading the tone...");
    auto t = authed<Tone>([&] { return api.tone(toneId, &cancelFlag); });
    if (!t.ok()) { if (t.error->kind != ApiError::Kind::Cancelled) setMessage(t.error->message); return; }
    setBusy("Loading the models...");
    auto m = authed<Page<Model>>([&] { return api.models(toneId, 1, &cancelFlag); });
    if (!m.ok()) { if (m.error->kind != ApiError::Kind::Cancelled) setMessage(m.error->message); return; }
    Tone tone = t.value;
    // a page link is only kept when it really points at tone3000.com (it is opened from the interface)
    if (tone.url.compare(0, be.pageUrlPrefix.size(), be.pageUrlPrefix) != 0) tone.url.clear();
    std::vector<Model> models;
    for (auto& mod : m.value.items) {
      if (mod.toneId.empty()) mod.toneId = tone.id;
      models.push_back(std::move(mod));
    }
    update([&](ClientState& s) {
      s.tone = tone;
      s.models = std::move(models);
      s.message.clear();
      s.download = DlPhase::None;
      s.downloadMessage.clear();
    });
  }

  void doShowList(ListKind kind, int page) {
    setBusy("Loading...");
    auto r = authed<Page<Tone>>([&] { return api.tones(kind, page, &cancelFlag); });
    if (!r.ok()) { if (r.error->kind != ApiError::Kind::Cancelled) setMessage(r.error->message); return; }
    update([&](ClientState& s) {
      s.listKind = kind;
      s.list = std::move(r.value.items);
      s.listPage = r.value.page;
      s.listTotalPages = std::max(1, r.value.totalPages);
      s.listLoaded = true;
      s.message.clear();
    });
  }

  // ---- download ----
  void failDownload(const std::string& message) {
    update([&](ClientState& s) { s.download = DlPhase::Failed; s.downloadMessage = message; });
  }

  void doDownload(const std::string& modelId) {
    Model model;
    Tone tone;
    {
      std::lock_guard<std::mutex> lk(m);
      if (!state.tone) return;
      tone = *state.tone;
      const auto it = std::find_if(state.models.begin(), state.models.end(), [&](const Model& x) { return x.id == modelId; });
      if (it == state.models.end()) return;   // not one of the models being shown: nothing is fetched
      model = *it;
    }
    const std::filesystem::path file = paths.tonesFolder / std::filesystem::path(downloadRelativePath(tone.id, tone.title, model.id));
    update([&](ClientState& s) { s.download = DlPhase::Running; s.downloadModelId = modelId; s.downloadBytes = 0; s.downloadMessage.clear(); s.downloadPath.clear(); });
    std::error_code ec;
    if (std::filesystem::exists(file, ec) && looksLikeNamFile(file) && readMeta(file)) {   // already here: no network
      update([&](ClientState& s) { s.download = DlPhase::Done; s.downloadPath = utf8(file); });
      return;
    }
    setBusy("Downloading the model...");
    std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::path part = file;
    part += ".part";
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    if (!out) { failDownload("Could not write the model file"); return; }
    std::uint64_t written = 0;
    bool writeFailed = false;
    auto r = authed<bool>([&] {
      out.clear();
      out.seekp(0);
      written = 0;
      return api.download(model, [&](const char* data, std::size_t n) {
        out.write(data, static_cast<std::streamsize>(n));
        if (!out) { writeFailed = true; return false; }
        written += n;
        update([&](ClientState& s) { s.downloadBytes = written; });
        return true;
      }, kMaxModelBytes, &cancelFlag);
    });
    out.close();
    if (!r.ok() || writeFailed) {
      std::filesystem::remove(part, ec);
      if (r.ok() || r.error->kind != ApiError::Kind::Cancelled) failDownload(writeFailed ? "Could not write the model file" : r.error->message);
      else update([&](ClientState& s) { s.download = DlPhase::None; });
      return;
    }
    if (!looksLikeNamFile(part)) {
      std::filesystem::remove(part, ec);
      failDownload("The downloaded file is not a NAM model");
      return;
    }
    std::filesystem::rename(part, file, ec);
    if (ec) { std::filesystem::remove(part, ec); failDownload("Could not save the model file"); return; }
    ModelMeta meta;
    meta.toneId = tone.id;
    meta.modelId = model.id;
    meta.title = tone.title;
    meta.authorName = tone.author.shownName();
    meta.authorVerified = tone.author.verified;
    meta.license = tone.license;
    meta.pageUrl = tone.url;
    meta.size = model.size;
    meta.architecture = model.architecture;
    meta.downloadedAt = isoUtc(be.nowUnix());
    writeMeta(file, meta);
    update([&](ClientState& s) { s.download = DlPhase::Done; s.downloadPath = utf8(file); });
  }

  ClientPaths paths;
  ClientBackends be;
  RateLimiter limiter;
  Tone3000Api api;
  StoredLogin login;

  mutable std::mutex m;
  std::condition_variable cv;
  std::deque<Command> queue;
  ClientState state;
  bool quit = false;
  std::atomic<bool> cancelFlag{false};
  std::thread worker;
};

Tone3000Client::Tone3000Client(ClientPaths paths, ClientBackends backends) : impl_(std::make_unique<Impl>(std::move(paths), std::move(backends))) {}
Tone3000Client::~Tone3000Client() = default;

ClientState Tone3000Client::snapshot() const {
  std::lock_guard<std::mutex> lk(impl_->m);
  return impl_->state;
}

void Tone3000Client::signIn(Prompt prompt) { Impl::Command c{Impl::Command::Kind::SignIn}; c.prompt = prompt; impl_->post(c); }
void Tone3000Client::signOut() { impl_->cancelFlag = true; impl_->post(Impl::Command{Impl::Command::Kind::SignOut}); }
void Tone3000Client::showTone(const std::string& toneId) { Impl::Command c{Impl::Command::Kind::ShowTone}; c.id = toneId; impl_->post(c); }
void Tone3000Client::showList(ListKind kind, int page) { Impl::Command c{Impl::Command::Kind::ShowList}; c.list = kind; c.page = page; impl_->post(c); }
void Tone3000Client::downloadModel(const std::string& modelId) { Impl::Command c{Impl::Command::Kind::Download}; c.id = modelId; impl_->post(c); }

void Tone3000Client::cancel() {
  {
    std::lock_guard<std::mutex> lk(impl_->m);
    impl_->queue.clear();
  }
  impl_->cancelFlag = true;
}

} // namespace FairyTapes::Tone3000
