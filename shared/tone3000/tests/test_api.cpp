/** Fairy Tapes · shared TONE3000 client — the REST API: readers on example answers, request building, errors (no network). */
#include "tone3000/api.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace FairyTapes::Tone3000;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

// Shapes as documented at https://www.tone3000.com/api (field names and types).
const char* kTone = R"({
  "id": 42, "user_id": 7, "created_at": "2026-01-01T00:00:00Z", "updated_at": "2026-01-02T00:00:00Z", "published_at": null,
  "title": "Fender Twin Reverb '65", "description": null, "url": "https://www.tone3000.com/tones/fender-twin-42",
  "gear": "amp", "format": "nam", "license": "cc-by-nc", "sizes": ["standard", "lite"],
  "user": {"id": 7, "username": "jane", "display_name": "Jane Doe", "is_verified": true, "avatar_url": null},
  "images": null, "links": null, "is_public": true, "makes": [{"id": 1, "name": "Fender"}], "tags": [],
  "models_count": 5, "a1_models_count": 3, "a2_models_count": 2, "irs_count": 0, "custom_models_count": 0,
  "downloads_count": 100, "favorites_count": 4, "is_favorite": true
})";

const char* kModels = R"({
  "data": [
    {"id": 100, "created_at": "x", "updated_at": "x", "user_id": 7, "model_url": "https://www.tone3000.com/api/v1/models/100/file",
     "name": "Twin standard", "size": "standard", "tone_id": 42, "architecture_version": "2"},
    {"id": "m-101", "model_url": "https://www.tone3000.com/api/v1/models/101/file", "name": "Twin lite", "size": "lite",
     "tone_id": 42, "architecture_version": "1"},
    {"id": 102, "name": "Custom", "size": "custom", "tone_id": 42, "architecture_version": "custom", "model_url": "https://x.example/c"},
    {"id": 103, "name": "No arch", "size": "nano", "tone_id": 42, "architecture_version": null, "model_url": "https://x.example/d"},
    {"id": "../../etc", "name": "Bad id"},
    {"name": "No id at all"},
    "not an object"
  ],
  "page": 1, "page_size": 100, "total": 7, "total_pages": 1
})";

struct FakeHttp : HttpClient {
  struct Seen { HttpRequest req; };
  std::vector<Seen> seen;
  HttpResponse next;
  bool sawCancel = false;
  HttpResponse perform(const HttpRequest& r, const HttpSink& sink, const std::atomic<bool>* cancel) override {
    seen.push_back({r});
    sawCancel = cancel != nullptr;
    if (sink && next.ok()) sink("FILE", 4);
    return next;
  }
};

HttpResponse okBody(const std::string& body) { HttpResponse r; r.status = 200; r.body = body; return r; }
HttpResponse status(int s) { HttpResponse r; r.status = s; r.body = "{\"error\":\"<script>alert(1)</script>\"}"; return r; }

struct Rig {
  FakeHttp http;
  RateLimiter limiter{60, 60.0, [] { return 0.0; }};
  Tone3000Api api{http, limiter};
};

void test_parse_tone() {
  const auto t = parseTone(kTone);
  CHECK(t.has_value());
  if (!t) return;
  CHECK(t->id == "42" && t->title == "Fender Twin Reverb '65" && t->license == "cc-by-nc" && t->gear == "amp" && t->format == "nam");
  CHECK(t->url == "https://www.tone3000.com/tones/fender-twin-42");
  CHECK(t->author.username == "jane" && t->author.displayName == "Jane Doe" && t->author.verified && t->author.shownName() == "Jane Doe");
  CHECK(t->sizes.size() == 2 && t->sizes[1] == "lite");
  CHECK(t->modelsCount == 5 && t->a1ModelsCount == 3 && t->a2ModelsCount == 2 && t->favorite);
}

void test_parse_tone_is_tolerant_and_safe() {
  CHECK(!parseTone("").has_value() && !parseTone("[]").has_value() && !parseTone("{}").has_value());
  CHECK(!parseTone("{\"id\":\"a/b\"}").has_value() && !parseTone("{\"id\":\"..\"}").has_value() && !parseTone("{\"id\":null}").has_value());
  const auto t = parseTone("{\"id\":\"abc-1\",\"title\":5,\"user\":\"nobody\",\"sizes\":\"x\",\"models_count\":-3,\"license\":null}");
  CHECK(t && t->id == "abc-1" && t->title.empty() && t->author.shownName().empty() && t->sizes.empty() && t->modelsCount == 0);
  const std::string big = "{\"id\":1,\"title\":\"" + std::string(900, 'y') + "\"}";
  CHECK(parseTone(big)->title.size() == kMaxApiText);
  Author a; a.username = "jane";
  CHECK(a.shownName() == "jane");                                           // no display name: the user name
}

