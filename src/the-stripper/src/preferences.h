/**
 * Fairy Tapes · The Stripper — user preferences (spec 6): a small text file in the user's preferences folder,
 * valid for every project and instance, never part of the plug-in state. Header-only and free of the SDK so it is
 * unit tested.
 *
 * Format: UTF-8 lines "key=value"; '#' starts a comment; unknown keys and bad lines are ignored; a missing,
 * unreadable, oversized or corrupt file means defaults (each invalid value falls back on its own).
 * The keys are stable names: never rename one.
 */

#pragma once

#include "fairy_ui/wear.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace FairyTapes::TheStripper {

using WearLevel = FairyTapes::Ui::WearLevel;   // shared with the other plug-ins (fairy_ui/wear.h): Off, Light, Heavy
enum class NotesLevel { Hide, Show };
enum class TapeFont { Zeyada, RockSalt, HomemadeApple };
enum class ValuesFont { Standard, Tape };
enum class BackTab { Settings, Credits, Buttons, Routing };   // the tab of the back panel the user looked at last

constexpr double kMaxMeterHoldSeconds = 10.0;
constexpr double kMinMeterReleaseDbPerSec = 5.0;
constexpr double kMaxMeterReleaseDbPerSec = 200.0;
constexpr std::size_t kMaxModelFolders = 64;
constexpr std::size_t kMaxFolderBytes = 4096;
constexpr std::uintmax_t kMaxPreferencesBytes = 256 * 1024;

struct Preferences {
  WearLevel wear = WearLevel::Light;
  NotesLevel notes = NotesLevel::Show;
  double meterHoldSeconds = 1.5;        // peak hold of the level meters
  double meterReleaseDbPerSec = 20.0;   // fall of the level meters
  TapeFont tapeFont = TapeFont::Zeyada;
  ValuesFont valuesFont = ValuesFont::Tape;
  BackTab backTab = BackTab::Settings;   // the back panel opens on the last tab used
  // false = the user never touched the folder list (the editor proposes its defaults); true = `modelFolders` is the
  // user's list, even when empty.
  bool modelFoldersConfigured = false;
  std::vector<std::string> modelFolders;
};

namespace prefs_detail {

inline std::string trim(const std::string& s) {
  std::size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
  return s.substr(a, b - a);
}

inline bool parseDouble(const std::string& s, double& out) {
  if (s.empty()) return false;
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (end != s.c_str() + s.size() || !std::isfinite(v)) return false;
  out = v;
  return true;
}

inline const char* name(WearLevel v) { return v == WearLevel::Off ? "Off" : v == WearLevel::Heavy ? "Heavy" : "Light"; }
inline const char* name(NotesLevel v) { return v == NotesLevel::Hide ? "Hide" : "Show"; }
inline const char* name(TapeFont v) { return v == TapeFont::RockSalt ? "RockSalt" : v == TapeFont::HomemadeApple ? "HomemadeApple" : "Zeyada"; }
inline const char* name(ValuesFont v) { return v == ValuesFont::Standard ? "Standard" : "Tape"; }
inline const char* name(BackTab v) {
  return v == BackTab::Credits ? "Credits" : v == BackTab::Buttons ? "Buttons" : v == BackTab::Routing ? "Routing" : "Settings";
}

inline bool usableFolder(const std::string& f) {
  return !f.empty() && f.size() <= kMaxFolderBytes && f.find_first_of(std::string("\n\r\0", 3)) == std::string::npos;
}

} // namespace prefs_detail

inline Preferences parsePreferences(const std::string& text) {
  using namespace prefs_detail;
  Preferences p;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos || eq == 0) continue;
    const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
    double d = 0;
    if (key == "wear") {
      if (value == "Off") p.wear = WearLevel::Off;
      else if (value == "Light") p.wear = WearLevel::Light;
      else if (value == "Heavy") p.wear = WearLevel::Heavy;
    } else if (key == "notes") {
      if (value == "Hide") p.notes = NotesLevel::Hide;
      else if (value == "Show") p.notes = NotesLevel::Show;
    } else if (key == "tapeFont") {
      if (value == "Zeyada") p.tapeFont = TapeFont::Zeyada;
      else if (value == "RockSalt") p.tapeFont = TapeFont::RockSalt;
      else if (value == "HomemadeApple") p.tapeFont = TapeFont::HomemadeApple;
    } else if (key == "valuesFont") {
      if (value == "Standard") p.valuesFont = ValuesFont::Standard;
      else if (value == "Tape") p.valuesFont = ValuesFont::Tape;
    } else if (key == "backTab") {
      if (value == "Settings") p.backTab = BackTab::Settings;
      else if (value == "Credits") p.backTab = BackTab::Credits;
      else if (value == "Buttons") p.backTab = BackTab::Buttons;
      else if (value == "Routing") p.backTab = BackTab::Routing;
    } else if (key == "meterHoldSeconds") {
      if (parseDouble(value, d)) p.meterHoldSeconds = std::clamp(d, 0.0, kMaxMeterHoldSeconds);
    } else if (key == "meterReleaseDbPerSec") {
      if (parseDouble(value, d)) p.meterReleaseDbPerSec = std::clamp(d, kMinMeterReleaseDbPerSec, kMaxMeterReleaseDbPerSec);
    } else if (key == "modelFolders") {
      if (value == "set") p.modelFoldersConfigured = true;
    } else if (key == "modelFolder") {
      if (usableFolder(value) && p.modelFolders.size() < kMaxModelFolders) p.modelFolders.push_back(value);
    }
  }
  return p;
}

