/** Fairy Tapes · shared TONE3000 client — OAuth/PKCE: pure parts, the loopback receiver on a real socket, the whole flow. */
#include "tone3000/loopback.h"
#include "tone3000/oauth.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace FairyTapes::Tone3000;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

OAuthConfig cfg() { OAuthConfig c; c.clientId = "pk_test_123"; return c; }

void test_authorize_url() {
  const std::string u = buildAuthorizeUrl(cfg(), "http://127.0.0.1:5555/callback", "CHAL-lenge_1", "ST8", Prompt::SelectTone);
  CHECK(u.rfind("https://www.tone3000.com/api/v1/oauth/authorize?", 0) == 0);
  CHECK(u.find("client_id=pk_test_123") != std::string::npos);
  CHECK(u.find("redirect_uri=http%3A%2F%2F127.0.0.1%3A5555%2Fcallback") != std::string::npos);   // encoded
  CHECK(u.find("response_type=code") != std::string::npos && u.find("code_challenge=CHAL-lenge_1") != std::string::npos);
  CHECK(u.find("code_challenge_method=S256") != std::string::npos && u.find("state=ST8") != std::string::npos);
  CHECK(u.find("prompt=select_tone") != std::string::npos);
  CHECK(buildAuthorizeUrl(cfg(), "r", "c", "s", Prompt::Login).find("prompt=") == std::string::npos);
  CHECK(buildAuthorizeUrl(cfg(), "r", "c", "s&evil=1", Prompt::Login).find("evil=1") == std::string::npos);   // values are encoded
  CHECK(redirectUriFor(4242) == "http://127.0.0.1:4242/callback");
}

void test_query_values() {
  const std::string t = "/callback?code=a%2Bb+c&state=S%41&empty=&flag";
  CHECK(queryValue(t, "code") == "a+b c" && queryValue(t, "state") == "SA");
  CHECK(queryValue(t, "empty").empty() && queryValue(t, "flag").empty() && queryValue(t, "missing").empty());
  CHECK(queryValue("/callback", "code").empty() && queryValue("/callback?%zz=1", "x").empty());
  CHECK(queryValue("/callback?a=%4", "a") == "%4");   // a broken escape is left as it is, never read past the end
}

void test_callback_parsing() {
  const auto ok = parseCallback("/callback?code=XYZ&state=S1&tone_id=42", "S1", Prompt::SelectTone);
  CHECK(ok.kind == CallbackResult::Kind::Ok && ok.code == "XYZ" && ok.toneId == "42");
  const auto login = parseCallback("/callback?code=XYZ&state=S1", "S1", Prompt::Login);
  CHECK(login.kind == CallbackResult::Kind::Ok && login.toneId.empty());
  CHECK(parseCallback("/callback?code=XYZ&state=S1", "S1", Prompt::SelectTone).kind == CallbackResult::Kind::Invalid);   // a tone was expected
  CHECK(parseCallback("/callback?code=XYZ&state=S1&tone_id=../x", "S1", Prompt::SelectTone).kind == CallbackResult::Kind::Invalid);
  // the state decides everything: a wrong, missing or empty one is Invalid even with an error or a cancellation
  CHECK(parseCallback("/callback?code=XYZ&state=OTHER", "S1", Prompt::Login).kind == CallbackResult::Kind::Invalid);
  CHECK(parseCallback("/callback?code=XYZ", "S1", Prompt::Login).kind == CallbackResult::Kind::Invalid);
  CHECK(parseCallback("/callback?code=XYZ&state=", "", Prompt::Login).kind == CallbackResult::Kind::Invalid);
  CHECK(parseCallback("/callback?error=access_denied&state=OTHER", "S1", Prompt::Login).kind == CallbackResult::Kind::Invalid);
  CHECK(parseCallback("/callback?canceled=true&state=OTHER", "S1", Prompt::Login).kind == CallbackResult::Kind::Invalid);
  const auto err = parseCallback("/callback?error=access_denied&state=S1", "S1", Prompt::Login);
  CHECK(err.kind == CallbackResult::Kind::Error && err.error == "access_denied");
  const auto evil = parseCallback("/callback?error=%3Cscript%3Ex&state=S1", "S1", Prompt::Login);
  CHECK(evil.kind == CallbackResult::Kind::Error && evil.error == "scriptx");                  // reduced to letters, digits, "_"
  CHECK(parseCallback("/callback?canceled=true&state=S1", "S1", Prompt::SelectTone).kind == CallbackResult::Kind::Cancelled);
  CHECK(parseCallback("/callback?state=S1", "S1", Prompt::Login).kind == CallbackResult::Kind::Cancelled);   // closed before signing in
  CHECK(parseCallback("/elsewhere?code=XYZ&state=S1", "S1", Prompt::Login).kind == CallbackResult::Kind::Invalid);
  CHECK(parseCallback("/callback?code=" + std::string(3000, 'a') + "&state=S1", "S1", Prompt::Login).kind == CallbackResult::Kind::Invalid);
  CHECK(parseCallback("/callback?code=a%20b&state=S1", "S1", Prompt::Login).kind == CallbackResult::Kind::Invalid);   // a code has no spaces
}

