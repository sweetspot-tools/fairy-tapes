/**
 * Fairy Tapes · shared TONE3000 client — the login kept between sessions: one small JSON file in the user's preferences
 * folder, created readable by its owner only. Nothing here ever prints or returns a token for logging. Signing out deletes the
 * file (a condition of the API terms: the user's data goes when they revoke access or ask).
 */

#pragma once

#include "tone3000/oauth.h"

#include <filesystem>
#include <optional>
#include <string>

namespace FairyTapes::Tone3000 {

struct StoredLogin {
  Tokens tokens;
  double expiresAtUnix = 0;     // seconds since 1970 when the access token stops working
  std::string userName;         // shown in the interface
};

std::string loginToJson(const StoredLogin& login);
std::optional<StoredLogin> loginFromJson(const std::string& json);          // nullopt unless it holds an access token
bool saveLogin(const std::filesystem::path& file, const StoredLogin& login);
std::optional<StoredLogin> loadLogin(const std::filesystem::path& file);
bool clearLogin(const std::filesystem::path& file);                          // true if the file is gone afterwards

} // namespace FairyTapes::Tone3000
