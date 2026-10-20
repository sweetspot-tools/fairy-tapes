// Fairy Tapes · shared TONE3000 client — the REST API (see api.h).
#include "tone3000/api.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>

namespace FairyTapes::Tone3000 {

namespace {
using Json = nlohmann::json;

std::string cleanText(const Json& j, const char* key) {
  if (!j.is_object()) return {};
  const auto it = j.find(key);
  if (it == j.end() || !it->is_string()) return {};
  std::string s = it->get<std::string>();
  for (char& c : s)
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) c = ' ';
  if (s.size() > kMaxApiText) {
    std::size_t cut = kMaxApiText;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    s.resize(cut);
  }
  return s;
}

// An id arrives as a number or a string; anything else (or anything that could not be an id) is no id.
std::string idOf(const Json& j, const char* key) {
  if (!j.is_object()) return {};
  const auto it = j.find(key);
  if (it == j.end()) return {};
  std::string s;
  if (it->is_number_integer() || it->is_number_unsigned()) s = std::to_string(it->get<long long>());
  else if (it->is_string()) s = it->get<std::string>();
  return isSafeId(s) ? s : std::string();
}

int intOf(const Json& j, const char* key, int lo, int hi, int fallback = 0) {
  if (!j.is_object()) return fallback;
  const auto it = j.find(key);
  if (it == j.end() || !it->is_number()) return fallback;
  const double v = it->get<double>();
  if (!(v >= lo)) return lo;
  return v > hi ? hi : static_cast<int>(v);
}

bool boolOf(const Json& j, const char* key) {
  if (!j.is_object()) return false;
  const auto it = j.find(key);
  return it != j.end() && it->is_boolean() && it->get<bool>();
}

Json parseJson(const std::string& text) {
  if (text.size() > 8 * 1024 * 1024) return Json();
  return Json::parse(text, nullptr, /*allow_exceptions*/ false);
}

std::optional<Tone> toneFrom(const Json& j) {
  if (!j.is_object()) return std::nullopt;
  Tone t;
  t.id = idOf(j, "id");
  if (t.id.empty()) return std::nullopt;
  t.title = cleanText(j, "title");
  t.url = cleanText(j, "url");
  t.license = cleanText(j, "license");
  t.gear = cleanText(j, "gear");
  t.format = cleanText(j, "format");
  const auto u = j.find("user");
  if (u != j.end() && u->is_object()) {
    t.author.username = cleanText(*u, "username");
    t.author.displayName = cleanText(*u, "display_name");
    t.author.verified = boolOf(*u, "is_verified");
  }
  const auto sz = j.find("sizes");
  if (sz != j.end() && sz->is_array()) {
    for (const auto& s : *sz) {
      if (s.is_string() && t.sizes.size() < 8) {
        std::string v = s.get<std::string>();
        if (v.size() <= 24) t.sizes.push_back(v);
      }
    }
  }
  t.modelsCount = intOf(j, "models_count", 0, 1000000);
  t.a1ModelsCount = intOf(j, "a1_models_count", 0, 1000000);
  t.a2ModelsCount = intOf(j, "a2_models_count", 0, 1000000);
  t.favorite = boolOf(j, "is_favorite");
  return t;
}

std::optional<Model> modelFrom(const Json& j) {
  if (!j.is_object()) return std::nullopt;
  Model m;
  m.id = idOf(j, "id");
  if (m.id.empty()) return std::nullopt;
  m.toneId = idOf(j, "tone_id");
  m.name = cleanText(j, "name");
  m.size = cleanText(j, "size");
  const auto a = j.find("architecture_version");
  if (a != j.end()) {
    if (a->is_string()) m.architecture = architectureName(a->get<std::string>());
    else if (a->is_number_integer()) m.architecture = architectureName(std::to_string(a->get<long long>()));
  }
  m.modelUrl = cleanText(j, "model_url");
  return m;
}

template <class T, class F>
Page<T> pageFrom(const std::string& text, F itemFrom, bool* wellFormed) {
  Page<T> p;
  if (wellFormed) *wellFormed = false;
  const Json j = parseJson(text);
  if (!j.is_object()) return p;
  const auto data = j.find("data");
  if (data == j.end() || !data->is_array()) return p;
  for (const auto& item : *data) {
    if (p.items.size() >= 500) break;
    if (auto v = itemFrom(item)) p.items.push_back(std::move(*v));
  }
  p.page = intOf(j, "page", 1, 1000000, 1);
  p.pageSize = intOf(j, "page_size", 0, 1000000, static_cast<int>(p.items.size()));
  p.total = intOf(j, "total", 0, 100000000, static_cast<int>(p.items.size()));
  p.totalPages = intOf(j, "total_pages", 1, 1000000, 1);
  if (wellFormed) *wellFormed = true;
  return p;
}
} // namespace