void test_token_bodies_and_response() {
  const std::string b = tokenRequestBody(cfg(), "CODE/1", "VERIF-ier_9", "http://127.0.0.1:1/callback");
  CHECK(b.find("grant_type=authorization_code") == 0);
  CHECK(b.find("code=CODE%2F1") != std::string::npos && b.find("code_verifier=VERIF-ier_9") != std::string::npos);
  CHECK(b.find("client_id=pk_test_123") != std::string::npos && b.find("redirect_uri=http%3A%2F%2F127.0.0.1%3A1%2Fcallback") != std::string::npos);
  CHECK(b.find("secret") == std::string::npos);                                               // no secret exists in the client
  CHECK(refreshRequestBody(cfg(), "RT+1") == "grant_type=refresh_token&refresh_token=RT%2B1&client_id=pk_test_123");
  auto t = parseTokenResponse("{\"access_token\":\"AT\",\"refresh_token\":\"RT\",\"token_type\":\"bearer\",\"expires_in\":3600}");
  CHECK(t && t->access == "AT" && t->refresh == "RT" && t->expiresInSeconds == 3600 && t->ok());
  t = parseTokenResponse("{\"access_token\":\"AT\"}");
  CHECK(t && t->refresh.empty() && t->expiresInSeconds == 3600);
  t = parseTokenResponse("{\"access_token\":\"AT\",\"expires_in\":5}");
  CHECK(t && t->expiresInSeconds == 60);                                                      // clamped
  t = parseTokenResponse("{\"access_token\":\"AT\",\"expires_in\":99999999999}");
  CHECK(t && t->expiresInSeconds == 30.0 * 86400.0);
  CHECK(!parseTokenResponse("{}").has_value() && !parseTokenResponse("nope").has_value() && !parseTokenResponse("[]").has_value());
  CHECK(!parseTokenResponse("{\"access_token\":\"has space\"}").has_value() && !parseTokenResponse("{\"access_token\":\"\"}").has_value());
  CHECK(!parseTokenResponse("{\"access_token\":5}").has_value());
  CHECK(oauthErrorText("access_denied") != oauthErrorText("invalid_grant") && !oauthErrorText("whatever").empty());
}

void test_request_parsing() {
  auto r = parseLoopbackRequest("GET /callback?code=1&state=2 HTTP/1.1\r\nHost: 127.0.0.1:5000\r\nAccept: */*\r\n\r\n");
  CHECK(r.complete && !r.malformed && r.method == "GET" && r.target == "/callback?code=1&state=2" && r.host == "127.0.0.1:5000");
  r = parseLoopbackRequest("GET /callback HTTP/1.1\r\nhOsT:   127.0.0.1:5000  \r\n\r\n");
  CHECK(r.host == "127.0.0.1:5000");                                                          // case-insensitive name, trimmed value
  CHECK(!parseLoopbackRequest("GET /callback HTTP/1.1\r\nHost: x").complete);                 // headers not finished yet
  CHECK(parseLoopbackRequest("GARBAGE\r\n\r\n").malformed);
  CHECK(parseLoopbackRequest("GET /a HTTP/9.9\r\n\r\n").malformed);
  CHECK(parseLoopbackRequest("GET a HTTP/1.1\r\n\r\n").malformed);                            // not an origin-form target
  CHECK(parseLoopbackRequest("GET /a\x01 HTTP/1.1\r\n\r\n").malformed);
  CHECK(parseLoopbackRequest("GET /" + std::string(3000, 'a') + " HTTP/1.1\r\n\r\n").malformed);
}