inline std::string serializePreferences(const Preferences& p) {
  using namespace prefs_detail;
  std::ostringstream out;
  out.precision(17);
  out << "# Fairy Tapes - The Stripper user preferences (safe to delete: defaults are used)\n";
  out << "wear=" << name(p.wear) << "\n";
  out << "notes=" << name(p.notes) << "\n";
  out << "meterHoldSeconds=" << std::clamp(p.meterHoldSeconds, 0.0, kMaxMeterHoldSeconds) << "\n";
  out << "meterReleaseDbPerSec=" << std::clamp(p.meterReleaseDbPerSec, kMinMeterReleaseDbPerSec, kMaxMeterReleaseDbPerSec) << "\n";
  out << "tapeFont=" << name(p.tapeFont) << "\n";
  out << "valuesFont=" << name(p.valuesFont) << "\n";
  out << "backTab=" << name(p.backTab) << "\n";
  if (p.modelFoldersConfigured) {
    out << "modelFolders=set\n";
    std::size_t n = 0;
    for (const auto& f : p.modelFolders)
      if (usableFolder(f) && n++ < kMaxModelFolders) out << "modelFolder=" << f << "\n";
  }
  return out.str();
}

/// Reads the file; defaults on any problem.
inline Preferences loadPreferences(const std::filesystem::path& file) {
  namespace fs = std::filesystem;
  if (file.empty()) return {};
  std::error_code ec;
  if (!fs::is_regular_file(file, ec) || ec) return {};
  const auto size = fs::file_size(file, ec);
  if (ec || size > kMaxPreferencesBytes) return {};
  std::ifstream in(file, std::ios::binary);
  if (!in) return {};
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (text.find('\0') != std::string::npos) return {};   // binary garbage
  return parsePreferences(text);
}

/// Writes the file atomically (temporary file in the same folder, then rename). Creates the folders. False on failure.
inline bool savePreferences(const std::filesystem::path& file, const Preferences& p) {
  namespace fs = std::filesystem;
  if (file.empty()) return false;
  std::error_code ec;
  if (file.has_parent_path()) {
    fs::create_directories(file.parent_path(), ec);
    if (ec) return false;
  }
  fs::path tmp = file;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << serializePreferences(p);
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

/// <preferences folder>/FairyTapes/<plugin>.prefs. `home` and `configHome` are the user's home folder and the
/// Linux config folder ($XDG_CONFIG_HOME or ~/.config); they are parameters so tests do not touch the real ones.
inline std::filesystem::path preferencesPath(const std::string& plugin, const std::string& home, const std::string& configHome) {
  namespace fs = std::filesystem;
#if defined(_WIN32)
  (void)home;
  (void)configHome;
  const char* appData = std::getenv("APPDATA");
  fs::path base = appData ? fs::path(appData) : fs::path(".");
#elif defined(__APPLE__)
  (void)configHome;
  fs::path base = fs::path(home) / "Library" / "Application Support";
#else
  fs::path base = configHome.empty() ? fs::path(home) / ".config" : fs::path(configHome);
#endif
  return base / "FairyTapes" / (plugin + ".prefs");
}

/// The user's home folder: $HOME, else %USERPROFILE% (Windows normally has no HOME), else %HOMEDRIVE%%HOMEPATH%; empty
/// when none is set. The values are parameters so tests do not touch the environment.
inline std::string userHomeFrom(const char* home, const char* userProfile, const char* homeDrive, const char* homePath) {
  if (home && *home) return home;
  if (userProfile && *userProfile) return userProfile;
  if (homeDrive && *homeDrive && homePath && *homePath) return std::string(homeDrive) + homePath;
  return {};
}

/// The user's home folder, from the environment.
inline std::string userHome() {
  return userHomeFrom(std::getenv("HOME"), std::getenv("USERPROFILE"), std::getenv("HOMEDRIVE"), std::getenv("HOMEPATH"));
}

/// The real path for this user, from the environment.
inline std::filesystem::path preferencesPath(const std::string& plugin) {
  const char* home = std::getenv("HOME");
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  return preferencesPath(plugin, home ? home : "", xdg ? xdg : "");
}

} // namespace FairyTapes::TheStripper
