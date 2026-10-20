/**
 * Fairy Tapes · shared TONE3000 client — the HTTP interface.
 *
 * Everything that talks to the network goes through HttpClient, so the API code and the tests never see libcurl
 * (http_curl.cpp is the only implementation that does). Blocking: callers run it on the Worker thread, never on the
 * audio or the editor thread.
 *
 * Safety rules every implementation keeps (spec 2026-10-21-tone3000-integration-design.md, section 6):
 *  - https only; plain http is accepted only for a loopback host and only when the request says allowLoopbackHttp
 *    (tests, and the local OAuth redirect receiver does not use this class at all);
 *  - redirects are followed by hand, at most kMaxRedirects, and only to https on the same host as the request;
 *  - certificates are always verified, there is no switch to turn that off;
 *  - the body is handed to a sink as it arrives, so a download never sits in memory and can be refused midway.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace FairyTapes::Tone3000 {

constexpr int kMaxRedirects = 3;

struct HttpRequest {
  std::string url;
  std::string method = "GET";                                     // "GET" or "POST"
  std::vector<std::pair<std::string, std::string>> headers;       // e.g. {"Authorization", "Bearer ..."}
  std::string body;                                               // POST body (the caller sets Content-Type)
  long timeoutSeconds = 30;                                       // whole transfer; the connect phase is capped at 10 s
  std::size_t maxBodyBytes = 4 * 1024 * 1024;                     // bigger responses are cut off and reported as an error
  bool allowLoopbackHttp = false;                                 // tests only
};

struct HttpResponse {
  int status = 0;            // HTTP status, 0 when no response arrived
  std::string body;          // empty when a sink took the body
  std::string error;         // transport or policy failure, empty on success (a 404 is not an error here)
  bool cancelled = false;
  bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

// Receives the body in pieces; returning false aborts the transfer (reported as an error).
using HttpSink = std::function<bool(const char* data, std::size_t size)>;

class HttpClient {
public:
  virtual ~HttpClient() = default;
  // `sink` unset: the body is collected in the response, up to maxBodyBytes. `cancel` may be null; when it turns true
  // the transfer stops and the response has cancelled = true.
  virtual HttpResponse perform(const HttpRequest& request, const HttpSink& sink = {},
                               const std::atomic<bool>* cancel = nullptr) = 0;
};

// The libcurl implementation (http_curl.cpp). Safe to create several; curl's global state is set up once.
class CurlHttpClient : public HttpClient {
public:
  HttpResponse perform(const HttpRequest& request, const HttpSink& sink = {},
                       const std::atomic<bool>* cancel = nullptr) override;
};

// URL helpers the policy uses (pure, tested).
struct UrlParts {
  std::string scheme, host, rest;   // scheme lower-case, host lower-case without port, rest = "/path?query"
  int port = 0;                     // 0 = default
  bool valid = false;
};
UrlParts parseUrl(const std::string& url);
bool isLoopbackHost(const std::string& host);
// True if `target` may be followed from `origin`: https, same host and same port. With `loopbackHttp` (tests) a plain-http
// redirect between the same loopback host and port is accepted too.
bool redirectAllowed(const UrlParts& origin, const UrlParts& target, bool loopbackHttp = false);
// Resolves a Location header (absolute or starting with "/") against the request URL; empty when unusable.
std::string resolveLocation(const UrlParts& origin, const std::string& location);

} // namespace FairyTapes::Tone3000
