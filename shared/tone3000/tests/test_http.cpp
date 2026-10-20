/** Fairy Tapes · shared TONE3000 client — HTTP policy (pure) and the libcurl client against a loopback test server. */
#include "tone3000/http.h"

#include <atomic>
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

void test_parse_url() {
  auto u = parseUrl("https://Tone3000.com/api/v1/tones?page=2");
  CHECK(u.valid && u.scheme == "https" && u.host == "tone3000.com" && u.port == 0 && u.rest == "/api/v1/tones?page=2");
  u = parseUrl("http://127.0.0.1:8123/cb?code=a");
  CHECK(u.valid && u.host == "127.0.0.1" && u.port == 8123 && u.rest == "/cb?code=a");
  u = parseUrl("https://example.com");
  CHECK(u.valid && u.rest == "/");
  u = parseUrl("https://[::1]:9/x");
  CHECK(u.valid && u.host == "::1" && u.port == 9);
  CHECK(!parseUrl("ftp://example.com/").valid);
  CHECK(!parseUrl("https://user:pw@example.com/").valid);     // credentials in the URL
  CHECK(!parseUrl("https://example.com/a b").valid);          // space
  CHECK(!parseUrl("https://example.com/a\r\nX: y").valid);    // header injection
  CHECK(!parseUrl("https://example.com:99999/").valid);
  CHECK(!parseUrl("https://example.com:abc/").valid);
  CHECK(!parseUrl("example.com/path").valid);
  CHECK(!parseUrl("").valid);
}

void test_redirect_policy() {
  const auto base = parseUrl("https://tone3000.com/api/v1/models/1");
  CHECK(redirectAllowed(base, parseUrl("https://tone3000.com/files/x.nam")));
  CHECK(redirectAllowed(base, parseUrl("https://TONE3000.com:443/files/x.nam")));       // explicit default port is the same
  CHECK(!redirectAllowed(base, parseUrl("https://cdn.tone3000.com/files/x.nam")));      // another host, even a subdomain
  CHECK(!redirectAllowed(base, parseUrl("https://evil.example/x")));
  CHECK(!redirectAllowed(base, parseUrl("http://tone3000.com/x")));                     // downgrade
  CHECK(!redirectAllowed(base, parseUrl("https://tone3000.com:8443/x")));               // another port
  CHECK(!redirectAllowed(base, parseUrl("not a url")));
  const auto loop = parseUrl("http://127.0.0.1:5000/a");
  CHECK(!redirectAllowed(loop, parseUrl("http://127.0.0.1:5000/b")));                   // plain http only on request
  CHECK(redirectAllowed(loop, parseUrl("http://127.0.0.1:5000/b"), true));
  CHECK(!redirectAllowed(loop, parseUrl("http://127.0.0.1:5001/b"), true));
  CHECK(!redirectAllowed(parseUrl("http://example.com/a"), parseUrl("http://example.com/b"), true));   // not a loopback host
  CHECK(isLoopbackHost("localhost") && isLoopbackHost("127.0.0.1") && isLoopbackHost("::1") && !isLoopbackHost("tone3000.com"));
}

void test_redirect_decision() {
  const auto api = parseUrl("https://www.tone3000.com/api/v1/models/1");
  const auto same = parseUrl("https://www.tone3000.com/files/a.nam");
  const auto other = parseUrl("https://files.example.org/a.nam");
  CHECK(decideRedirect(api, same, false, false) == RedirectDecision::FollowKeepAuth);
  CHECK(decideRedirect(api, same, false, true) == RedirectDecision::FollowKeepAuth);        // same host: still with the token
  CHECK(decideRedirect(api, other, false, false) == RedirectDecision::Refuse);              // refused unless asked
  CHECK(decideRedirect(api, other, false, true) == RedirectDecision::FollowDropAuth);       // followed, but without the token
  CHECK(decideRedirect(api, parseUrl("http://files.example.org/a.nam"), false, true) == RedirectDecision::Refuse);   // never to plain http
  CHECK(decideRedirect(api, parseUrl("https://user@files.example.org/a"), false, true) == RedirectDecision::Refuse);
  CHECK(decideRedirect(api, parseUrl("garbage"), false, true) == RedirectDecision::Refuse);
}

void test_resolve_location() {
  const auto base = parseUrl("https://tone3000.com:8443/api/x");
  CHECK(resolveLocation(base, "https://tone3000.com/y") == "https://tone3000.com/y");
  CHECK(resolveLocation(base, "/files/y.nam") == "https://tone3000.com:8443/files/y.nam");
  CHECK(resolveLocation(base, "relative/y").empty());
  CHECK(resolveLocation(base, "//evil.example/y").empty());
  CHECK(resolveLocation(base, "").empty());
}

