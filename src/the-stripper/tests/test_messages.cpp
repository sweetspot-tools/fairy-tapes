/**
 * Fairy Tapes · The Stripper — model messages (controller <-> processor): encode/decode round trips.
 * Fake message and attribute list stand in for the host's IMessage / IAttributeList (setBinary/getBinary).
 */

#include "messages.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace FairyTapes::TheStripper;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

struct FakeAttrs {
  std::map<std::string, std::vector<char>> bins;
  std::map<std::string, long long> ints;
  int setBinary(const char* id, const void* d, unsigned n) { bins[id].assign((const char*)d, (const char*)d + n); return 0; }
  int getBinary(const char* id, const void*& d, unsigned& n) { auto it = bins.find(id); if (it == bins.end()) return 1; d = it->second.data(); n = (unsigned)it->second.size(); return 0; }
  int setInt(const char* id, long long v) { ints[id] = v; return 0; }
  int getInt(const char* id, long long& v) { auto it = ints.find(id); if (it == ints.end()) return 1; v = it->second; return 0; }
};
struct FakeMessage {
  std::string id; FakeAttrs attrs;
  void setMessageID(const char* s) { id = s; }
  const char* getMessageID() { return id.c_str(); }
  FakeAttrs* getAttributes() { return &attrs; }
};

void test_load_message_roundtrip() {
  FakeMessage m;
  const std::string path = "/home/utente/Modelli/Chitarra \xC3\xA8 \xE2\x82\xAC \xF0\x9F\x8E\xB8/amp.nam";   // accents, euro sign, an emoji
  encodeLoadMessage(&m, path);
  CHECK(m.id == kMsgLoadModel);
  std::string out;
  CHECK(decodeLoadMessage(&m, out));
  CHECK(out == path);
}

void test_load_message_empty_path_means_unload() {
  FakeMessage m; encodeLoadMessage(&m, "");
  std::string out = "x";
  CHECK(decodeLoadMessage(&m, out));
  CHECK(out.empty());
}

void test_load_message_rejects_wrong_or_incomplete() {
  std::string out;
  { FakeMessage m; m.setMessageID("Other"); m.attrs.setBinary("path", "a", 1); CHECK(!decodeLoadMessage(&m, out)); }
  { FakeMessage m; m.setMessageID(kMsgLoadModel); CHECK(!decodeLoadMessage(&m, out)); }                       // no path attribute
  { FakeMessage m; encodeLoadMessage(&m, std::string(kMaxMessageString + 1, 'a')); CHECK(!decodeLoadMessage(&m, out)); }   // absurdly long
  CHECK(!decodeLoadMessage(static_cast<FakeMessage*>(nullptr), out));
}

void test_status_message_roundtrip() {
  FakeMessage m;
  ModelStatus in{2, "Amp \xC3\xA8.nam", ""};
  encodeStatusMessage(&m, in);
  CHECK(m.id == kMsgModelStatus);
  ModelStatus out;
  CHECK(decodeStatusMessage(&m, out));
  CHECK(out.state == 2 && out.name == in.name && out.error.empty());
  FakeMessage f; encodeStatusMessage(&f, ModelStatus{3, "", "File not found"});
  CHECK(decodeStatusMessage(&f, out));
  CHECK(out.state == 3 && out.name.empty() && out.error == "File not found");
}

void test_status_message_rejects_wrong_or_incomplete() {
  ModelStatus out;
  { FakeMessage m; m.setMessageID("Nope"); CHECK(!decodeStatusMessage(&m, out)); }
  { FakeMessage m; m.setMessageID(kMsgModelStatus); CHECK(!decodeStatusMessage(&m, out)); }
}
} // namespace

int main() {
  std::printf("model message tests\n");
  test_load_message_roundtrip();
  test_load_message_empty_path_means_unload();
  test_load_message_rejects_wrong_or_incomplete();
  test_status_message_roundtrip();
  test_status_message_rejects_wrong_or_incomplete();
  std::printf(g_failed ? "FAILED (%d)\n" : "All message tests passed\n", g_failed);
  return g_failed ? 1 : 0;
}
