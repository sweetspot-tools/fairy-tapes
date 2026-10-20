/**
 * Fairy Tapes · shared TONE3000 client — the file kept next to every downloaded model (`<model>.t3k.json`): who made it,
 * under which licence, where it comes from. It is what keeps the author and the licence in sight (a condition of the API
 * terms) when the model is listed by the local scan. Everything read from it is limited in length; unknown fields are
 * ignored and missing ones are empty, so older or newer files still load.
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace FairyTapes::Tone3000 {

constexpr std::size_t kMaxMetaString = 200;
constexpr std::size_t kMaxMetaFileBytes = 16 * 1024;

struct ModelMeta {
  std::string toneId, modelId;
  std::string title;            // the tone's title
  std::string authorName;       // display name of the creator
  bool authorVerified = false;
  std::string license;          // as the API says: "t3k", "cc-by", "cc-by-nc", "cco", ...
  std::string pageUrl;          // the tone's page on tone3000.com
  std::string size;             // "standard", "lite", "feather", "nano", "custom"
  std::string architecture;     // "A1", "A2", ...
  std::string downloadedAt;     // ISO 8601 UTC, written by the caller
};

std::filesystem::path sidecarPathFor(const std::filesystem::path& modelFile);   // "x.nam" -> "x.t3k.json"
std::string metaToJson(const ModelMeta& meta);
std::optional<ModelMeta> metaFromJson(const std::string& json);                // nullopt when it is not a JSON object
bool writeMeta(const std::filesystem::path& modelFile, const ModelMeta& meta);
std::optional<ModelMeta> readMeta(const std::filesystem::path& modelFile);

// True for the licences that forbid commercial use ("cc-by-nc", "cc-by-nc-sa", ...): shown with a badge.
bool isNonCommercialLicense(const std::string& license);
// The short text of the licence badge: "T3K", "CC BY", "CC BY-NC", "CC0", or the upper-cased text for an unknown one.
std::string licenseBadge(const std::string& license);

} // namespace FairyTapes::Tone3000
