/**
 * Fairy Tapes · The Stripper — the texts of the TONE3000 tab, as pure functions (unit tested). The terms of the API ask that
 * the author, the licence and the tone's name stay in sight: attributionLine() is what the tab shows for the tone being
 * looked at, and every row of the lists carries the author and the licence badge.
 */

#pragma once

#include "tone3000_actions.h"

#include "tone3000/metadata.h"

#include <cctype>
#include <cstdint>
#include <string>

namespace FairyTapes::TheStripper::Ui {

using Tone3000::ClientState;
using Tone3000::ListKind;
using Tone3000::Model;
using Tone3000::Tone;

inline std::string toneRowText(const Tone& t) { return t.title.empty() ? "Untitled tone" : t.title; }

// "Jane Doe  CC BY" (the licence badge is drawn in its own colour; this is the text of the meta column)
inline std::string toneRowMeta(const Tone& t) {
  const std::string who = t.author.shownName();
  return (who.empty() ? std::string("unknown author") : who) + " \xC2\xB7 " + Tone3000::licenseBadge(t.license);
}

inline std::string modelRowText(const Model& m) { return m.name.empty() ? "Model " + m.id : m.name; }

inline std::string modelRowMeta(const Model& m) {
  std::string size = m.size.empty() ? "?" : m.size;
  for (char& c : size) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return size + " \xC2\xB7 " + (m.architecture.empty() ? "?" : m.architecture);
}

// Who made the tone, as shown: "Jane Doe (verified)" or "unknown author".
inline std::string authorText(const Tone& t) {
  std::string who = t.author.shownName();
  if (who.empty()) who = "unknown author";
  if (t.author.verified) who += " (verified)";
  return who;
}

// The licence badge of a tone ("CC BY-NC", "?" when the API said nothing).
inline std::string badgeText(const Tone& t) { return Tone3000::licenseBadge(t.license); }

// The tone, its author and its licence stay in sight with the models of a tone (terms of the API): the title is one line
// (cut to fit), the credit is the other: "by <author> . <licence>" where only the author may be cut, never the licence.
inline std::string creditLine(const Tone& t, const std::string& author) { return "by " + author + " \xC2\xB7 " + badgeText(t); }

inline std::string licenseNote(const std::string& license) {
  if (Tone3000::isNonCommercialLicense(license)) return "Non-commercial license (check before commercial use)";
  return {};
}

inline const char* listKindLabel(ListKind k) {
  switch (k) {
  case ListKind::Favorited: return "FAVORITES";
  case ListKind::Downloaded: return "DOWNLOADED";
  case ListKind::Created: return "CREATED";
  case ListKind::Trending: return "TRENDING";
  case ListKind::Latest: return "LATEST";
  }
  return "";
}

inline bool listKindIsPaged(ListKind k) { return k != ListKind::Trending && k != ListKind::Latest; }

inline std::string byteText(std::uint64_t bytes) {
  if (bytes >= 1024ull * 1024ull) return std::to_string(bytes / (1024 * 1024)) + "." + std::to_string((bytes % (1024 * 1024)) * 10 / (1024 * 1024)) + " MB";
  return std::to_string((bytes + 1023) / 1024) + " KB";
}

// A model can be downloaded and loaded: signed in, idle, a tone with that model, and the tone holds NAM models (not impulse responses).
inline bool canDownload(const ClientState& s, int modelIndex) {
  if (s.phase != ClientState::Phase::SignedIn || s.busy || !s.tone) return false;
  if (modelIndex < 0 || modelIndex >= static_cast<int>(s.models.size())) return false;
  return s.tone->format.empty() || s.tone->format == "nam";
}

// One line for the status area under the lists: what is going on, or what went wrong.
inline std::string statusText(const ClientState& s) {
  using DP = ClientState::DownloadPhase;
  if (s.busy) return s.busyText.empty() ? "Working..." : s.busyText;
  if (s.download == DP::Running) return "Downloading... " + byteText(s.downloadBytes);
  if (s.download == DP::Failed) return s.downloadMessage;
  if (!s.message.empty()) return s.message;
  if (s.download == DP::Done) return "Saved. Loading the model...";
  return {};
}

} // namespace FairyTapes::TheStripper::Ui
