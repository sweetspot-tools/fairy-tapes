// Fairy Tapes · shared TONE3000 client — the sidecar file of a downloaded model (see metadata.h).
#include "tone3000/metadata.h"

#include "tone3000/file_util.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace FairyTapes::Tone3000 {

namespace {
using Json = nlohmann::json;

// Text from outside: at most kMaxMetaString bytes, cut at a character boundary, control characters turned into spaces.
std::string clean(const Json& j, const char* key) {
  if (!j.is_object()) return {};
  const auto it = j.find(key);
  if (it == j.end() || !it->is_string()) return {};
  std::string s = it->get<std::string>();
  for (char& c : s)
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) c = ' ';
  if (s.size() > kMaxMetaString) {
    std::size_t cut = kMaxMetaString;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    s.resize(cut);
  }
  return s;
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
} // namespace

std::filesystem::path sidecarPathFor(const std::filesystem::path& modelFile) {
  std::filesystem::path p = modelFile;
  p.replace_extension(".t3k.json");
  return p;
}

std::string metaToJson(const ModelMeta& m) {
  Json j = {{"tone_id", m.toneId},         {"model_id", m.modelId},     {"title", m.title},
            {"author", m.authorName},      {"author_verified", m.authorVerified},
            {"license", m.license},        {"page_url", m.pageUrl},     {"size", m.size},
            {"architecture", m.architecture}, {"downloaded_at", m.downloadedAt}};
  return j.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

std::optional<ModelMeta> metaFromJson(const std::string& text) {
  if (text.size() > kMaxMetaFileBytes) return std::nullopt;
  const Json j = Json::parse(text, nullptr, /*allow_exceptions*/ false);
  if (!j.is_object()) return std::nullopt;
  ModelMeta m;
  m.toneId = clean(j, "tone_id");
  m.modelId = clean(j, "model_id");
  m.title = clean(j, "title");
  m.authorName = clean(j, "author");
  const auto v = j.find("author_verified");
  m.authorVerified = v != j.end() && v->is_boolean() && v->get<bool>();
  m.license = clean(j, "license");
  m.pageUrl = clean(j, "page_url");
  m.size = clean(j, "size");
  m.architecture = clean(j, "architecture");
  m.downloadedAt = clean(j, "downloaded_at");
  return m;
}

bool writeMeta(const std::filesystem::path& modelFile, const ModelMeta& meta) {
  return writeFileAtomic(sidecarPathFor(modelFile), metaToJson(meta));
}

std::optional<ModelMeta> readMeta(const std::filesystem::path& modelFile) {
  const auto text = readFileLimited(sidecarPathFor(modelFile), kMaxMetaFileBytes);
  return text ? metaFromJson(*text) : std::nullopt;
}

bool isNonCommercialLicense(const std::string& license) {
  const std::string l = lower(license);
  return l.find("-nc") != std::string::npos;
}

std::string licenseBadge(const std::string& license) {
  const std::string l = lower(license);
  if (l.empty()) return "?";
  if (l == "t3k") return "T3K";
  if (l == "cco" || l == "cc0") return "CC0";
  if (l.rfind("cc-", 0) == 0) {
    std::string out = "CC";
    std::string part;
    for (std::size_t i = 3; i <= l.size(); ++i) {
      if (i == l.size() || l[i] == '-') {
        std::string up;
        for (char c : part) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        out += (out == "CC" ? " " : "-") + up;
        part.clear();
      } else {
        part += l[i];
      }
    }
    return out;
  }
  std::string up;
  for (char c : license) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return up.size() > 12 ? up.substr(0, 12) : up;
}

} // namespace FairyTapes::Tone3000
