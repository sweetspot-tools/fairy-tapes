// Fairy Tapes · shared TONE3000 client — the libcurl implementation of HttpClient. See http.h for the safety rules.
#include "tone3000/http.h"

#include <curl/curl.h>

#include <mutex>

namespace FairyTapes::Tone3000 {

namespace {

std::once_flag g_curlInit;

struct Transfer {
  const HttpRequest* request = nullptr;
  const HttpSink* sink = nullptr;
  const std::atomic<bool>* cancel = nullptr;
  CURL* easy = nullptr;
  std::string body;
  std::string location;
  std::size_t received = 0;
  bool tooLarge = false;
  bool sinkRefused = false;
};

bool isRedirectStatus(long code) { return code == 301 || code == 302 || code == 303 || code == 307 || code == 308; }

std::size_t onWrite(char* data, std::size_t size, std::size_t count, void* user) {
  auto* t = static_cast<Transfer*>(user);
  const std::size_t n = size * count;
  long code = 0;
  curl_easy_getinfo(t->easy, CURLINFO_RESPONSE_CODE, &code);
  if (isRedirectStatus(code)) return n;   // the body of a redirect is dropped
  t->received += n;
  if (t->received > t->request->maxBodyBytes) {
    t->tooLarge = true;
    return 0;
  }
  if (*t->sink) {
    if (!(*t->sink)(data, n)) {
      t->sinkRefused = true;
      return 0;
    }
  } else {
    t->body.append(data, n);
  }
  return n;
}

std::size_t onHeader(char* data, std::size_t size, std::size_t count, void* user) {
  auto* t = static_cast<Transfer*>(user);
  const std::size_t n = size * count;
  static const char kName[] = "location:";
  if (n > sizeof(kName) - 1) {
    bool match = true;
    for (std::size_t i = 0; i < sizeof(kName) - 1; ++i)
      if (std::tolower(static_cast<unsigned char>(data[i])) != kName[i]) { match = false; break; }
    if (match) {
      std::size_t b = sizeof(kName) - 1, e = n;
      while (b < e && (data[b] == ' ' || data[b] == '\t')) ++b;
      while (e > b && (data[e - 1] == '\r' || data[e - 1] == '\n' || data[e - 1] == ' ')) --e;
      t->location.assign(data + b, e - b);
    }
  }
  return n;
}

int onProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  auto* t = static_cast<Transfer*>(user);
  return t->cancel && t->cancel->load() ? 1 : 0;
}

} // namespace

HttpResponse CurlHttpClient::perform(const HttpRequest& request, const HttpSink& sink, const std::atomic<bool>* cancel) {
  std::call_once(g_curlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
  HttpResponse out;
  if (request.method != "GET" && request.method != "POST") {
    out.error = "unsupported method";
    return out;
  }
  std::string url = request.url;
  std::string method = request.method, body = request.body;
  UrlParts origin = parseUrl(url);
  for (int hop = 0; hop <= kMaxRedirects; ++hop) {
    const UrlParts here = parseUrl(url);
    if (!here.valid) { out.error = "invalid URL"; return out; }
    const bool plain = here.scheme == "http";
    if (hop > 0 && !redirectAllowed(origin, here, request.allowLoopbackHttp)) {
      out.error = "redirect to another host refused";
      return out;
    }
    if (plain && !(request.allowLoopbackHttp && isLoopbackHost(here.host))) {
      out.error = "plain http is not allowed";
      return out;
    }

    CURL* easy = curl_easy_init();
    if (!easy) { out.error = "curl init failed"; return out; }
    Transfer t;
    t.request = &request;
    t.sink = &sink;
    t.cancel = cancel;
    t.easy = easy;
    curl_slist* headers = nullptr;
    for (const auto& h : request.headers) headers = curl_slist_append(headers, (h.first + ": " + h.second).c_str());

    curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, (plain ? "http" : "https"));
    curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);   // redirects are followed below, by hand
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, "FairyTapes/1.0");
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, request.timeoutSeconds);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    if (headers) curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
    if (method == "POST") {
      curl_easy_setopt(easy, CURLOPT_POST, 1L);
      curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body.c_str());
      curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    }
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, onWrite);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &t);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, onHeader);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, &t);
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(easy, CURLOPT_XFERINFODATA, &t);

    const CURLcode rc = curl_easy_perform(easy);
    long code = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(easy);
    if (headers) curl_slist_free_all(headers);

    if (rc == CURLE_ABORTED_BY_CALLBACK && cancel && cancel->load()) { out.cancelled = true; out.error = "cancelled"; return out; }
    if (t.tooLarge) { out.error = "response too large"; return out; }
    if (t.sinkRefused) { out.error = "download refused by the receiver"; return out; }
    if (rc != CURLE_OK) { out.error = curl_easy_strerror(rc); return out; }

    if (isRedirectStatus(code)) {
      const std::string next = resolveLocation(here, t.location);
      if (next.empty()) { out.error = "unusable redirect"; return out; }
      url = next;
      if (code == 301 || code == 302 || code == 303) { method = "GET"; body.clear(); }
      continue;
    }
    out.status = static_cast<int>(code);
    out.body = std::move(t.body);
    return out;
  }
  out.error = "too many redirects";
  return out;
}

} // namespace FairyTapes::Tone3000
