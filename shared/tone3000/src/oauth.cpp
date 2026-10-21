// Fairy Tapes · shared TONE3000 client — OAuth 2.0 with PKCE (see oauth.h).
#include "tone3000/oauth.h"

#include "tone3000/api.h"
#include "tone3000/loopback.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace FairyTapes::Tone3000 {

namespace {
using Json = nlohmann::json;

bool printableToken(const std::string& s, std::size_t maxLen) {
  if (s.empty() || s.size() > maxLen) return false;
  for (unsigned char c : s)
    if (c <= 0x20 || c >= 0x7f) return false;
  return true;
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string form(std::initializer_list<std::pair<const char*, std::string>> fields) {
  std::string out;
  for (const auto& f : fields) {
    if (!out.empty()) out += '&';
    out += f.first;
    out += '=';
    out += percentEncode(f.second);
  }
  return out;
}

std::string oauthErrorOf(const std::string& json) {
  const Json j = Json::parse(json, nullptr, false);
  if (!j.is_object()) return {};
  const auto e = j.find("error");
  if (e == j.end() || !e->is_string()) return {};
  std::string s = e->get<std::string>();
  std::string out;
  for (char c : s)
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') out += c;
  return out.substr(0, 40);
}
} // namespace

std::string redirectUriFor(int port) { return "http://127.0.0.1:" + std::to_string(port) + kCallbackPath; }

std::string buildAuthorizeUrl(const OAuthConfig& cfg, const std::string& redirectUri, const std::string& challenge,
                              const std::string& state, Prompt prompt) {
  std::string url = cfg.base + "/oauth/authorize?" +
                    form({{"client_id", cfg.clientId}, {"redirect_uri", redirectUri}, {"response_type", "code"},
                          {"code_challenge", challenge}, {"code_challenge_method", "S256"}, {"state", state}});
  if (prompt == Prompt::SelectTone) url += "&prompt=select_tone";
  return url;
}

std::string queryValue(const std::string& target, const std::string& key) {
  const auto q = target.find('?');
  if (q == std::string::npos) return {};
  std::size_t pos = q + 1;
  while (pos <= target.size()) {
    const auto amp = target.find('&', pos);
    const std::string pair = target.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
    const auto eq = pair.find('=');
    const std::string name = pair.substr(0, eq);
    if (name == key) {
      std::string raw = eq == std::string::npos ? std::string() : pair.substr(eq + 1), out;
      for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '+') out += ' ';
        else if (raw[i] == '%' && i + 2 < raw.size() && hexVal(raw[i + 1]) >= 0 && hexVal(raw[i + 2]) >= 0) {
          out += static_cast<char>(hexVal(raw[i + 1]) * 16 + hexVal(raw[i + 2]));
          i += 2;
        } else out += raw[i];
      }
      return out;
    }
    if (amp == std::string::npos) break;
    pos = amp + 1;
  }
  return {};
}

CallbackResult parseCallback(const std::string& target, const std::string& expectedState, Prompt prompt) {
  CallbackResult r;
  if (target.substr(0, target.find('?')) != kCallbackPath) return r;
  const std::string state = queryValue(target, "state");
  if (state.empty() || expectedState.empty() || !sameSecret(state, expectedState)) return r;   // Invalid
  const std::string error = queryValue(target, "error");
  if (!error.empty()) {
    r.kind = CallbackResult::Kind::Error;
    for (char c : error)
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') r.error += c;
    r.error = r.error.substr(0, 40);
    return r;
  }
  if (queryValue(target, "canceled") == "true" || queryValue(target, "cancelled") == "true") { r.kind = CallbackResult::Kind::Cancelled; return r; }
  const std::string code = queryValue(target, "code");
  if (code.empty()) { r.kind = CallbackResult::Kind::Cancelled; return r; }   // the user closed the page before signing in
  if (!printableToken(code, 2048)) return r;                                    // Invalid
  if (prompt == Prompt::SelectTone) {
    const std::string tone = queryValue(target, "tone_id");
    if (!isSafeId(tone)) return r;                                              // Invalid: a tone was expected
    r.toneId = tone;
  }
  r.kind = CallbackResult::Kind::Ok;
  r.code = code;
  return r;
}

std::string tokenRequestBody(const OAuthConfig& cfg, const std::string& code, const std::string& verifier, const std::string& redirectUri) {
  return form({{"grant_type", "authorization_code"}, {"code", code}, {"code_verifier", verifier},
               {"redirect_uri", redirectUri}, {"client_id", cfg.clientId}});
}

std::string refreshRequestBody(const OAuthConfig& cfg, const std::string& refreshToken) {
  return form({{"grant_type", "refresh_token"}, {"refresh_token", refreshToken}, {"client_id", cfg.clientId}});
}

std::optional<Tokens> parseTokenResponse(const std::string& text) {
  if (text.size() > 64 * 1024) return std::nullopt;
  const Json j = Json::parse(text, nullptr, false);
  if (!j.is_object()) return std::nullopt;
  const auto a = j.find("access_token");
  if (a == j.end() || !a->is_string() || !printableToken(a->get<std::string>(), 4096)) return std::nullopt;
  Tokens t;
  t.access = a->get<std::string>();
  const auto r = j.find("refresh_token");
  if (r != j.end() && r->is_string() && printableToken(r->get<std::string>(), 4096)) t.refresh = r->get<std::string>();
  const auto e = j.find("expires_in");
  double ex = 3600;
  if (e != j.end() && e->is_number()) ex = e->get<double>();
  t.expiresInSeconds = std::clamp(ex, 60.0, 30.0 * 86400.0);
  return t;
}