void test_parse_models_page() {
  bool ok = false;
  const auto p = parseModelPage(kModels, &ok);
  CHECK(ok && p.items.size() == 4);                                         // the three malformed items are left out
  CHECK(p.page == 1 && p.pageSize == 100 && p.total == 7 && p.totalPages == 1);
  if (p.items.size() == 4) {
    CHECK(p.items[0].id == "100" && p.items[0].architecture == "A2" && p.items[0].size == "standard" && p.items[0].toneId == "42");
    CHECK(p.items[1].id == "m-101" && p.items[1].architecture == "A1");
    CHECK(p.items[2].architecture == "custom" && p.items[3].architecture.empty());
  }
  parseModelPage("{\"nodata\":1}", &ok);
  CHECK(!ok);
  parseModelPage("garbage", &ok);
  CHECK(!ok);
  CHECK(architectureName("1") == "A1" && architectureName("2") == "A2" && architectureName("custom") == "custom" && architectureName("9").empty());
}

void test_parse_unpaginated_lists() {
  bool ok = false;
  const auto p = parseTonePage("{\"data\":[" + std::string(kTone) + "]}", &ok);   // trending / latest: no page fields
  CHECK(ok && p.items.size() == 1 && p.page == 1 && p.total == 1 && p.totalPages == 1);
  std::string many = "{\"data\":[";
  for (int i = 1; i <= 600; ++i) many += (i > 1 ? "," : "") + std::string("{\"id\":") + std::to_string(i) + "}";
  many += "]}";
  CHECK(parseTonePage(many).items.size() == 500);                           // never an unbounded list
}

void test_parse_user() {
  const auto u = parseUser("{\"id\":7,\"username\":\"jane\",\"display_name\":null,\"is_verified\":true}");
  CHECK(u && u->id == "7" && u->username == "jane" && u->displayName.empty() && u->verified);
  CHECK(!parseUser("{}").has_value() && !parseUser("nope").has_value());
}

void test_urls() {
  const std::string b = "https://www.tone3000.com/api/v1";
  CHECK(toneUrl(b, "42") == b + "/tones/42");
  CHECK(modelsUrl(b, "42", 2, 100) == b + "/models?tone_id=42&page=2&page_size=100");
  CHECK(modelsUrl(b, "42", -5, 9999) == b + "/models?tone_id=42&page=1&page_size=300");
  CHECK(listUrl(b, ListKind::Favorited, 1, 25) == b + "/tones/favorited?page=1&page_size=25");
  CHECK(listUrl(b, ListKind::Downloaded, 3, 500) == b + "/tones/downloaded?page=3&page_size=100");
  CHECK(listUrl(b, ListKind::Created, 1, 10) == b + "/tones/created?page=1&page_size=10");
  CHECK(listUrl(b, ListKind::Trending, 4, 25) == b + "/tones/trending");     // not paginated
  CHECK(listUrl(b, ListKind::Latest, 1, 25) == b + "/tones/latest");
  CHECK(userUrl(b) == b + "/user");
  CHECK(percentEncode("a b&c=d/é") == "a%20b%26c%3Dd%2F%C3%A9");
  CHECK(isSafeId("42") && isSafeId("a-b_C9") && !isSafeId("") && !isSafeId("a b") && !isSafeId("a/b") && !isSafeId(std::string(65, 'a')));
}

void test_requests_carry_the_token_and_parse() {
  Rig r;
  r.api.setAccessToken("tok123");
  r.http.next = okBody(kTone);
  auto t = r.api.tone("42");
  CHECK(t.ok() && t.value.id == "42");
  CHECK(r.http.seen.size() == 1 && r.http.seen[0].req.url == "https://www.tone3000.com/api/v1/tones/42");
  bool hasAuth = false;
  for (const auto& h : r.http.seen[0].req.headers) hasAuth |= h.first == "Authorization" && h.second == "Bearer tok123";
  CHECK(hasAuth);
  r.http.next = okBody(kModels);
  auto m = r.api.models("42");
  CHECK(m.ok() && m.value.items.size() == 4);
  r.http.next = okBody("{\"data\":[]}");
  CHECK(r.api.tones(ListKind::Trending).ok());
  r.http.next = okBody("{\"id\":7,\"username\":\"jane\"}");
  CHECK(r.api.user().ok());
}

