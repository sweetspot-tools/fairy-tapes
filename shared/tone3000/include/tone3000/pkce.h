/**
 * Fairy Tapes · shared TONE3000 client — PKCE (RFC 7636) for the OAuth 2.0 login: SHA-256, base64url, the code verifier and
 * its challenge, and the random `state`. No dependencies; the random source is injectable so tests are exact.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace FairyTapes::Tone3000 {

std::array<std::uint8_t, 32> sha256(const std::string& data);
std::string base64Url(const std::uint8_t* data, std::size_t size);   // RFC 4648 section 5, no padding

// Fills `out` with `size` cryptographically secure random bytes; false if the system could not.
using RandomBytes = std::function<bool(std::uint8_t* out, std::size_t size)>;
bool systemRandom(std::uint8_t* out, std::size_t size);   // getrandom / arc4random_buf / BCryptGenRandom

struct Pkce {
  std::string verifier;    // 43 characters from 32 random bytes; empty if the random source failed
  std::string challenge;   // base64url(SHA-256(verifier)), method S256
  bool ok() const { return !verifier.empty(); }
};

std::string challengeFor(const std::string& verifier);
Pkce makePkce(const RandomBytes& random = systemRandom);
// 16 random bytes as 22 base64url characters; empty if the random source failed.
std::string makeState(const RandomBytes& random = systemRandom);

// Constant-time comparison for `state` values (no early exit on the first difference).
bool sameSecret(const std::string& a, const std::string& b);

} // namespace FairyTapes::Tone3000
