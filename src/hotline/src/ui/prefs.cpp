// Hotline: user preferences file (see ui/prefs.h).
#include "ui/prefs.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace FairyTapes::Hotline::Ui {

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& s) {
  std::size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
  return s.substr(a, b - a);
}

const char* name(WearLevel v) { return v == WearLevel::Off ? "Off" : v == WearLevel::Heavy ? "Heavy" : "Light"; }
const char* name(TapeFont v) {
  return v == TapeFont::RockSalt ? "RockSalt" : v == TapeFont::HomemadeApple ? "HomemadeApple" : "Zeyada";
}

#if !defined(_WIN32)
std::string env(const char* key) {
  const char* v = std::getenv(key);
  return v ? std::string(v) : std::string();
}
#endif

} // namespace

Prefs parsePrefs(const std::string& text) {
  Prefs p;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos || eq == 0) continue;
    const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
    if (key == "wear") {
      if (value == "Off") p.wear = WearLevel::Off;
      else if (value == "Light") p.wear = WearLevel::Light;
      else if (value == "Heavy") p.wear = WearLevel::Heavy;
    } else if (key == "notes") {
      if (value == "Show") p.notes = true;
      else if (value == "Hide") p.notes = false;
    } else if (key == "tapeFont") {
      if (value == "Zeyada") p.tapeFont = TapeFont::Zeyada;
      else if (value == "RockSalt") p.tapeFont = TapeFont::RockSalt;
      else if (value == "HomemadeApple") p.tapeFont = TapeFont::HomemadeApple;
    } else if (key == "valuesFont") {
      if (value == "Tape") p.handwrittenValues = true;
      else if (value == "Standard") p.handwrittenValues = false;
    }
  }
  return p;
}

std::string serializePrefs(const Prefs& p) {
  std::ostringstream out;
  out << "# Fairy Tapes - Hotline user preferences (safe to delete: defaults are used)\n";
  out << "wear=" << name(p.wear) << "\n";
  out << "notes=" << (p.notes ? "Show" : "Hide") << "\n";
  out << "tapeFont=" << name(p.tapeFont) << "\n";
  out << "valuesFont=" << (p.handwrittenValues ? "Tape" : "Standard") << "\n";
  return out.str();
}

Prefs loadPrefs(const fs::path& file) {
  if (file.empty()) return {};
  std::error_code ec;
  if (!fs::is_regular_file(file, ec) || ec) return {};
  const auto size = fs::file_size(file, ec);
  if (ec || size > kMaxPrefsBytes) return {};
  std::ifstream in(file, std::ios::binary);
  if (!in) return {};
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (text.find('\0') != std::string::npos) return {};   // binary garbage
  return parsePrefs(text);
}

bool savePrefs(const fs::path& file, const Prefs& p) {
  if (file.empty()) return false;
  std::error_code ec;
  if (fs::is_directory(file, ec)) return false;
  if (file.has_parent_path()) {
    fs::create_directories(file.parent_path(), ec);
    if (ec) return false;
  }
  fs::path tmp = file;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << serializePrefs(p);
    out.flush();
    if (!out) {
      out.close();
      fs::remove(tmp, ec);
      return false;
    }
  }
  fs::rename(tmp, file, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return false;
  }
  return true;
}

fs::path prefsPathFor(const std::string& home, const std::string& configHome, const std::string& appData) {
  fs::path base;
#if defined(_WIN32)
  (void)home;
  (void)configHome;
  if (appData.empty()) return {};
  base = fs::path(appData);
#elif defined(__APPLE__)
  (void)configHome;
  (void)appData;
  if (home.empty()) return {};
  base = fs::path(home) / "Library" / "Application Support";
#else
  (void)appData;
  // XDG: a relative $XDG_CONFIG_HOME is invalid and ignored.
  if (!configHome.empty() && fs::path(configHome).is_absolute()) base = fs::path(configHome);
  else if (!home.empty()) base = fs::path(home) / ".config";
  else return {};
#endif
  return base / "FairyTapes" / "Hotline.prefs";
}

fs::path prefsPath() {
#if defined(_WIN32)
  // %APPDATA% may hold non-ASCII characters: read it as UTF-16.
  const wchar_t* appData = _wgetenv(L"APPDATA");
  if (!appData || !*appData) return {};
  return fs::path(appData) / "FairyTapes" / "Hotline.prefs";
#else
  return prefsPathFor(env("HOME"), env("XDG_CONFIG_HOME"), env("APPDATA"));
#endif
}

} // namespace FairyTapes::Hotline::Ui
