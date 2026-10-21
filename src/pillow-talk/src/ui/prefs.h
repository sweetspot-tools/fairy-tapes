/**
 * Fairy Tapes · Pillow Talk — user preferences: a small text file in the user's preferences folder, valid for every
 * project and instance, never part of the plug-in state. Pure C++ (no SDK, no VSTGUI), unit tested
 * (tests/test_prefs.cpp).
 *
 * Same format and rules as The Stripper's preferences (src/the-stripper/src/preferences.h), in its own file:
 *   <preferences folder>/FairyTapes/PillowTalk.prefs
 *   Linux:   $XDG_CONFIG_HOME (if absolute) or ~/.config
 *   macOS:   ~/Library/Application Support
 *   Windows: %APPDATA%
 * UTF-8 lines "key=value"; '#' starts a comment; unknown keys and bad lines are ignored; a missing, unreadable,
 * oversized or binary file means defaults (each invalid value falls back on its own). Keys (stable, never renamed;
 * the same names as The Stripper's): wear=Off|Light|Heavy, notes=Show|Hide, tapeFont=Zeyada|RockSalt|HomemadeApple,
 * valuesFont=Tape|Standard. Writes are atomic (temporary file, then rename); a failed write is reported, never fatal.
 */

#pragma once

#include "fairy_ui/wear.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace FairyTapes::PillowTalk::Ui {

using FairyTapes::Ui::WearLevel;

enum class TapeFont { Zeyada, RockSalt, HomemadeApple };

constexpr std::uintmax_t kMaxPrefsBytes = 64 * 1024;

struct Prefs {
  WearLevel wear = WearLevel::Light;
  bool notes = true;               // NOTES LAYER (tape and pen marks on the values): stored, nothing drawn yet
  TapeFont tapeFont = TapeFont::Zeyada;
  bool handwrittenValues = true;   // yellow values in the tape font (valuesFont=Tape) or plain (Standard)
};

Prefs parsePrefs(const std::string& text);
std::string serializePrefs(const Prefs& p);

// Defaults on any problem.
Prefs loadPrefs(const std::filesystem::path& file);
// Creates the folders; false on failure (nothing else happens).
bool savePrefs(const std::filesystem::path& file, const Prefs& p);

// The file for the given folders (parameters so tests do not touch the real ones): `home` = the user's home,
// `configHome` = $XDG_CONFIG_HOME (Linux), `appData` = %APPDATA% (Windows). Empty when the base folder is unknown.
std::filesystem::path prefsPathFor(const std::string& home, const std::string& configHome, const std::string& appData);
// The real file for this user, from the environment.
std::filesystem::path prefsPath();

} // namespace FairyTapes::PillowTalk::Ui