#ifndef _WIN32
// A client that behaves like the browser: connects to the loopback port and sends one request.
std::string request(int port, const std::string& raw, bool* connected = nullptr) {
  const int s = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.sin_port = htons(static_cast<uint16_t>(port));
  const bool ok = ::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0;
  if (connected) *connected = ok;
  std::string answer;
  if (ok) {
    ::send(s, raw.data(), raw.size(), MSG_NOSIGNAL);
    char buf[2048];
    ssize_t n;
    while ((n = ::recv(s, buf, sizeof buf, 0)) > 0) answer.append(buf, static_cast<std::size_t>(n));
  }
  ::close(s);
  return answer;
}
std::string get(int port, const std::string& target, const std::string& host = {}) {
  return request(port, "GET " + target + " HTTP/1.1\r\nHost: " + (host.empty() ? "127.0.0.1:" + std::to_string(port) : host) + "\r\n\r\n");
}

void test_loopback_receives_the_callback() {
  LoopbackServer srv;
  CHECK(srv.listening() && srv.port() > 0);
  std::string answer, other;
  std::thread browser([&] {
    other = get(srv.port(), "/favicon.ico");                                   // browsers ask for this: a 404, the wait goes on
    answer = get(srv.port(), "/callback?code=C&state=S");
  });
  const std::string target = srv.waitForCallback(5.0, nullptr);
  browser.join();
  CHECK(target == "/callback?code=C&state=S");
  CHECK(other.find("404") != std::string::npos);
  CHECK(answer.find("200 OK") != std::string::npos && answer.find("You can close this tab") != std::string::npos);
  CHECK(answer.find("Cache-Control: no-store") != std::string::npos && answer.find("nosniff") != std::string::npos);
  CHECK(!srv.listening());                                                     // closed once it has its answer
  bool connected = true;
  request(srv.port(), "x", &connected);
  CHECK(!connected);                                                           // nobody listens any more
}

void test_loopback_refuses_foreign_requests() {
  LoopbackServer srv;
  std::string a1, a2, a3, a4;
  std::thread browser([&] {
    a1 = get(srv.port(), "/callback?code=C&state=S", "evil.example:80");       // wrong Host header (DNS rebinding)
    a2 = request(srv.port(), "POST /callback HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(srv.port()) + "\r\n\r\n");
    a3 = request(srv.port(), "not http at all\r\n\r\n");
    a4 = get(srv.port(), "/callback?code=C&state=WRONG");                      // refused by the caller's check
    get(srv.port(), "/callback?code=C&state=S");
  });
  const std::string target = srv.waitForCallback(5.0, nullptr, [](const std::string& t) { return t.find("state=S") != std::string::npos && t.find("WRONG") == std::string::npos; });
  browser.join();
  CHECK(target == "/callback?code=C&state=S");
  CHECK(a1.find("400") != std::string::npos && a2.find("405") != std::string::npos && a3.find("400") != std::string::npos && a4.find("400") != std::string::npos);
}

void test_loopback_timeout_and_cancel() {
  {
    LoopbackServer srv;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(srv.waitForCallback(0.4, nullptr).empty());
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));
    CHECK(!srv.listening());
  }
  {
    LoopbackServer srv;
    std::atomic<bool> cancel{false};
    std::thread t([&] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); cancel = true; });
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(srv.waitForCallback(30.0, &cancel).empty());
    t.join();
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(5));
  }
}

struct FakeHttp : HttpClient {
  std::vector<HttpRequest> seen;
  HttpResponse next;
  HttpResponse perform(const HttpRequest& r, const HttpSink&, const std::atomic<bool>*) override {
    seen.push_back(r);
    return next;
  }
};

HttpResponse okTokens() {
  HttpResponse r;
  r.status = 200;
  r.body = "{\"access_token\":\"AT1\",\"refresh_token\":\"RT1\",\"token_type\":\"bearer\",\"expires_in\":7200}";
  return r;
}

