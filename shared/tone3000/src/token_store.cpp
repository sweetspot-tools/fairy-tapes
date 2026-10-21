// Fairy Tapes · shared TONE3000 client — the stored login (see token_store.h).
#include "tone3000/token_store.h"

#include "tone3000/file_util.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <system_error>

namespace FairyTapes::Tone3000 {

namespace {
using Json = nlohmann::json;
constexpr std::size_t kMaxLoginFile = 32 * 1024;

bool tokenOk(const std::string& s) {
  if (s.empty() || s.size() > 4096) return false;
  for (unsigned char c : s)
    if (c <= 0x20 || c >= 0x7f) return false;
  return true;
}
} // namespace

std::string loginToJson(const StoredLogin& l) {
  Json j = {{"access_token", l.tokens.access}, {"refresh_token", l.tokens.refresh}, {"expires_at", l.expiresAtUnix}, {"user", l.userName}};
  return j.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

std::optional<StoredLogin> loginFromJson(const std::string& text) {
  if (text.size() > kMaxLoginFile) return std::nullopt;
  const Json j = Json::parse(text, nullptr, false);
  if (!j.is_object()) return std::nullopt;
  const auto a = j.find("access_token");
  if (a == j.end() || !a->is_string() || !tokenOk(a->get<std::string>())) return std::nullopt;
  StoredLogin l;
  l.tokens.access = a->get<std::string>();
  const auto r = j.find("refresh_token");
  if (r != j.end() && r->is_string() && tokenOk(r->get<std::string>())) l.tokens.refresh = r->get<std::string>();
  const auto e = j.find("expires_at");
  if (e != j.end() && e->is_number()) {
    const double v = e->get<double>();
    l.expiresAtUnix = std::isfinite(v) && v > 0 ? v : 0;
  }
  const auto u = j.find("user");
  if (u != j.end() && u->is_string()) {
    l.userName = u->get<std::string>();
    for (char& c : l.userName)
      if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) c = ' ';
    if (l.userName.size() > 120) l.userName.resize(120);
  }
  return l;
}

bool saveLogin(const std::filesystem::path& file, const StoredLogin& login) { return writeFileAtomic(file, loginToJson(login), /*privateFile*/ true); }

std::optional<StoredLogin> loadLogin(const std::filesystem::path& file) {
  const auto text = readFileLimited(file, kMaxLoginFile);
  return text ? loginFromJson(*text) : std::nullopt;
}

bool clearLogin(const std::filesystem::path& file) {
  std::error_code ec;
  std::filesystem::remove(file, ec);
  return !std::filesystem::exists(file, ec);
}

} // namespace FairyTapes::Tone3000
