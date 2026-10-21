// Fairy Tapes · shared TONE3000 client — the loopback receiver of the OAuth redirect (see loopback.h).
#include "tone3000/loopback.h"

#include <algorithm>
#include <cctype>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SockLen = int;
static void closeSock(std::intptr_t s) { closesocket(static_cast<SOCKET>(s)); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using SockLen = socklen_t;
static void closeSock(std::intptr_t s) { ::close(static_cast<int>(s)); }
#endif

namespace FairyTapes::Tone3000 {

namespace {
constexpr std::size_t kMaxRequestBytes = 8192;
constexpr std::size_t kMaxTarget = 2048;
constexpr int kMaxBadRequests = 200;

std::string lowerCase(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

const char kPage[] =
    "<!doctype html><html><head><meta charset=\"utf-8\"><title>Fairy Tapes</title>"
    "<style>body{font-family:sans-serif;background:#161617;color:#eee;text-align:center;padding:4em 1em}</style></head>"
    "<body><h2>You can close this tab</h2><p>and go back to the plug-in.</p></body></html>";

void sendAll(std::intptr_t s, const std::string& data) {
  std::size_t off = 0;
  while (off < data.size()) {
#ifdef _WIN32
    const int n = ::send(static_cast<SOCKET>(s), data.data() + off, static_cast<int>(data.size() - off), 0);
#else
    const ssize_t n = ::send(static_cast<int>(s), data.data() + off, data.size() - off, MSG_NOSIGNAL);
#endif
    if (n <= 0) return;
    off += static_cast<std::size_t>(n);
  }
}

void reply(std::intptr_t s, int code, const char* text, const std::string& body, const char* type = "text/plain; charset=utf-8") {
  sendAll(s, "HTTP/1.1 " + std::to_string(code) + " " + text + "\r\nContent-Type: " + type +
                 "\r\nContent-Length: " + std::to_string(body.size()) +
                 "\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff"
                 "\r\nConnection: close\r\n\r\n" + body);
}
} // namespace

LoopbackRequest parseLoopbackRequest(const std::string& raw) {
  LoopbackRequest r;
  const auto end = raw.find("\r\n\r\n");
  if (end == std::string::npos) return r;
  r.complete = true;
  const auto eol = raw.find("\r\n");
  const std::string line = raw.substr(0, eol);
  const auto sp1 = line.find(' ');
  const auto sp2 = sp1 == std::string::npos ? std::string::npos : line.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) { r.malformed = true; return r; }
  r.method = line.substr(0, sp1);
  r.target = line.substr(sp1 + 1, sp2 - sp1 - 1);
  const std::string version = line.substr(sp2 + 1);
  if (version != "HTTP/1.1" && version != "HTTP/1.0") { r.malformed = true; return r; }
  if (r.target.empty() || r.target[0] != '/' || r.target.size() > kMaxTarget) { r.malformed = true; return r; }
  for (unsigned char c : r.target)
    if (c <= 0x20 || c == 0x7f) { r.malformed = true; return r; }
  // headers: only Host matters
  std::size_t pos = eol + 2;
  while (pos < end) {
    const auto next = raw.find("\r\n", pos);
    const std::string h = raw.substr(pos, next - pos);
    const auto colon = h.find(':');
    if (colon != std::string::npos && lowerCase(h.substr(0, colon)) == "host") {
      std::string v = h.substr(colon + 1);
      const auto a = v.find_first_not_of(" \t");
      const auto b = v.find_last_not_of(" \t");
      r.host = a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
    }
    pos = next + 2;
  }
  return r;
}

LoopbackServer::LoopbackServer() {
#ifdef _WIN32
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
  if (s == INVALID_SOCKET) return;
#else
  const int s = ::socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) return;
#endif
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // never any other interface
  a.sin_port = 0;
  if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(s, 8) != 0) {
    closeSock(static_cast<std::intptr_t>(s));
    return;
  }
  SockLen len = sizeof a;
  ::getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
  port_ = ntohs(a.sin_port);
  fd_ = static_cast<std::intptr_t>(s);
}

LoopbackServer::~LoopbackServer() { close(); }

void LoopbackServer::close() {
  if (fd_ >= 0) {
    closeSock(fd_);
    fd_ = -1;
#ifdef _WIN32
    WSACleanup();
#endif
  }
}

std::string LoopbackServer::waitForCallback(double timeoutSeconds, const std::atomic<bool>* cancel,
                                            const std::function<bool(const std::string&)>& accept) {
  using Clock = std::chrono::steady_clock;
  const auto deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(std::max(0.0, timeoutSeconds)));
  const std::string expectedHost = "127.0.0.1:" + std::to_string(port_);
  int bad = 0;
  std::string result;
  while (fd_ >= 0 && !(cancel && cancel->load()) && Clock::now() < deadline && bad < kMaxBadRequests) {
    fd_set set;
    FD_ZERO(&set);
    FD_ZERO(&set);
#ifdef _WIN32
    FD_SET(static_cast<SOCKET>(fd_), &set);
#else
    FD_SET(static_cast<int>(fd_), &set);
#endif
    timeval tv{0, 200 * 1000};
    const int ready = ::select(static_cast<int>(fd_) + 1, &set, nullptr, nullptr, &tv);
    if (ready <= 0) continue;
#ifdef _WIN32
    const SOCKET c = ::accept(static_cast<SOCKET>(fd_), nullptr, nullptr);
    if (c == INVALID_SOCKET) continue;
#else
    const int c = ::accept(static_cast<int>(fd_), nullptr, nullptr);
    if (c < 0) continue;
#endif
    const std::intptr_t conn = static_cast<std::intptr_t>(c);
#ifdef _WIN32
    DWORD to = 2000;
    ::setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof to);
#else
    timeval rt{2, 0};
    ::setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof rt);
#endif
    std::string raw;
    char buf[1024];
    while (raw.size() < kMaxRequestBytes && raw.find("\r\n\r\n") == std::string::npos) {
#ifdef _WIN32
      const int n = ::recv(c, buf, sizeof buf, 0);
#else
      const ssize_t n = ::recv(c, buf, sizeof buf, 0);
#endif
      if (n <= 0) break;
      raw.append(buf, static_cast<std::size_t>(n));
    }
    const LoopbackRequest req = parseLoopbackRequest(raw);
    if (!req.complete || req.malformed) {
      reply(conn, 400, "Bad Request", "Bad request");
      ++bad;
    } else if (req.method != "GET") {
      reply(conn, 405, "Method Not Allowed", "Method not allowed");
      ++bad;
    } else if (req.host != expectedHost) {
      reply(conn, 400, "Bad Request", "Bad request");   // another name for this address: not a browser we sent to the login
      ++bad;
    } else if (req.target.substr(0, req.target.find('?')) != "/callback") {
      reply(conn, 404, "Not Found", "Not found");
      ++bad;
    } else if (accept && !accept(req.target)) {
      reply(conn, 400, "Bad Request", "Bad request");
      ++bad;
    } else {
      reply(conn, 200, "OK", kPage, "text/html; charset=utf-8");
      result = req.target;
    }
    closeSock(conn);
    if (!result.empty()) break;
  }
  close();
  return result;
}

} // namespace FairyTapes::Tone3000
