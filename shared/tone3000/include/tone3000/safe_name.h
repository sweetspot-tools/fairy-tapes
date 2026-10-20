/**
 * Fairy Tapes · shared TONE3000 client — file names built from text that comes from the network (tone titles, ids). Header
 * only and pure, unit tested. Nothing that arrives from the API is ever used as a path as it is: each component is reduced
 * to ASCII letters, digits and "._-", has no leading or trailing dots, cannot be a Windows device name, and is short.
 */

#pragma once

#include <cctype>
#include <cstddef>
#include <string>

namespace FairyTapes::Tone3000 {

constexpr std::size_t kMaxNameComponent = 48;

inline bool isReservedWindowsName(const std::string& s) {
  std::string u;
  for (char c : s) {
    if (c == '.') break;   // "CON.txt" is reserved too
    u += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  if (u == "CON" || u == "PRN" || u == "AUX" || u == "NUL") return true;
  if (u.size() == 4 && (u.compare(0, 3, "COM") == 0 || u.compare(0, 3, "LPT") == 0) && u[3] >= '1' && u[3] <= '9') return true;
  return false;
}

// One path component from arbitrary text: letters, digits, '.', '_' and '-' stay; spaces and every other character
// (including any non-ASCII byte) become '_', runs of '_' collapse to one; dots at the start or end are dropped; at most
// kMaxNameComponent characters; a reserved or empty result becomes `fallback` (itself reduced the same way).
inline std::string sanitizeFileComponent(const std::string& in, const std::string& fallback = "item") {
  std::string out;
  for (char ch : in) {
    const unsigned char c = static_cast<unsigned char>(ch);
    const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
    const char put = keep ? ch : '_';
    if (put == '_' && !out.empty() && out.back() == '_') continue;
    out += put;
  }
  const auto trim = [](std::string& s) {
    std::size_t a = 0;
    while (a < s.size() && (s[a] == '.' || s[a] == '_')) ++a;
    std::size_t b = s.size();
    while (b > a && (s[b - 1] == '.' || s[b - 1] == '_')) --b;
    s = s.substr(a, b - a);
  };
  trim(out);
  // two dots in a row never survive (".." is a parent directory in some readers)
  for (std::size_t p; (p = out.find("..")) != std::string::npos;) out.erase(p, 1);
  if (out.size() > kMaxNameComponent) {
    out.resize(kMaxNameComponent);
    trim(out);
  }
  if (out.empty() || isReservedWindowsName(out)) {
    if (fallback == in || fallback.empty()) return "item";
    return sanitizeFileComponent(fallback, "item");
  }
  return out;
}

// "<tone id>-<tone title>/<model id>.nam": the place of a downloaded model under the tones folder (forward slashes).
inline std::string downloadRelativePath(const std::string& toneId, const std::string& toneTitle, const std::string& modelId) {
  return sanitizeFileComponent(toneId, "tone") + "-" + sanitizeFileComponent(toneTitle, "tone") + "/" +
         sanitizeFileComponent(modelId, "model") + ".nam";
}

} // namespace FairyTapes::Tone3000
