// Fairy Tapes — Deep Throat: dynamic panel texts derived from the mode table. Pure (no VSTGUI).
// Positions live in ui/layout.h; this file only produces the strings.
#pragma once

#include "../mode_table.h"
#include "../params.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace FairyTapes::DeepThroat::Ui {

namespace detail {
inline double sane(double v, double fallback = 0.5) { return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : fallback; }

inline std::string fmt(const char* f, double v) {
  char buf[48];
  std::snprintf(buf, sizeof buf, f, v);
  return buf;
}

// Preview fs(+(v).toFixed(1)): at most one decimal, trailing ".0" dropped, "+" for positives, no "-0".
inline std::string signedShort(double v) {
  double r = std::round(v * 10.0) / 10.0;
  if (r == 0.0) return "0";
  std::string s = fmt("%.1f", r);
  if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.resize(s.size() - 2);
  return (r > 0 ? "+" : "") + s;
}

inline std::string intText(double v) { return std::to_string(static_cast<long long>(std::llround(v))); }

inline std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

inline std::string trim(const std::string& s) {
  size_t b = s.find_first_not_of(' '), e = s.find_last_not_of(' ');
  return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}
}  // namespace detail

// ---- internal overloads on a ModeRow ----
inline std::string eqHpfText(const ModeRow& r, double a) {
  return detail::intText(r.hpfHz * (0.5 + detail::sane(a))) + " Hz";
}
inline std::string eqMudText(const ModeRow& r, double a) {
  return detail::signedShort(r.mudCutDb * 2.0 * detail::sane(a)) + " dB";
}
inline std::string toneGainText(const ModeRow& r, int band, double tone) {
  const double gain = band == 0 ? r.toneMudDb : (band == 1 ? r.tonePresenceDb : r.toneAirDb);
  const double g = gain * (detail::sane(tone) - 0.5) * 2.0;
  if (std::fabs(g) < 0.05) return "0.0 dB";
  return (g > 0 ? "+" : "") + detail::fmt("%.1f", g) + " dB";
}
inline std::string ratioText(int choice, const ModeRow& r) {
  if (choice <= 0) return "AUTO " + detail::intText(r.compRatio) + ":1";
  return detail::intText(compRatioFromChoice(choice)) + ":1";
}
inline std::string distText(int choice, const ModeRow& r) {
  const char* names[3] = {"OFF", "DIST 2", "DIST 3"};
  if (choice <= 0) return std::string("AUTO ") + names[std::clamp(r.compDist, 1, 3) - 1];
  return names[std::clamp(choice, 1, 3) - 1];
}
inline std::string modeTagLine1(const ModeRow& r) {
  const std::string d = r.description;
  return detail::upper(detail::trim(d.substr(0, d.find(':'))));
}
inline std::string modeTagLine2(const ModeRow& r) {
  const std::string d = r.description;
  const size_t p = d.find(':');
  return p == std::string::npos ? std::string() : detail::upper(detail::trim(d.substr(p + 1)));
}
inline const char* detectorTag(const ModeRow& r) { return r.levelDetector ? "LEVEL" : "SPECTRAL"; }
inline std::string riderMaxText(const ModeRow& r) {
  return "MAX \xC2\xB1" + detail::intText(r.riderMaxRangeDb) + " dB \xC2\xB7 " + detail::intText(r.riderSlewDbPerSec) +
         " dB/s";
}
inline std::string deEssNote(const ModeRow& r) {
  if (r.deEssScale >= 1.0) return {};
  return "MOSTLY OFF IN " + std::string(r.name) + " (X" + detail::fmt("%.2f", r.deEssScale) + ")";
}

// ---- public API ----
inline std::string eqHpfText(Mode m, double a) { return eqHpfText(modeRow(m), a); }
inline std::string eqMudText(Mode m, double a) { return eqMudText(modeRow(m), a); }
inline std::string toneGainText(Mode m, int band, double tone) { return toneGainText(modeRow(m), band, tone); }
inline std::string ratioText(int choice, Mode m) { return ratioText(choice, modeRow(m)); }
inline std::string distText(int choice, Mode m) { return distText(choice, modeRow(m)); }
inline std::string hpfChoiceText(int choice) {
  const double hz = compHpfHzFromChoice(choice);
  return hz <= 0.0 ? "OFF" : detail::intText(hz) + " Hz";
}
inline std::string modeTagLine1(Mode m) { return modeTagLine1(modeRow(m)); }
inline std::string modeTagLine2(Mode m) { return modeTagLine2(modeRow(m)); }
inline const char* detectorTag(Mode m) { return detectorTag(modeRow(m)); }
inline std::string riderMaxText(Mode m) { return riderMaxText(modeRow(m)); }
inline std::string deEssNote(Mode m) { return m == Mode::Puke ? deEssNote(modeRow(m)) : std::string(); }

}  // namespace FairyTapes::DeepThroat::Ui