bool isSafeId(const std::string& id) {
  if (id.empty() || id.size() > 64) return false;
  for (unsigned char c : id)
    if (!(std::isalnum(c) || c == '-' || c == '_')) return false;
  return true;
}

std::string architectureName(const std::string& v) {
  if (v == "1") return "A1";
  if (v == "2") return "A2";
  if (v == "custom") return "custom";
  return {};
}

std::string percentEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}

std::optional<Tone> parseTone(const std::string& json) { return toneFrom(parseJson(json)); }
std::optional<Model> parseModel(const std::string& json) { return modelFrom(parseJson(json)); }
Page<Tone> parseTonePage(const std::string& json, bool* ok) { return pageFrom<Tone>(json, toneFrom, ok); }
Page<Model> parseModelPage(const std::string& json, bool* ok) { return pageFrom<Model>(json, modelFrom, ok); }

std::optional<User> parseUser(const std::string& json) {
  const Json j = parseJson(json);
  if (!j.is_object()) return std::nullopt;
  User u;
  u.id = idOf(j, "id");
  u.username = cleanText(j, "username");
  u.displayName = cleanText(j, "display_name");
  u.verified = boolOf(j, "is_verified");
  if (u.id.empty() && u.username.empty()) return std::nullopt;
  return u;
}

std::string toneUrl(const std::string& base, const std::string& toneId) { return base + "/tones/" + percentEncode(toneId); }

std::string modelsUrl(const std::string& base, const std::string& toneId, int page, int pageSize) {
  return base + "/models?tone_id=" + percentEncode(toneId) + "&page=" + std::to_string(std::max(1, page)) +
         "&page_size=" + std::to_string(std::clamp(pageSize, 1, 300));
}

std::string listUrl(const std::string& base, ListKind kind, int page, int pageSize) {
  static const char* kNames[] = {"favorited", "downloaded", "created", "trending", "latest"};
  std::string url = base + "/tones/" + kNames[static_cast<int>(kind)];
  if (kind == ListKind::Trending || kind == ListKind::Latest) return url;   // not paginated
  return url + "?page=" + std::to_string(std::max(1, page)) + "&page_size=" + std::to_string(std::clamp(pageSize, 1, 100));
}

std::string userUrl(const std::string& base) { return base + "/user"; }

ApiError errorForStatus(int status) {
  using K = ApiError::Kind;
  ApiError e;
  e.status = status;
  if (status == 401) { e.kind = K::Unauthorized; e.message = "Sign in to TONE3000 again"; }
  else if (status == 403) { e.kind = K::Forbidden; e.message = "TONE3000 refused the request"; }
  else if (status == 404) { e.kind = K::NotFound; e.message = "Not found on TONE3000"; }
  else if (status == 429) { e.kind = K::RateLimited; e.message = "Too many requests, try again in a minute"; }
  else if (status >= 500) { e.kind = K::Server; e.message = "TONE3000 is not answering"; }
  else { e.kind = K::BadResponse; e.message = "Unexpected answer from TONE3000"; }
  return e;
}

Tone3000Api::Tone3000Api(HttpClient& http, RateLimiter& limiter, std::string base)
: http_(http), limiter_(limiter), base_(std::move(base)) {}

ApiResult<std::string> Tone3000Api::get(const std::string& url, const std::atomic<bool>* cancel) {
  ApiResult<std::string> r;
  if (!limiter_.tryAcquire()) {
    r.error = ApiError{ApiError::Kind::RateLimited, 429, "Too many requests, try again in a minute"};   // our own limit: nothing sent
    return r;
  }
  HttpRequest req;
  req.url = url;
  req.headers = {{"Accept", "application/json"}};
  if (!token_.empty()) req.headers.push_back({"Authorization", "Bearer " + token_});
  const HttpResponse res = http_.perform(req, {}, cancel);
  if (res.cancelled) { r.error = ApiError{ApiError::Kind::Cancelled, 0, "Cancelled"}; return r; }
  if (!res.error.empty()) { r.error = ApiError{ApiError::Kind::Network, 0, "Could not reach TONE3000"}; return r; }
  if (!res.ok()) { r.error = errorForStatus(res.status); return r; }
  r.value = res.body;
  return r;
}