std::string oauthErrorText(const std::string& e) {
  if (e == "access_denied") return "You did not allow the access";
  if (e == "invalid_grant") return "The login expired, sign in again";
  if (e == "invalid_client" || e == "unauthorized_client") return "TONE3000 does not know this app";
  if (e == "invalid_request" || e == "invalid_scope" || e == "unsupported_response_type") return "TONE3000 refused the login request";
  if (e == "temporarily_unavailable" || e == "server_error") return "TONE3000 is not answering";
  return "The login did not work";
}

bool openInSystemBrowser(const std::string& url) {
  static const std::string kPrefix = "https://www.tone3000.com/";
  if (url.compare(0, kPrefix.size(), kPrefix) != 0) return false;   // only our own login page, never an arbitrary URL
  for (unsigned char c : url)
    if (c <= 0x20 || c >= 0x7f) return false;
#ifdef _WIN32
  std::wstring w(url.begin(), url.end());
  return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
#ifdef __APPLE__
  const char* opener = "open";
#else
  const char* opener = "xdg-open";
#endif
  pid_t pid = 0;
  char* const argv[] = {const_cast<char*>(opener), const_cast<char*>(url.c_str()), nullptr};
  if (posix_spawnp(&pid, opener, nullptr, nullptr, argv, environ) != 0) return false;   // no shell involved
  std::thread([pid] { int st = 0; waitpid(pid, &st, 0); }).detach();                      // no zombie left behind
  return true;
#endif
}

OAuthFlow::OAuthFlow(OAuthConfig cfg, HttpClient& http, BrowserOpener opener, RandomBytes random)
: cfg_(std::move(cfg)), http_(http), opener_(std::move(opener)), random_(std::move(random)) {}

namespace {
OAuthOutcome tokensFrom(const HttpResponse& res) {
  OAuthOutcome out;
  if (res.cancelled) { out.cancelled = true; out.message = "Cancelled"; return out; }
  if (!res.error.empty()) { out.message = "Could not reach TONE3000"; return out; }
  if (!res.ok()) {
    const std::string e = oauthErrorOf(res.body);
    out.message = e.empty() ? errorForStatus(res.status).message : oauthErrorText(e);
    return out;
  }
  const auto t = parseTokenResponse(res.body);
  if (!t) { out.message = "Unexpected answer from TONE3000"; return out; }
  out.ok = true;
  out.tokens = *t;
  return out;
}

HttpRequest postForm(const std::string& url, const std::string& body) {
  HttpRequest req;
  req.url = url;
  req.method = "POST";
  req.body = body;
  req.headers = {{"Content-Type", "application/x-www-form-urlencoded"}, {"Accept", "application/json"}};
  return req;
}
} // namespace

OAuthOutcome OAuthFlow::run(Prompt prompt, double timeoutSeconds, const std::atomic<bool>* cancel) {
  OAuthOutcome out;
  if (cfg_.clientId.empty()) { out.message = "TONE3000 is not set up in this build"; return out; }
  const Pkce pkce = makePkce(random_);
  const std::string state = makeState(random_);
  if (!pkce.ok() || state.empty()) { out.message = "Could not create a secure login"; return out; }
  LoopbackServer server;
  if (!server.listening()) { out.message = "Could not open the local login receiver"; return out; }
  const std::string redirect = redirectUriFor(server.port());
  const std::string url = buildAuthorizeUrl(cfg_, redirect, pkce.challenge, state, prompt);
  if (!opener_ || !opener_(url)) { out.message = "Could not open your browser"; return out; }

  CallbackResult cb;
  const std::string target = server.waitForCallback(timeoutSeconds, cancel, [&](const std::string& t) {
    const CallbackResult c = parseCallback(t, state, prompt);
    if (c.kind == CallbackResult::Kind::Invalid) return false;   // wrong or missing state: as if it never came
    cb = c;
    return true;
  });
  if (target.empty()) {
    if (cancel && cancel->load()) { out.cancelled = true; out.message = "Cancelled"; }
    else out.message = "The login took too long";
    return out;
  }
  if (cb.kind == CallbackResult::Kind::Cancelled) { out.cancelled = true; out.message = "Cancelled"; return out; }
  if (cb.kind == CallbackResult::Kind::Error) { out.message = oauthErrorText(cb.error); return out; }

  out = tokensFrom(http_.perform(postForm(cfg_.base + "/oauth/token", tokenRequestBody(cfg_, cb.code, pkce.verifier, redirect)), {}, cancel));
  out.toneId = cb.toneId;
  return out;
}

OAuthOutcome OAuthFlow::refresh(const std::string& refreshToken, const std::atomic<bool>* cancel) {
  if (cfg_.clientId.empty() || refreshToken.empty()) {
    OAuthOutcome out;
    out.message = "Sign in to TONE3000 again";
    return out;
  }
  OAuthOutcome out = tokensFrom(http_.perform(postForm(cfg_.base + "/oauth/token", refreshRequestBody(cfg_, refreshToken)), {}, cancel));
  if (out.ok && out.tokens.refresh.empty()) out.tokens.refresh = refreshToken;   // the server kept the same one
  return out;
}

} // namespace FairyTapes::Tone3000