void test_errors_map_to_our_own_text() {
  Rig r;
  r.http.next = status(401);
  auto t = r.api.tone("42");
  CHECK(!t.ok() && t.error->kind == ApiError::Kind::Unauthorized && t.error->status == 401);
  CHECK(t.error->message.find("script") == std::string::npos);              // the server's body is never shown
  r.http.next = status(404);
  CHECK(r.api.tone("42").error->kind == ApiError::Kind::NotFound);
  r.http.next = status(429);
  CHECK(r.api.tone("42").error->kind == ApiError::Kind::RateLimited);
  r.http.next = status(503);
  CHECK(r.api.tone("42").error->kind == ApiError::Kind::Server);
  r.http.next = status(403);
  CHECK(r.api.tone("42").error->kind == ApiError::Kind::Forbidden);
  r.http.next = HttpResponse{};
  r.http.next.error = "Could not resolve host";
  CHECK(r.api.tone("42").error->kind == ApiError::Kind::Network);
  r.http.next = HttpResponse{};
  r.http.next.cancelled = true;
  r.http.next.error = "cancelled";
  CHECK(r.api.tone("42").error->kind == ApiError::Kind::Cancelled);
  r.http.next = okBody("<html>not json</html>");
  CHECK(r.api.tone("42").error->kind == ApiError::Kind::BadResponse);
  CHECK(r.api.models("42").error->kind == ApiError::Kind::BadResponse);
  CHECK(r.api.tones(ListKind::Latest).error->kind == ApiError::Kind::BadResponse);
}

void test_bad_ids_never_reach_the_network() {
  Rig r;
  r.http.next = okBody(kTone);
  CHECK(!r.api.tone("../../x").ok() && !r.api.tone("").ok() && !r.api.models("a b").ok());
  CHECK(r.http.seen.empty());
}

void test_the_rate_limit_stops_requests_before_they_go_out() {
  double now = 0;
  FakeHttp http;
  RateLimiter limiter(2, 60.0, [&] { return now; });
  Tone3000Api api(http, limiter);
  http.next = okBody(kTone);
  CHECK(api.tone("1").ok() && api.tone("2").ok());
  auto third = api.tone("3");
  CHECK(!third.ok() && third.error->kind == ApiError::Kind::RateLimited && http.seen.size() == 2);   // nothing was sent
  now = 61;
  CHECK(api.tone("3").ok());
}

void test_the_cancel_flag_is_passed_on() {
  Rig r;
  r.http.next = okBody(kTone);
  std::atomic<bool> cancel{false};
  r.api.tone("1", &cancel);
  CHECK(r.http.sawCancel);
}

void test_download_token_goes_only_to_the_api_host() {
  Rig r;
  r.api.setAccessToken("tok123");
  r.http.next = okBody("");
  Model onApi;
  onApi.id = "100"; onApi.modelUrl = "https://www.tone3000.com/api/v1/models/100/file";
  std::string got;
  auto d = r.api.download(onApi, [&](const char* p, std::size_t n) { got.append(p, n); return true; }, 1 << 20);
  CHECK(d.ok() && got == "FILE");
  const auto& q = r.http.seen.back().req;
  bool auth = false;
  for (const auto& h : q.headers) auth |= h.first == "Authorization";
  CHECK(auth && q.allowOtherHostRedirects && q.maxBodyBytes == (1u << 20));
  Model elsewhere;
  elsewhere.id = "101"; elsewhere.modelUrl = "https://storage.example.org/file.nam";
  r.api.download(elsewhere, [](const char*, std::size_t) { return true; }, 1 << 20);
  bool auth2 = false;
  for (const auto& h : r.http.seen.back().req.headers) auth2 |= h.first == "Authorization";
  CHECK(!auth2);                                                              // another host: no token at all
  Model plain;
  plain.id = "102"; plain.modelUrl = "http://www.tone3000.com/file";
  const size_t before = r.http.seen.size();
  CHECK(!r.api.download(plain, [](const char*, std::size_t) { return true; }, 1 << 20).ok() && r.http.seen.size() == before);   // plain http never
  Model junk;
  CHECK(!r.api.download(junk, [](const char*, std::size_t) { return true; }, 1 << 20).ok() && r.http.seen.size() == before);
}

void test_download_errors() {
  Rig r;
  Model m;
  m.id = "1"; m.modelUrl = "https://www.tone3000.com/api/v1/models/1/file";
  r.http.next = HttpResponse{};
  r.http.next.error = "response too large";
  auto d = r.api.download(m, [](const char*, std::size_t) { return true; }, 10);
  CHECK(!d.ok() && d.error->message == "The model file is too big");
  r.http.next = status(401);
  CHECK(r.api.download(m, [](const char*, std::size_t) { return true; }, 10).error->kind == ApiError::Kind::Unauthorized);
}
} // namespace

int main() {
  std::puts("tone3000 api tests");
  test_parse_tone();
  test_parse_tone_is_tolerant_and_safe();
  test_parse_models_page();
  test_parse_unpaginated_lists();
  test_parse_user();
  test_urls();
  test_requests_carry_the_token_and_parse();
  test_errors_map_to_our_own_text();
  test_bad_ids_never_reach_the_network();
  test_the_rate_limit_stops_requests_before_they_go_out();
  test_the_cancel_flag_is_passed_on();
  test_download_token_goes_only_to_the_api_host();
  test_download_errors();
  if (g_failed) { std::fprintf(stderr, "FAILED (%d)\n", g_failed); return 1; }
  std::puts("All tone3000 api tests passed");
  return 0;
}
