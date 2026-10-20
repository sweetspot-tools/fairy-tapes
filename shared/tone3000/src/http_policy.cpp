// Fairy Tapes · shared TONE3000 client — URL parsing and the redirect policy (no libcurl, unit tested).
#include "tone3000/http.h"

#include <algorithm>
#include <cctype>

namespace FairyTapes::Tone3000 {

namespace {
std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
} // namespace

UrlParts parseUrl(const std::string& url) {
  UrlParts p;
  for (unsigned char c : url)
    if (c <= 0x20 || c == 0x7f) return p;   // control characters and spaces never belong in a URL we use
  const auto sep = url.find("://");
  if (sep == std::string::npos || sep == 0) return p;
  p.scheme = lower(url.substr(0, sep));
  if (p.scheme != "https" && p.scheme != "http") return p;
  std::string rest = url.substr(sep + 3);
  const auto slash = rest.find_first_of("/?#");
  std::string authority = rest.substr(0, slash);
  p.rest = slash == std::string::npos ? "/" : rest.substr(slash);
  if (!p.rest.empty() && p.rest[0] != '/') p.rest.insert(p.rest.begin(), '/');
  if (authority.empty() || authority.find('@') != std::string::npos) return p;   // user:password@ is refused
  if (authority[0] == '[') {                                                       // [::1]:port
    const auto close = authority.find(']');
    if (close == std::string::npos) return p;
    p.host = lower(authority.substr(1, close - 1));
    authority = authority.substr(close + 1);
    if (!authority.empty() && authority[0] != ':') return p;
  } else {
    const auto colon = authority.find(':');
    p.host = lower(authority.substr(0, colon));
    authority = colon == std::string::npos ? std::string() : authority.substr(colon);
  }
  if (!authority.empty()) {
    if (authority.size() < 2 || authority.size() > 6) return p;
    int port = 0;
    for (std::size_t i = 1; i < authority.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(authority[i]))) return p;
      port = port * 10 + (authority[i] - '0');
    }
    if (port < 1 || port > 65535) return p;
    p.port = port;
  }
  p.valid = !p.host.empty();
  return p;
}

bool isLoopbackHost(const std::string& host) {
  const std::string h = lower(host);
  return h == "localhost" || h == "127.0.0.1" || h == "::1";
}

bool redirectAllowed(const UrlParts& origin, const UrlParts& target, bool loopbackHttp) {
  if (!origin.valid || !target.valid) return false;
  if (origin.host != target.host) return false;
  const bool plainOk = loopbackHttp && isLoopbackHost(target.host) && target.scheme == "http" && origin.scheme == "http";
  if (target.scheme != "https" && !plainOk) return false;
  const int def = target.scheme == "https" ? 443 : 80;
  const int originPort = origin.port == 0 ? def : origin.port;
  const int targetPort = target.port == 0 ? def : target.port;
  return originPort == targetPort;
}

std::string resolveLocation(const UrlParts& origin, const std::string& location) {
  if (!origin.valid || location.empty()) return {};
  if (location.find("://") != std::string::npos) return location;
  if (location[0] == '/' && (location.size() < 2 || location[1] != '/')) {
    std::string out = origin.scheme + "://" + (origin.host.find(':') != std::string::npos ? "[" + origin.host + "]" : origin.host);
    if (origin.port != 0) out += ":" + std::to_string(origin.port);
    return out + location;
  }
  return {};   // relative paths and scheme-relative "//host" are not followed
}

} // namespace FairyTapes::Tone3000
