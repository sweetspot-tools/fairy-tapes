/** Fairy Tapes · shared TONE3000 client — small file helpers (atomic write, size-limited read). */

#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace FairyTapes::Tone3000 {

// Writes `data` to a temporary file next to `path` and renames it over `path`, so a reader never sees half a file. Creates
// the parent folders. On failure nothing is left behind and the old file (if any) is untouched. With `privateFile` the
// file is created readable and writable by its owner only (mode 0600 from the first byte, on systems that have modes).
inline bool writeFileAtomic(const std::filesystem::path& path, const std::string& data, bool privateFile = false) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
  if (ec) return false;
  fs::path tmp = path;
  tmp += ".part";
#if !defined(_WIN32)
  if (privateFile) {
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    std::size_t off = 0;
    bool ok = true;
    while (ok && off < data.size()) {
      const ssize_t n = ::write(fd, data.data() + off, data.size() - off);
      if (n <= 0) ok = false;
      else off += static_cast<std::size_t>(n);
    }
    ok = ::close(fd) == 0 && ok;
    if (!ok) {
      fs::remove(tmp, ec);
      return false;
    }
    fs::rename(tmp, path, ec);
    if (ec) {
      fs::remove(tmp, ec);
      return false;
    }
    return true;
  }
#else
  (void)privateFile;   // the user's profile folder is private by default on Windows
#endif
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