ApiResult<User> Tone3000Api::user(const std::atomic<bool>* cancel) {
  ApiResult<User> out;
  auto g = get(userUrl(base_), cancel);
  if (!g.ok()) { out.error = g.error; return out; }
  auto u = parseUser(g.value);
  if (!u) { out.error = ApiError{ApiError::Kind::BadResponse, 200, "Unexpected answer from TONE3000"}; return out; }
  out.value = *u;
  return out;
}

ApiResult<Tone> Tone3000Api::tone(const std::string& toneId, const std::atomic<bool>* cancel) {
  ApiResult<Tone> out;
  if (!isSafeId(toneId)) { out.error = ApiError{ApiError::Kind::NotFound, 0, "Not found on TONE3000"}; return out; }
  auto g = get(toneUrl(base_, toneId), cancel);
  if (!g.ok()) { out.error = g.error; return out; }
  auto t = parseTone(g.value);
  if (!t) { out.error = ApiError{ApiError::Kind::BadResponse, 200, "Unexpected answer from TONE3000"}; return out; }
  out.value = *t;
  return out;
}

ApiResult<Page<Model>> Tone3000Api::models(const std::string& toneId, int page, const std::atomic<bool>* cancel) {
  ApiResult<Page<Model>> out;
  if (!isSafeId(toneId)) { out.error = ApiError{ApiError::Kind::NotFound, 0, "Not found on TONE3000"}; return out; }
  auto g = get(modelsUrl(base_, toneId, page, 100), cancel);
  if (!g.ok()) { out.error = g.error; return out; }
  bool ok = false;
  out.value = parseModelPage(g.value, &ok);
  if (!ok) out.error = ApiError{ApiError::Kind::BadResponse, 200, "Unexpected answer from TONE3000"};
  return out;
}

ApiResult<Page<Tone>> Tone3000Api::tones(ListKind kind, int page, const std::atomic<bool>* cancel) {
  ApiResult<Page<Tone>> out;
  auto g = get(listUrl(base_, kind, page, 25), cancel);
  if (!g.ok()) { out.error = g.error; return out; }
  bool ok = false;
  out.value = parseTonePage(g.value, &ok);
  if (!ok) out.error = ApiError{ApiError::Kind::BadResponse, 200, "Unexpected answer from TONE3000"};
  return out;
}

ApiResult<bool> Tone3000Api::download(const Model& model, const HttpSink& sink, std::size_t maxBytes, const std::atomic<bool>* cancel) {
  ApiResult<bool> out;
  const UrlParts u = parseUrl(model.modelUrl), apiHost = parseUrl(base_);
  if (!u.valid || u.scheme != "https" || !apiHost.valid) {
    out.error = ApiError{ApiError::Kind::BadResponse, 0, "Unexpected answer from TONE3000"};
    return out;
  }
  if (!limiter_.tryAcquire()) {
    out.error = ApiError{ApiError::Kind::RateLimited, 429, "Too many requests, try again in a minute"};
    return out;
  }
  HttpRequest req;
  req.url = model.modelUrl;
  req.timeoutSeconds = 120;
  req.maxBodyBytes = maxBytes;
  req.allowOtherHostRedirects = true;   // a storage host may serve the file; it never gets the token
  // The token goes only to the API host itself: a model_url on another host is fetched without it.
  if (!token_.empty() && u.host == apiHost.host) req.headers.push_back({"Authorization", "Bearer " + token_});
  const HttpResponse res = http_.perform(req, sink, cancel);
  if (res.cancelled) { out.error = ApiError{ApiError::Kind::Cancelled, 0, "Cancelled"}; return out; }
  if (!res.error.empty()) {
    out.error = ApiError{ApiError::Kind::Network, 0, res.error.find("too large") != std::string::npos ? "The model file is too big" : "Could not download the model"};
    return out;
  }
  if (!res.ok()) { out.error = errorForStatus(res.status); return out; }
  out.value = true;
  return out;
}

} // namespace FairyTapes::Tone3000