// The fake browser: reads the authorize URL, then answers like the website would (from another thread, after a moment).
struct FakeBrowser {
  std::string url;
  std::string answerTemplate;   // what comes back; {STATE} is replaced with the state of the URL
  const std::atomic<bool>* after = nullptr;   // when set, the answer waits until this turns true
  std::thread worker;
  bool operator()(const std::string& u) {
    url = u;
    worker = std::thread([this, u] {
      const std::string state = queryValue("/x?" + u.substr(u.find('?') + 1), "state");
      const std::string redirect = queryValue("/x?" + u.substr(u.find('?') + 1), "redirect_uri");
      const int port = std::stoi(redirect.substr(redirect.rfind(':') + 1));
      std::string target = answerTemplate;
      for (std::size_t p; (p = target.find("{STATE}")) != std::string::npos;) target.replace(p, 7, state);
      while (after && !after->load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
      get(port, target);
    });
    return true;
  }
  ~FakeBrowser() { if (worker.joinable()) worker.join(); }
};

void test_full_flow_select_tone() {
  FakeHttp http;
  http.next = okTokens();
  FakeBrowser browser;
  browser.answerTemplate = "/callback?code=AUTHCODE&state={STATE}&tone_id=42";
  OAuthFlow flow(cfg(), http, [&](const std::string& u) { return browser(u); });
  const OAuthOutcome out = flow.run(Prompt::SelectTone, 10.0, nullptr);
  CHECK(out.ok && out.tokens.access == "AT1" && out.tokens.refresh == "RT1" && out.toneId == "42");
  CHECK(browser.url.find("prompt=select_tone") != std::string::npos && browser.url.find("code_challenge_method=S256") != std::string::npos);
  CHECK(http.seen.size() == 1 && http.seen[0].method == "POST" && http.seen[0].url == "https://www.tone3000.com/api/v1/oauth/token");
  // the verifier sent to the token endpoint must be the one the challenge in the browser URL was made from
  const std::string body = http.seen[0].body;
  const std::string verifier = queryValue("/x?" + body, "code_verifier");
  CHECK(!verifier.empty() && challengeFor(verifier) == queryValue("/x?" + browser.url.substr(browser.url.find('?') + 1), "code_challenge"));
  CHECK(queryValue("/x?" + body, "code") == "AUTHCODE" && queryValue("/x?" + body, "grant_type") == "authorization_code");
  CHECK(queryValue("/x?" + body, "redirect_uri") == queryValue("/x?" + browser.url.substr(browser.url.find('?') + 1), "redirect_uri"));
}

void test_flow_ignores_a_wrong_state_and_goes_on() {
  FakeHttp http;
  http.next = okTokens();
  FakeBrowser browser;
  browser.answerTemplate = "/callback?code=AUTHCODE&state={STATE}";
  bool bogusRefused = false;
  std::atomic<bool> bogusDone{false};
  browser.after = &bogusDone;                    // the real answer comes only after the forged one was dealt with
  std::thread bogus;
  OAuthFlow flow(cfg(), http, [&](const std::string& u) {
    // a local program that sends a callback with a made-up state before the real browser answers (the server only
    // answers once the flow is waiting, so this runs on its own thread)
    bogus = std::thread([&, u] {
      const std::string redirect = queryValue("/x?" + u.substr(u.find('?') + 1), "redirect_uri");
      const int port = std::stoi(redirect.substr(redirect.rfind(':') + 1));
      bogusRefused = get(port, "/callback?code=EVIL&state=guess").find("400") != std::string::npos;
      bogusDone = true;
    });
    return browser(u);
  });
  const OAuthOutcome out = flow.run(Prompt::Login, 10.0, nullptr);
  bogus.join();
  CHECK(bogusRefused && out.ok);
  CHECK(queryValue("/x?" + http.seen[0].body, "code") == "AUTHCODE");          // the forged code never reached the token endpoint
}

void test_flow_outcomes() {
  {
    FakeHttp http;
    FakeBrowser browser;
    browser.answerTemplate = "/callback?error=access_denied&state={STATE}";
    OAuthFlow flow(cfg(), http, [&](const std::string& u) { return browser(u); });
    const auto out = flow.run(Prompt::Login, 10.0, nullptr);
    CHECK(!out.ok && !out.cancelled && out.message == oauthErrorText("access_denied") && http.seen.empty());
  }
  {
    FakeHttp http;
    FakeBrowser browser;
    browser.answerTemplate = "/callback?canceled=true&state={STATE}";
    OAuthFlow flow(cfg(), http, [&](const std::string& u) { return browser(u); });
    const auto out = flow.run(Prompt::SelectTone, 10.0, nullptr);
    CHECK(!out.ok && out.cancelled && http.seen.empty());
  }
  {
    FakeHttp http;
    OAuthFlow flow(cfg(), http, [](const std::string&) { return false; });       // no browser could be opened
    const auto out = flow.run(Prompt::Login, 10.0, nullptr);
    CHECK(!out.ok && out.message == "Could not open your browser");
  }
  {
    FakeHttp http;
    OAuthFlow flow(cfg(), http, [](const std::string&) { return true; });        // the user never answers
    const auto out = flow.run(Prompt::Login, 0.4, nullptr);
    CHECK(!out.ok && !out.cancelled && out.message == "The login took too long");
  }
  {
    FakeHttp http;
    OAuthConfig none;                                                            // not configured: the app is not registered yet
    OAuthFlow flow(none, http, [](const std::string&) { return true; });
    CHECK(flow.run(Prompt::Login, 1.0, nullptr).message == "TONE3000 is not set up in this build");
  }
  {
    FakeHttp http;
    OAuthFlow flow(cfg(), http, [](const std::string&) { return true; }, [](std::uint8_t*, std::size_t) { return false; });
    CHECK(flow.run(Prompt::Login, 1.0, nullptr).message == "Could not create a secure login");   // no random, no login
  }
  {
    FakeHttp http;                                                               // the token endpoint refuses the code
    http.next.status = 400;
    http.next.body = "{\"error\":\"invalid_grant\"}";
    FakeBrowser browser;
    browser.answerTemplate = "/callback?code=OLD&state={STATE}";
    OAuthFlow flow(cfg(), http, [&](const std::string& u) { return browser(u); });
    const auto out = flow.run(Prompt::Login, 10.0, nullptr);
    CHECK(!out.ok && out.message == oauthErrorText("invalid_grant"));
  }
}

void test_refresh() {
  FakeHttp http;
  http.next.status = 200;
  http.next.body = "{\"access_token\":\"AT2\",\"expires_in\":3600}";
  OAuthFlow flow(cfg(), http);
  auto out = flow.refresh("RT1", nullptr);
  CHECK(out.ok && out.tokens.access == "AT2" && out.tokens.refresh == "RT1");     // no new refresh token: keep the old one
  CHECK(queryValue("/x?" + http.seen[0].body, "grant_type") == "refresh_token" && queryValue("/x?" + http.seen[0].body, "refresh_token") == "RT1");
  http.next.status = 400;
  http.next.body = "{\"error\":\"invalid_grant\"}";
  out = flow.refresh("RT1", nullptr);
  CHECK(!out.ok && out.message == oauthErrorText("invalid_grant"));
  CHECK(!flow.refresh("", nullptr).ok);
  const size_t n = http.seen.size();
  CHECK(!flow.refresh("", nullptr).ok && http.seen.size() == n);                  // nothing sent without a refresh token
}

void test_browser_opener_only_opens_our_login_page() {
  CHECK(!openInSystemBrowser("file:///etc/passwd"));
  CHECK(!openInSystemBrowser("http://www.tone3000.com/api/v1/oauth/authorize"));
  CHECK(!openInSystemBrowser("https://evil.example/https://www.tone3000.com/"));
  CHECK(!openInSystemBrowser("https://www.tone3000.com/x y"));
  CHECK(!openInSystemBrowser("https://www.tone3000.com/\nrm -rf"));
  CHECK(!openInSystemBrowser("javascript:alert(1)"));
}
#endif
} // namespace

#define RUN(t) do { std::fprintf(stderr, "  %s\n", #t); t(); } while (0)

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::puts("tone3000 oauth tests");
  RUN(test_authorize_url);
  RUN(test_query_values);
  RUN(test_callback_parsing);
  RUN(test_token_bodies_and_response);
  RUN(test_request_parsing);
#ifndef _WIN32
  RUN(test_loopback_receives_the_callback);
  RUN(test_loopback_refuses_foreign_requests);
  RUN(test_loopback_timeout_and_cancel);
  RUN(test_full_flow_select_tone);
  RUN(test_flow_ignores_a_wrong_state_and_goes_on);
  RUN(test_flow_outcomes);
  RUN(test_refresh);
  RUN(test_browser_opener_only_opens_our_login_page);
#endif
  if (g_failed) { std::fprintf(stderr, "FAILED (%d)\n", g_failed); return 1; }
  std::puts("All tone3000 oauth tests passed");
  return 0;
}
