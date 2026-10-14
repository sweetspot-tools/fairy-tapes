/**
 * Fairy Tapes · The Stripper — model loading through messages, on the real module.
 * dlopens the bundle and plays the host: a mock IHostApplication that allocates messages, a mock peer that records the
 * "ModelStatus" messages, mock streams for the state. Checks: a "LoadModel" message loads the model and the status
 * comes back (Loaded, name, no error); a missing file reports Failed with an error; an empty path unloads; the model
 * path is saved in the state and a fresh processor restores it. Exits 77 (skipped) when the module is absent.
 */

#include "cids.h"
#include "messages.h"
#include "state.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>
#include <dlfcn.h>

#ifndef THE_STRIPPER_MODULE
#error THE_STRIPPER_MODULE must point to the built module binary
#endif
#ifndef NAM_DRIVE_FIXTURES
#error NAM_DRIVE_FIXTURES must point to the nam_drive tests/fixtures directory
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;
using FairyTapes::TheStripper::ModelStatus;
using FairyTapes::TheStripper::PluginState;

namespace {
constexpr int kSkip = 77;
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

struct MockAttrs : IAttributeList {
  std::map<std::string, std::vector<char>> bins;
  tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
  tresult PLUGIN_API setInt(AttrID, int64) override { return kResultFalse; }
  tresult PLUGIN_API getInt(AttrID, int64&) override { return kResultFalse; }
  tresult PLUGIN_API setFloat(AttrID, double) override { return kResultFalse; }
  tresult PLUGIN_API getFloat(AttrID, double&) override { return kResultFalse; }
  tresult PLUGIN_API setString(AttrID, const TChar*) override { return kResultFalse; }
  tresult PLUGIN_API getString(AttrID, TChar*, uint32) override { return kResultFalse; }
  tresult PLUGIN_API setBinary(AttrID id, const void* d, uint32 n) override { bins[id].assign((const char*)d, (const char*)d + n); return kResultOk; }
  tresult PLUGIN_API getBinary(AttrID id, const void*& d, uint32& n) override {
    auto it = bins.find(id); if (it == bins.end()) return kResultFalse; d = it->second.data(); n = (uint32)it->second.size(); return kResultOk; }
};
struct MockMessage : IMessage {
  std::atomic<int> refs{1};
  std::string id; MockAttrs attrs;
  tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
  uint32 PLUGIN_API addRef() override { return (uint32)++refs; }
  uint32 PLUGIN_API release() override { int r = --refs; if (r == 0) delete this; return (uint32)r; }
  FIDString PLUGIN_API getMessageID() override { return id.c_str(); }
  void PLUGIN_API setMessageID(FIDString s) override { id = s; }
  IAttributeList* PLUGIN_API getAttributes() override { return &attrs; }
};
struct MockHost : IHostApplication {
  tresult PLUGIN_API queryInterface(const TUID _iid, void** o) override {
    if (FUnknownPrivate::iidEqual(_iid, IHostApplication::iid) || FUnknownPrivate::iidEqual(_iid, FUnknown::iid)) { *o = static_cast<IHostApplication*>(this); return kResultOk; }
    *o = nullptr; return kNoInterface; }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
  tresult PLUGIN_API getName(String128 n) override { n[0] = 'H'; n[1] = 0; return kResultOk; }
  tresult PLUGIN_API createInstance(TUID, TUID _iid, void** obj) override {
    if (FUnknownPrivate::iidEqual(_iid, IMessage::iid)) { *obj = static_cast<IMessage*>(new MockMessage); return kResultOk; }
    *obj = nullptr; return kNoInterface; }
};
struct MockPeer : IConnectionPoint {
  std::mutex m; std::vector<ModelStatus> got;
  tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
  tresult PLUGIN_API connect(IConnectionPoint*) override { return kResultOk; }
  tresult PLUGIN_API disconnect(IConnectionPoint*) override { return kResultOk; }
  tresult PLUGIN_API notify(IMessage* msg) override {
    ModelStatus st; if (!FairyTapes::TheStripper::decodeStatusMessage(msg, st)) return kResultFalse;
    std::lock_guard<std::mutex> lk(m); got.push_back(st); return kResultOk; }
  bool waitFor(int state, int ms = 5000) {
    const auto t0 = std::chrono::steady_clock::now();
    for (;;) {
      { std::lock_guard<std::mutex> lk(m); for (auto& s : got) if (s.state == state) return true; }
      if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(ms)) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
  ModelStatus lastWith(int state) { std::lock_guard<std::mutex> lk(m); ModelStatus r; for (auto& s : got) if (s.state == state) r = s; return r; }
  void clear() { std::lock_guard<std::mutex> lk(m); got.clear(); }
};
struct MockStream : IBStream {
  std::vector<char> data; size_t pos = 0;
  tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
  tresult PLUGIN_API read(void* b, int32 n, int32* r) override { int32 k = (int32)std::min<size_t>((size_t)n, data.size() - pos); std::memcpy(b, data.data() + pos, (size_t)k); pos += (size_t)k; if (r) *r = k; return kResultOk; }
  tresult PLUGIN_API write(void* b, int32 n, int32* w) override { data.insert(data.end(), (char*)b, (char*)b + n); if (w) *w = n; return kResultOk; }
  tresult PLUGIN_API seek(int64 p, int32 mode, int64* r) override { if (mode == kIBSeekSet) pos = (size_t)p; else if (mode == kIBSeekCur) pos += (size_t)p; else pos = data.size() + (size_t)p; if (r) *r = (int64)pos; return kResultOk; }
  tresult PLUGIN_API tell(int64* p) override { if (p) *p = (int64)pos; return kResultOk; }
};
/// Reader over the saved bytes for readState (the stream is little-endian on this platform).
struct Reader {
  const std::vector<char>& d; size_t p = 0;
  bool readInt32(std::int32_t& v) { if (p + 4 > d.size()) return false; std::memcpy(&v, d.data() + p, 4); p += 4; return true; }
  bool readDouble(double& v) { if (p + 8 > d.size()) return false; std::memcpy(&v, d.data() + p, 8); p += 8; return true; }
  long long readRaw(void* b, std::int32_t n) { if (p + (size_t)n > d.size()) return 0; std::memcpy(b, d.data() + p, (size_t)n); p += (size_t)n; return n; }
};

/// Writer for writeState into a byte vector (little-endian, as on this platform).
struct Writer {
  std::vector<char>& d;
  bool writeInt32(std::int32_t v) { d.insert(d.end(), (char*)&v, (char*)&v + 4); return true; }
  bool writeDouble(double v) { d.insert(d.end(), (char*)&v, (char*)&v + 8); return true; }
  long long writeRaw(const void* b, std::int32_t n) { d.insert(d.end(), (const char*)b, (const char*)b + n); return n; }
};

struct Plugin {
  void* h = nullptr; IComponent* comp = nullptr; IConnectionPoint* cp = nullptr; MockHost host; MockPeer peer;
  std::vector<float> in, out; int dummy = 0;
  bool open(void* existing = nullptr) {
    h = existing ? existing : dlopen(THE_STRIPPER_MODULE, RTLD_NOW | RTLD_LOCAL);
    if (!h) return false;
    auto entry = (bool (*)(void*))dlsym(h, "ModuleEntry"); auto getFactory = (IPluginFactory * (*)())dlsym(h, "GetPluginFactory");
    if (!entry || !getFactory) return false;
    entry(h);
    IPluginFactory* f = getFactory(); FUnknown* obj = nullptr;
    if (f->createInstance(FairyTapes::TheStripper::kProcessorUID.toTUID(), IComponent::iid, (void**)&obj) != kResultOk || !obj) return false;
    comp = static_cast<IComponent*>(obj);
    if (comp->initialize(&host) != kResultOk) return false;
    IAudioProcessor* ap = nullptr; comp->queryInterface(IAudioProcessor::iid, (void**)&ap);
    if (ap) { ProcessSetup su{kRealtime, kSample32, 64, 48000.0}; ap->setupProcessing(su); ap->release(); }
    comp->setActive(true);
    if (comp->queryInterface(IConnectionPoint::iid, (void**)&cp) != kResultOk || !cp) return false;
    cp->connect(&peer);
    f->release();
    return true;
  }
  void sendLoad(const std::string& path) { auto* m = new MockMessage; FairyTapes::TheStripper::encodeLoadMessage(m, path); cp->notify(m); m->release(); }
  void close() { if (cp) cp->release(); if (comp) { comp->setActive(false); comp->terminate(); comp->release(); } }
};

const std::string kFixture = std::string(NAM_DRIVE_FIXTURES) + "/linear_unity.nam";

void test_load_message_loads_and_reports_status() {
  Plugin p; CHECK(p.open());
  CHECK(p.peer.waitFor(0));                                    // on connect the controller learns "idle"
  p.sendLoad(kFixture);
  CHECK(p.peer.waitFor(1));                                    // loading, at once
  CHECK(p.peer.waitFor(2));                                    // then loaded
  ModelStatus ok = p.peer.lastWith(2);
  CHECK(ok.name == "linear_unity.nam" && ok.error.empty());
  p.close();
}

void test_missing_file_reports_failed_and_empty_path_unloads() {
  Plugin p; CHECK(p.open());
  p.sendLoad(std::string(NAM_DRIVE_FIXTURES) + "/does_not_exist.nam");
  CHECK(p.peer.waitFor(3));
  ModelStatus bad = p.peer.lastWith(3);
  CHECK(!bad.error.empty() && bad.name.empty());
  p.peer.clear();
  p.sendLoad(kFixture); CHECK(p.peer.waitFor(2));
  p.peer.clear();
  p.sendLoad("");                                              // "No model"
  CHECK(p.peer.waitFor(0));
  ModelStatus idle = p.peer.lastWith(0);
  CHECK(idle.name.empty() && idle.error.empty());
  p.close();
}

void test_model_path_is_saved_and_restored() {
  std::vector<char> saved;
  {
    Plugin p; CHECK(p.open());
    p.sendLoad(kFixture); CHECK(p.peer.waitFor(2));
    MockStream s; IComponent* c = p.comp;
    CHECK(c->getState(&s) == kResultOk);
    saved = s.data;
    PluginState st; Reader r{saved};
    CHECK(FairyTapes::TheStripper::readState(r, st));
    CHECK(st.modelPath == kFixture);
    p.close();
  }
  {
    Plugin p2; CHECK(p2.open());
    MockStream s; s.data = saved;
    CHECK(p2.comp->setState(&s) == kResultOk);                 // a project restored with this model loads it again
    CHECK(p2.peer.waitFor(2));
    CHECK(p2.peer.lastWith(2).name == "linear_unity.nam");
    p2.close();
  }
}
// The controller keeps the editor data (wear seed, writing on the tape) in its own state, apart from the processor's.
void test_controller_state_keeps_seed_and_tape() {
  void* h = dlopen(THE_STRIPPER_MODULE, RTLD_NOW | RTLD_LOCAL); CHECK(h != nullptr);
  if (!h) return;
  auto entry = (bool (*)(void*))dlsym(h, "ModuleEntry"); auto getFactory = (IPluginFactory * (*)())dlsym(h, "GetPluginFactory");
  entry(h);
  IPluginFactory* f = getFactory(); FUnknown* obj = nullptr;
  CHECK(f->createInstance(FairyTapes::TheStripper::kControllerUID.toTUID(), IEditController::iid, (void**)&obj) == kResultOk && obj);
  auto* ctl = static_cast<IEditController*>(obj);
  MockHost host;
  CHECK(ctl->initialize(&host) == kResultOk);

  auto readBack = [&](PluginState& st) {
    MockStream s; CHECK(ctl->getState(&s) == kResultOk);
    Reader r{s.data}; CHECK(FairyTapes::TheStripper::readState(r, st));
  };
  PluginState first; readBack(first);
  CHECK(first.wearSeed != 0);                                  // a fresh instance made a seed
  CHECK(first.tapeTexts.empty());

  // a project state with a seed and writing on the tape restores both
  PluginState in; FairyTapes::TheStripper::fillDefaultParams(in.params);
  in.wearSeed = 0xABCDEF01u; in.tapeTexts = {{true, "VOX"}, {false, ""}, {true, "7/12"}};
  { MockStream s; Writer w{s.data}; CHECK(FairyTapes::TheStripper::writeState(w, in)); CHECK(ctl->setState(&s) == kResultOk); }
  PluginState got; readBack(got);
  CHECK(got.wearSeed == 0xABCDEF01u && got.tapeTexts.size() == 3);
  CHECK(got.tapeTexts[0].set && got.tapeTexts[0].text == "VOX" && !got.tapeTexts[1].set && got.tapeTexts[2].text == "7/12");

  // the processor's state (no seed, no tape) loaded as component state must not wipe the editor data
  PluginState proc; FairyTapes::TheStripper::fillDefaultParams(proc.params);
  { MockStream s; Writer w{s.data}; CHECK(FairyTapes::TheStripper::writeState(w, proc)); CHECK(ctl->setComponentState(&s) == kResultOk); }
  PluginState after; readBack(after);
  CHECK(after.wearSeed == 0xABCDEF01u && after.tapeTexts.size() == 3);

  // an older project (v9: no seed) keeps the seed of this instance
  { MockStream s; s.data.resize(0); Writer w{s.data}; w.writeInt32(9); for (int i = 0; i < FairyTapes::TheStripper::kNumParams; ++i) w.writeDouble(0.5); w.writeInt32(0);
    CHECK(ctl->setState(&s) == kResultOk); }
  PluginState old; readBack(old);
  CHECK(old.wearSeed == 0xABCDEF01u);

  ctl->terminate(); ctl->release(); f->release();
  auto ex = (bool (*)())dlsym(h, "ModuleExit"); if (ex) ex();
  dlclose(h);
}
} // namespace

int main() {
  struct stat st{};
  if (stat(THE_STRIPPER_MODULE, &st) != 0) { std::printf("SKIP: module not built: %s\n", THE_STRIPPER_MODULE); return kSkip; }
  test_load_message_loads_and_reports_status();
  test_missing_file_reports_failed_and_empty_path_unloads();
  test_model_path_is_saved_and_restored();
  test_controller_state_keeps_seed_and_tape();
  std::printf(g_failed ? "Model message module tests FAILED\n" : "Model message module tests OK\n");
  return g_failed ? 1 : 0;
}
