/** Fairy Tapes · shared TONE3000 client — small file helpers (atomic write, size-limited read). */

#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

namespace FairyTapes::Tone3000 {

// Writes `data` to a temporary file next to `path` and renames it over `path`, so a reader never sees half a file. Creates
// the parent folders. On failure nothing is left behind and the old file (if any) is untouched.
inline bool writeFileAtomic(const std::filesystem::path& path, const std::string& data) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
  if (ec) return false;
  fs::path tmp = path;
  tmp += ".part";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.flush();
    if (!out) {
      out.close();
      fs::remove(tmp, ec);
      return false;
    }
  }
  fs::rename(tmp, path, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return false;
  }
  return true;
}

// The whole file, or nothing if it is missing, unreadable or longer than `maxBytes`.
inline std::optional<std::string> readFileLimited(const std::filesystem::path& path, std::size_t maxBytes) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return std::nullopt;
  const std::streamoff size = in.tellg();
  if (size < 0 || static_cast<std::size_t>(size) > maxBytes) return std::nullopt;
  std::string data(static_cast<std::size_t>(size), '\0');
  in.seekg(0);
  if (size > 0 && !in.read(data.data(), size)) return std::nullopt;
  return data;
}

} // namespace FairyTapes::Tone3000
