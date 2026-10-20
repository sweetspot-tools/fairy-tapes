/**
 * Fairy Tapes · shared TONE3000 client — the REST API (https://www.tone3000.com/api, v1), the part the free tier allows: the
 * tone of a `select_tone` result, its models, the bounded lists (favorited, downloaded, created, trending, latest) and the
 * download of one model. Types and the JSON readers are pure (tested with files of example answers); Tone3000Api only builds
 * the requests and runs them through an HttpClient and a RateLimiter. Blocking: the Worker runs it, never the audio thread.
 *
 * Everything read from an answer is data: ids must look like ids, text is cut and cleaned, and an item that does not parse is
 * left out instead of failing the page. The access token is sent only to the API host.
 */

#pragma once

#include "tone3000/http.h"
#include "tone3000/rate_limiter.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace FairyTapes::Tone3000 {

constexpr const char* kApiBase = "https://www.tone3000.com/api/v1";
constexpr std::size_t kMaxApiText = 200;

struct Author {
  std::string username, displayName;
  bool verified = false;
  const std::string& shownName() const { return displayName.empty() ? username : displayName; }
};

struct Tone {
  std::string id;
  std::string title;
  std::string url;                  // the tone's page on tone3000.com
  std::string license;              // "t3k", "cc-by", "cc-by-nc", "cco", ...
  std::string gear;                 // "amp", "amp-cab", "pedal", "cab", ...
  std::string format;               // "nam", "ir"
  Author author;
  std::vector<std::string> sizes;   // "standard", "lite", "feather", "nano", "custom"
  int modelsCount = 0, a1ModelsCount = 0, a2ModelsCount = 0;
  bool favorite = false;
};

struct Model {
  std::string id, toneId;
  std::string name;
  std::string size;                 // "standard", "lite", ...
  std::string architecture;         // "A1", "A2", "custom" or "" when unknown
  std::string modelUrl;             // where the file is; needs the access token
};

template <class T>
struct Page {
  std::vector<T> items;
  int page = 1, pageSize = 0, total = 0, totalPages = 1;
};

struct User {
  std::string id, username, displayName;
  bool verified = false;
};

struct ApiError {
  enum class Kind { Network, Unauthorized, Forbidden, NotFound, RateLimited, Server, BadResponse, Cancelled };
  Kind kind = Kind::Network;
  int status = 0;            // HTTP status, 0 when none
  std::string message;       // short, from our own text (never the server's body)
};

template <class T>
struct ApiResult {
  T value{};
  std::optional<ApiError> error;
  bool ok() const { return !error.has_value(); }
};

enum class ListKind { Favorited, Downloaded, Created, Trending, Latest };

// ---- pure readers (JSON text in, nothing out on failure) ----
std::optional<Tone> parseTone(const std::string& json);
std::optional<Model> parseModel(const std::string& json);
Page<Tone> parseTonePage(const std::string& json, bool* wellFormed = nullptr);     // {"data":[...], page fields optional}
Page<Model> parseModelPage(const std::string& json, bool* wellFormed = nullptr);
std::optional<User> parseUser(const std::string& json);
// "1" -> "A1", "2" -> "A2", "custom" -> "custom", anything else -> "".
std::string architectureName(const std::string& apiValue);
bool isSafeId(const std::string& id);   // 1..64 characters of [A-Za-z0-9_-]
std::string percentEncode(const std::string& s);

// ---- request building (pure) ----
std::string toneUrl(const std::string& base, const std::string& toneId);
std::string modelsUrl(const std::string& base, const std::string& toneId, int page, int pageSize);
std::string listUrl(const std::string& base, ListKind kind, int page, int pageSize);
std::string userUrl(const std::string& base);
ApiError errorForStatus(int status);

class Tone3000Api {
public:
  Tone3000Api(HttpClient& http, RateLimiter& limiter, std::string base = kApiBase);

  void setAccessToken(std::string token) { token_ = std::move(token); }
  bool hasToken() const { return !token_.empty(); }

  ApiResult<User> user(const std::atomic<bool>* cancel = nullptr);
  ApiResult<Tone> tone(const std::string& toneId, const std::atomic<bool>* cancel = nullptr);
  ApiResult<Page<Model>> models(const std::string& toneId, int page = 1, const std::atomic<bool>* cancel = nullptr);
  ApiResult<Page<Tone>> tones(ListKind kind, int page = 1, const std::atomic<bool>* cancel = nullptr);

  // Downloads one model into `sink` (the caller writes it to a file and decides what is too big). The token goes to the API
  // host only: a redirect to a storage host is followed without it. `maxBytes` caps the file.
  ApiResult<bool> download(const Model& model, const HttpSink& sink, std::size_t maxBytes, const std::atomic<bool>* cancel = nullptr);

private:
  ApiResult<std::string> get(const std::string& url, const std::atomic<bool>* cancel);
  HttpClient& http_;
  RateLimiter& limiter_;
  std::string base_;
  std::string token_;
};

} // namespace FairyTapes::Tone3000