#ifndef _WIN32
// A tiny HTTP/1.1 server on 127.0.0.1 for the client tests. One request per connection.
class TestServer {
public:
  TestServer() {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    ok_ = ::bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 && ::listen(fd_, 8) == 0;
    socklen_t len = sizeof a;
    ::getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &len);
    port_ = ntohs(a.sin_port);
    if (ok_) thread_ = std::thread([this] { loop(); });
  }
  ~TestServer() {
    stop_ = true;
    if (fd_ >= 0) { ::shutdown(fd_, SHUT_RDWR); ::close(fd_); }
    if (thread_.joinable()) thread_.join();
  }
  bool ok() const { return ok_; }
  std::string url(const std::string& path) const { return "http://127.0.0.1:" + std::to_string(port_) + path; }
  std::string lastRequest() { return lastRequest_; }

private:
  void loop() {
    while (!stop_) {
      const int c = ::accept(fd_, nullptr, nullptr);
      if (c < 0) break;
      handle(c);
      ::close(c);
    }
  }
  static void sendAll(int c, const std::string& s) {
    std::size_t off = 0;
    while (off < s.size()) {
      const ssize_t n = ::send(c, s.data() + off, s.size() - off, MSG_NOSIGNAL);
      if (n <= 0) return;
      off += static_cast<std::size_t>(n);
    }
  }
  void handle(int c) {
    std::string req;
    char buf[2048];
    while (req.find("\r\n\r\n") == std::string::npos) {
      const ssize_t n = ::recv(c, buf, sizeof buf, 0);
      if (n <= 0) return;
      req.append(buf, static_cast<std::size_t>(n));
    }
    std::size_t need = 0;
    const auto cl = req.find("Content-Length: ");
    if (cl != std::string::npos) need = static_cast<std::size_t>(std::stoul(req.substr(cl + 16)));
    while (req.size() < req.find("\r\n\r\n") + 4 + need) {
      const ssize_t n = ::recv(c, buf, sizeof buf, 0);
      if (n <= 0) break;
      req.append(buf, static_cast<std::size_t>(n));
    }
    lastRequest_ = req;
    const std::string path = req.substr(req.find(' ') + 1, req.find(' ', req.find(' ') + 1) - req.find(' ') - 1);
    auto reply = [&](int code, const std::string& body, const std::string& extra = {}) {
      sendAll(c, "HTTP/1.1 " + std::to_string(code) + " X\r\nContent-Length: " + std::to_string(body.size()) +
                     "\r\nConnection: close\r\n" + extra + "\r\n" + body);
    };
    if (path == "/ok") reply(200, "hello");
    else if (path == "/missing") reply(404, "no");
    else if (path == "/echo") reply(200, req);
    else if (path == "/redir-same") reply(302, "moved", "Location: /ok\r\n");
    else if (path == "/redir-other") reply(302, "moved", "Location: http://example.invalid/x\r\n");
    else if (path == "/redir-echo") reply(302, "moved", "Location: /echo\r\n");
    else if (path == "/redir-localhost") reply(302, "moved", "Location: http://localhost:" + std::to_string(port_) + "/echo\r\n");
    else if (path == "/redir-loop") reply(302, "moved", "Location: /redir-loop\r\n");
    else if (path == "/big") reply(200, std::string(100000, 'x'));
    else if (path == "/slow") { ::usleep(1500000); reply(200, "late"); }
    else reply(500, "?");
  }
  int fd_ = -1;
  int port_ = 0;
  bool ok_ = false;
  std::atomic<bool> stop_{false};
  std::thread thread_;
  std::string lastRequest_;
};

HttpRequest req(const TestServer& s, const std::string& path) {
  HttpRequest r;
  r.url = s.url(path);
  r.allowLoopbackHttp = true;
  r.timeoutSeconds = 5;
  return r;
}

void test_client_basic() {
  TestServer srv;
  CHECK(srv.ok());
  CurlHttpClient c;
  auto r = c.perform(req(srv, "/ok"));
  CHECK(r.ok() && r.status == 200 && r.body == "hello");
  r = c.perform(req(srv, "/missing"));
  CHECK(r.error.empty() && r.status == 404 && !r.ok());     // a 404 is an answer, not a transport error
}

