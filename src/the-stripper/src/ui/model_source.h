/**
 * Fairy Tapes · The Stripper — sources of .nam models for the picker popup (spec 3.1), plus the model-name helpers.
 * Header-only and free of VSTGUI so it is unit tested. The local folder scan is blocking: the editor runs it on a
 * worker thread.
 */

#pragma once

#include "fairy_ui/tape_font.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace FairyTapes::TheStripper::Ui {

using FairyTapes::Ui::truncateToWidth;   // moved to the common tape kit

struct ModelEntry {
  std::string name;     // file name with extension
  std::string path;     // absolute path, UTF-8
  std::string folder;   // the configured folder it was found under
  std::uint64_t bytes = 0;
};

// UTF-8 <-> std::filesystem::path (C++20 u8string() returns std::u8string).
inline std::string toUtf8(const std::filesystem::path& p) {
  const std::u8string u = p.u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}
inline std::filesystem::path fromUtf8(const std::string& s) {
  return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

inline std::string lowerAscii(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

inline bool hasNamExtension(const std::filesystem::path& p) { return lowerAscii(p.extension().string()) == ".nam"; }

// Recursive scan of the folders for .nam files, sorted by name (case-insensitive). Missing folders and unreadable
// entries are skipped, directory symlinks are not followed, the same file is listed once. `maxDepth` counts the
// levels below each folder (0 = only the folder itself).
inline std::vector<ModelEntry> scanFolders(const std::vector<std::string>& folders, std::size_t maxEntries = 5000,
                                           int maxDepth = 8) {
  namespace fs = std::filesystem;
  std::vector<ModelEntry> out;
  std::set<std::string> seen;
  for (const auto& folder : folders) {
    if (folder.empty() || out.size() >= maxEntries) continue;
    std::error_code ec;
    const fs::path root = fs::absolute(fromUtf8(folder), ec);
    if (ec || !fs::is_directory(root, ec)) continue;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end && out.size() < maxEntries; it.increment(ec)) {
      if (it.depth() > maxDepth) {
        it.disable_recursion_pending();
        continue;
      }
      std::error_code e2;
      if (it->is_symlink(e2)) {
        it.disable_recursion_pending();
        if (!fs::is_regular_file(it->path(), e2)) continue;   // a link to a directory: not followed
      }
      if (!it->is_regular_file(e2) || e2 || !hasNamExtension(it->path())) continue;
      if (it.depth() >= maxDepth + 1) continue;
      ModelEntry m;
      m.path = toUtf8(it->path());
      if (!seen.insert(m.path).second) continue;
      m.name = toUtf8(it->path().filename());
      m.folder = folder;
      m.bytes = static_cast<std::uint64_t>(it->file_size(e2));
      if (e2) m.bytes = 0;
      out.push_back(std::move(m));
    }
  }
  std::sort(out.begin(), out.end(), [](const ModelEntry& a, const ModelEntry& b) {
    const std::string la = lowerAscii(a.name), lb = lowerAscii(b.name);
    return la != lb ? la < lb : a.path < b.path;
  });
  return out;
}

// Entries whose name contains `query` (trimmed, case-insensitive), optionally only those of one folder.
inline std::vector<ModelEntry> filterModels(const std::vector<ModelEntry>& all, const std::string& query,
                                            const std::string& folder = {}) {
  std::string q = query;
  while (!q.empty() && std::isspace(static_cast<unsigned char>(q.front()))) q.erase(q.begin());
  while (!q.empty() && std::isspace(static_cast<unsigned char>(q.back()))) q.pop_back();
  q = lowerAscii(q);
  std::vector<ModelEntry> out;
  for (const auto& m : all) {
    if (!folder.empty() && m.folder != folder) continue;
    if (!q.empty() && lowerAscii(m.name).find(q) == std::string::npos) continue;
    out.push_back(m);
  }
  return out;
}

// The hint on the model strip (the strip itself shows a name shortened to fit): the full file name once a model is loaded,
// why a load failed, or what a click does. With the NAM off it names the engine at work instead. `state` is the
// controller's model state (0 idle, 1 loading, 2 loaded, 3 failed).
inline std::string stripTooltip(int state, const std::string& name, const std::string& error) {
  if (state == 1) return "Loading the model";
  if (state == 2 && !name.empty()) return name;
  if (state == 3) return error.empty() ? "Could not load the model" : error;
  return "Click to choose a NAM model";
}

// File name without directory and without the .nam extension (kept as is when nothing would remain).
inline std::string displayName(const std::string& pathOrName) {
  std::string name = toUtf8(fromUtf8(pathOrName).filename());
  if (name.size() > 4 && lowerAscii(name.substr(name.size() - 4)) == ".nam") name.resize(name.size() - 4);
  return name;
}

inline std::size_t utf8Length(const std::string& s) {
  std::size_t n = 0;
  for (unsigned char c : s)
    if ((c & 0xC0) != 0x80) ++n;
  return n;
}

} // namespace FairyTapes::TheStripper::Ui
