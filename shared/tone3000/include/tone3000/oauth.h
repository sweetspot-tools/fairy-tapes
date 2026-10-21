/**
 * Fairy Tapes · shared TONE3000 client — OAuth 2.0 with PKCE for a desktop plug-in (https://www.tone3000.com/api).
 *
 * The user logs in in their own browser; the answer comes back to a short-lived server on 127.0.0.1 (loopback.h). This file
 * is the pure part (URLs, the callback, the token answer, error texts), tested without a network, plus OAuthFlow, which runs
 * the steps through an HttpClient, a browser opener and a LoopbackServer. Blocking: the Worker runs it, never the audio or
 * the editor thread.
 *
 * Only the publishable client id is used; there is no secret in the plug-in. The `state` of the answer must match the one
 * sent, compared in constant time, and a callback with any other state is ignored as if it never came.
 */

#pragma once

#include "tone3000/http.h"
#include "tone3000/pkce.h"

#include <atomic>
#include <functional>
#include <optional>
#include <string>

namespace FairyTapes::Tone3000 {

constexpr const char* kCallbackPath = "/callback";

struct OAuthConfig {
  std::string clientId;                                // the publishable key of the registered app
  std::string base = "https://www.tone3000.com/api/v1";
};

enum class Prompt { Login, SelectTone };               // "" (basic sign-in) or "select_tone"

struct Tokens {
  std::string access, refresh;
  double expiresInSeconds = 0;                         // from the answer, clamped to a sane range
  bool ok() const { return !access.empty(); }
};

struct CallbackResult {
  enum class Kind { Ok, Cancelled, Error, Invalid } kind = Kind::Invalid;
  std::string code;       // the authorization code (Ok)
  std::string toneId;     // the chosen tone (SelectTone)
  std::string error;      // short OAuth error such as "access_denied" (Error)
};

// ---- pure ----
std::string redirectUriFor(int port);                                              // "http://127.0.0.1:<port>/callback"
std::string buildAuthorizeUrl(const OAuthConfig& cfg, const std::string& redirectUri, const std::string& challenge,
                              const std::string& state, Prompt prompt);
// `target` is the request target the browser sent ("/callback?code=...&state=..."). Anything unexpected is Invalid.
CallbackResult parseCallback(const std::string& target, const std::string& expectedState, Prompt prompt);
std::string tokenRequestBody(const OAuthConfig& cfg, const std::string& code, const std::string& verifier, const std::string& redirectUri);
std::string refreshRequestBody(const OAuthConfig& cfg, const std::string& refreshToken);
std::optional<Tokens> parseTokenResponse(const std::string& json);
std::string oauthErrorText(const std::string& oauthError);                         // our own words for "access_denied" & co.
std::string queryValue(const std::string& query, const std::string& key);          // percent-decoded value, empty if absent

// ---- the flow ----
using BrowserOpener = std::function<bool(const std::string& url)>;
bool openInSystemBrowser(const std::string& url);                                  // only https URLs on tone3000.com

struct OAuthOutcome {
  bool ok = false;
  bool cancelled = false;
  Tokens tokens;
  std::string toneId;     // SelectTone: the tone the user picked
  std::string message;    // short text for the user when not ok
};

class OAuthFlow {
public:
  OAuthFlow(OAuthConfig cfg, HttpClient& http, BrowserOpener opener = openInSystemBrowser, RandomBytes random = systemRandom);
  // Opens the browser and waits (up to timeoutSeconds) for the user; exchanges the code for tokens.
  OAuthOutcome run(Prompt prompt, double timeoutSeconds, const std::atomic<bool>* cancel);
  // A new access token from a refresh token; ok = false when it is no longer valid (log in again).
  OAuthOutcome refresh(const std::string& refreshToken, const std::atomic<bool>* cancel);

private:
  OAuthConfig cfg_;
  HttpClient& http_;
  BrowserOpener opener_;
  RandomBytes random_;
};

} // namespace FairyTapes::Tone3000