void test_client_headers_and_post() {
  TestServer srv;
  CurlHttpClient c;
  auto q = req(srv, "/echo");
  q.method = "POST";
  q.body = "grant_type=refresh_token";
  q.headers = {{"Authorization", "Bearer abc123"}, {"Content-Type", "application/x-www-form-urlencoded"}};
  auto r = c.perform(q);
  CHECK(r.ok());
  CHECK(r.body.find("POST /echo") == 0);
  CHECK(r.body.find("Authorization: Bearer abc123") != std::string::npos);
  CHECK(r.body.find("grant_type=refresh_token") != std::string::npos);
  CHECK(r.body.find("User-Agent: FairyTapes/") != std::string::npos);
}

void test_client_redirects() {
  TestServer srv;
  CurlHttpClient c;
  auto r = c.perform(req(srv, "/redir-same"));
  CHECK(r.ok() && r.body == "hello");                         // a redirect on the same host is followed (by hand)
  r = c.perform(req(srv, "/redir-other"));
  CHECK(!r.ok() && r.error.find("refused") != std::string::npos);
  r = c.perform(req(srv, "/redir-loop"));
  CHECK(!r.ok() && r.error.find("too many") != std::string::npos);
}

void test_client_token_stays_on_its_host() {
  TestServer srv;
  CurlHttpClient c;
  auto q = req(srv, "/redir-echo");
  q.headers = {{"Authorization", "Bearer secret-token"}};
  auto r = c.perform(q);
  CHECK(r.ok() && r.body.find("Authorization: Bearer secret-token") != std::string::npos);   // same host: the token goes along
  q = req(srv, "/redir-localhost");                                                           // "localhost" is another host than 127.0.0.1
  q.headers = {{"Authorization", "Bearer secret-token"}, {"Cookie", "a=b"}, {"X-Other", "kept"}};
  r = c.perform(q);
  CHECK(!r.ok() && r.error.find("refused") != std::string::npos);                             // refused unless the caller allows it
  q.allowOtherHostRedirects = true;
  r = c.perform(q);
  CHECK(r.ok() && r.body.find("GET /echo") == 0);
  CHECK(r.body.find("secret-token") == std::string::npos && r.body.find("Authorization") == std::string::npos);   // dropped
  CHECK(r.body.find("Cookie") == std::string::npos);
  CHECK(r.body.find("X-Other: kept") != std::string::npos);                                   // other headers are not touched
}

void test_client_refuses_plain_http_without_flag() {
  TestServer srv;
  CurlHttpClient c;
  HttpRequest q;
  q.url = srv.url("/ok");        // allowLoopbackHttp stays false
  auto r = c.perform(q);
  CHECK(!r.ok() && r.status == 0 && r.error.find("plain http") != std::string::npos);
  q.url = "http://example.com/";  // never, even with the flag: not loopback
  q.allowLoopbackHttp = true;
  r = c.perform(q);
  CHECK(!r.ok() && r.error.find("plain http") != std::string::npos);
}

void test_client_limits_sink_and_cancel() {
  TestServer srv;
  CurlHttpClient c;
  auto q = req(srv, "/big");
  q.maxBodyBytes = 1000;
  auto r = c.perform(q);
  CHECK(!r.ok() && r.error.find("too large") != std::string::npos);
  q.maxBodyBytes = 1 << 20;
  std::size_t total = 0;
  r = c.perform(q, [&](const char*, std::size_t n) { total += n; return true; });
  CHECK(r.ok() && total == 100000 && r.body.empty());          // the sink took the body
  r = c.perform(q, [&](const char*, std::size_t) { return false; });
  CHECK(!r.ok() && r.error.find("refused") != std::string::npos);
  std::atomic<bool> cancel{false};
  std::thread t([&] { ::usleep(300000); cancel = true; });
  r = c.perform(req(srv, "/slow"), {}, &cancel);
  t.join();
  CHECK(r.cancelled && !r.ok());
}
#endif
} // namespace

int main() {
  std::puts("tone3000 http tests");
  test_parse_url();
  test_redirect_policy();
  test_redirect_decision();
  test_resolve_location();
#ifndef _WIN32
  test_client_basic();
  test_client_headers_and_post();
  test_client_redirects();
  test_client_token_stays_on_its_host();
  test_client_refuses_plain_http_without_flag();
  test_client_limits_sink_and_cancel();
#endif
  if (g_failed) { std::fprintf(stderr, "FAILED (%d)\n", g_failed); return 1; }
  std::puts("All tone3000 http tests passed");
  return 0;
}
