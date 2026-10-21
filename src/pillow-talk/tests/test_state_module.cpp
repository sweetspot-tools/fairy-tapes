/**
 * Fairy Tapes · Pillow Talk — state v2 and the "Decor" messages, through the real module.
 * dlopens the built bundle, creates the processor and the controller from the factory with a minimal host (SDK
 * hostclasses: IHostApplication + IMessage) and a fake peer that records the decor messages:
 *   - a fresh processor, a v1 state and a v2 state with seed 0 all end with a non-zero seed, kept by getState;
 *   - the processor tells its peer the decor after setState and on connect, and adopts the decor the controller sends;
 *   - the controller, given a state without a seed, generates one only when it has none and sends it to its peer.
 * Exits 77 (skipped) when the module has not been built.
 */

#include "cids.h"
#include "messages.h"
#include "params.h"
#include "state.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#include <sys/stat.h>
#include <dlfcn.h>

#ifndef PILLOW_TALK_MODULE
#error PILLOW_TALK_MODULE must point to the built module binary
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace FairyTapes::PillowTalk;

namespace {
constexpr int kSkip = 77;
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

// Minimal in-memory IBStream.
struct MemStream : IBStream {
  std::vector<uint8_t> data;
  size_t pos = 0;
  tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
  tresult PLUGIN_API read(void* buffer, int32 n, int32* got) override {
    const size_t k = n <= 0 ? 0 : std::min<size_t>((size_t)n, data.size() - pos);
    if (k) std::memcpy(buffer, data.data() + pos, k);
    pos += k;
    if (got) *got = (int32)k;
    return kResultOk;
  }
  tresult PLUGIN_API write(void* buffer, int32 n, int32* put) override {
    if (n > 0) data.insert(data.begin() + (long)pos, (uint8_t*)buffer, (uint8_t*)buffer + n);
    pos += n > 0 ? (size_t)n : 0;
    if (put) *put = n;
    return kResultOk;
  }
  tresult PLUGIN_API seek(int64 p, int32 mode, int64* result) override {
    const int64 base = mode == kIBSeekSet ? 0 : mode == kIBSeekCur ? (int64)pos : (int64)data.size();
    pos = (size_t)std::clamp<int64>(base + p, 0, (int64)data.size());
    if (result) *result = (int64)pos;
    return kResultOk;
  }
  tresult PLUGIN_API tell(int64* p) override { if (p) *p = (int64)pos; return kResultOk; }
};

// Records the decor messages it receives (stands for the other side of the connection).
struct Peer : IConnectionPoint {
  std::vector<Decor> got;
  tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
  tresult PLUGIN_API connect(IConnectionPoint*) override { return kResultOk; }
  tresult PLUGIN_API disconnect(IConnectionPoint*) override { return kResultOk; }
  tresult PLUGIN_API notify(IMessage* m) override {
    Decor d;
    if (decodeDecorMessage(m, d)) got.push_back(d);
    return kResultOk;
  }
};

HostApplication g_host;

IMessage* newMessage() {
  TUID iid;
  std::memcpy(iid, IMessage::iid, sizeof(TUID));
  IMessage* msg = nullptr;
  return g_host.createInstance(iid, iid, (void**)&msg) == kResultOk ? msg : nullptr;
}

struct Module {
  void* h = nullptr;
  IPluginFactory* f = nullptr;
  bool open() {
    h = dlopen(PILLOW_TALK_MODULE, RTLD_NOW | RTLD_LOCAL);
    if (!h) { std::fprintf(stderr, "dlopen failed: %s\n", dlerror()); return false; }
    auto entry = (bool (*)(void*))dlsym(h, "ModuleEntry");
    auto getFactory = (IPluginFactory * (*)())dlsym(h, "GetPluginFactory");
    if (!entry || !getFactory) return false;
    entry(h);
    f = getFactory();
    return f != nullptr;
  }
  template <class T>
  T* create(const FUID& cid, const TUID iid) {
    FUnknown* obj = nullptr;
    if (f->createInstance(cid.toTUID(), iid, (void**)&obj) != kResultOk || !obj) return nullptr;
    return static_cast<T*>(obj);
  }
  void close() {
    if (f) f->release();
    auto ex = (bool (*)())dlsym(h, "ModuleExit"); if (ex) ex();
    dlclose(h);
  }
};

PluginState readBack(IComponent* comp) {
  MemStream s;
  CHECK(comp->getState(&s) == kResultOk);
  PluginState st;
  defaultState(st);
  CHECK(deserializeState(s.data.data(), s.data.size(), st));
  CHECK((int32_t)detail::getLE(s.data.data(), 4) == kStateVersion);
  return st;
}

MemStream streamOf(const std::vector<uint8_t>& bytes) {
  MemStream s;
  s.data = bytes;
  return s;
}

std::vector<uint8_t> v1Bytes(const double* p) {
  std::vector<uint8_t> out;
  detail::putLE(out, 1u, 4);
  for (int i = 0; i < (int)kNumParams; ++i) {
    uint64_t bits;
    std::memcpy(&bits, &p[i], sizeof bits);
    detail::putLE(out, bits, 8);
  }
  return out;
}

IConnectionPoint* connectionOf(FUnknown* obj) {
  IConnectionPoint* cp = nullptr;
  obj->queryInterface(IConnectionPoint::iid, (void**)&cp);
  return cp;
}

void test_processor_seed_and_state(Module& m) {
  auto* comp = m.create<IComponent>(kProcessorUID, IComponent::iid);
  CHECK(comp && comp->initialize(&g_host) == kResultOk);
  if (!comp) return;

  // Fresh instance: a seed exists before any setState and stays the same.
  const PluginState fresh = readBack(comp);
  CHECK(fresh.decor.seed != 0);
  CHECK(readBack(comp).decor.seed == fresh.decor.seed);

  // The processor tells its peer on connect.
  Peer peer;
  IConnectionPoint* cp = connectionOf(comp);
  CHECK(cp && cp->connect(&peer) == kResultOk);
  CHECK(!peer.got.empty() && peer.got.back().seed == fresh.decor.seed);

  // v1 state: parameters restored, a seed generated once and then kept; the peer learns it.
  double p[kNumParams];
  loadDefaults(p);
  p[kMix] = 0.3;
  MemStream v1 = streamOf(v1Bytes(p));
  peer.got.clear();
  CHECK(comp->setState(&v1) == kResultOk);
  const PluginState a = readBack(comp);
  CHECK(a.params[kMix] == 0.3);
  CHECK(a.decor.seed != 0);
  CHECK(readBack(comp).decor.seed == a.decor.seed);
  CHECK(!peer.got.empty() && peer.got.back().seed == a.decor.seed);

  // v2 with seed 0: a new seed; v2 with a seed: that seed, angles and texts as stored.
  PluginState z;
  defaultState(z);
  z.decor.tapeTexts = {"hello"};
  std::vector<uint8_t> bytes;
  serializeState(z, bytes);
  MemStream zs = streamOf(bytes);
  CHECK(comp->setState(&zs) == kResultOk);
  const PluginState b = readBack(comp);
  CHECK(b.decor.seed != 0 && b.decor.tapeTexts.size() == 1 && b.decor.tapeTexts[0] == "hello");

  z.decor.seed = 1234;
  z.decor.screwAngles[2] = 90.f;
  serializeState(z, bytes);
  MemStream ks = streamOf(bytes);
  CHECK(comp->setState(&ks) == kResultOk);
  const PluginState c = readBack(comp);
  CHECK(c.decor.seed == 1234 && c.decor.screwAngles[2] == 90.f);

  // A corrupt state is refused and leaves the previous one in place.
  MemStream junk = streamOf({9, 0, 0, 0, 1, 2});
  CHECK(comp->setState(&junk) != kResultOk);
  CHECK(readBack(comp).decor.seed == 1234);

  // The decor the controller sends is adopted (sanitized) and saved with the state.
  Decor edit = c.decor;
  edit.tapeTexts = {"  new text that is too long  ", "b"};
  edit.screwAngles[0] = 400.f;
  IMessage* msg = newMessage();
  CHECK(msg);
  if (msg) {
    encodeDecorMessage(msg, edit);
    CHECK(cp->notify(msg) == kResultOk);
    msg->release();
  }
  const PluginState d = readBack(comp);
  CHECK(d.decor.seed == 1234 && d.decor.screwAngles[0] == 40.f);
  CHECK(d.decor.tapeTexts.size() == 2 && d.decor.tapeTexts[0] == "new text that");
  CHECK(d.params[kMix] == z.params[kMix]);   // parameters untouched by the decor

  // A decor message with seed 0 never clears the seed.
  edit.seed = 0;
  if ((msg = newMessage())) {
    encodeDecorMessage(msg, edit);
    cp->notify(msg);
    msg->release();
  }
  CHECK(readBack(comp).decor.seed == 1234);

  cp->disconnect(&peer);
  cp->release();
  comp->terminate();
  comp->release();
}

void test_controller_seed(Module& m) {
  auto* ctl = m.create<IEditController>(kControllerUID, IEditController::iid);
  CHECK(ctl && ctl->initialize(&g_host) == kResultOk);
  if (!ctl) return;
  Peer peer;
  IConnectionPoint* cp = connectionOf(ctl);
  CHECK(cp && cp->connect(&peer) == kResultOk);

  // A stored seed is adopted silently.
  PluginState s;
  defaultState(s);
  s.decor.seed = 777;
  std::vector<uint8_t> bytes;
  serializeState(s, bytes);
  MemStream st = streamOf(bytes);
  CHECK(ctl->setComponentState(&st) == kResultOk);
  CHECK(peer.got.empty());

  // A state without a seed keeps the seed the controller already has (no message).
  s.decor.seed = 0;
  serializeState(s, bytes);
  MemStream st0 = streamOf(bytes);
  CHECK(ctl->setComponentState(&st0) == kResultOk);
  CHECK(peer.got.empty());

  // A v1 state on a fresh controller: it generates a seed and sends it to the processor.
  auto* fresh = m.create<IEditController>(kControllerUID, IEditController::iid);
  CHECK(fresh && fresh->initialize(&g_host) == kResultOk);
  Peer peer2;
  IConnectionPoint* cp2 = connectionOf(fresh);
  CHECK(cp2 && cp2->connect(&peer2) == kResultOk);
  double p[kNumParams];
  loadDefaults(p);
  MemStream v1 = streamOf(v1Bytes(p));
  CHECK(fresh->setComponentState(&v1) == kResultOk);
  CHECK(peer2.got.size() == 1 && peer2.got[0].seed != 0);

  // The processor's answer (its own seed) wins; a later seedless state keeps it.
  Decor fromProcessor;
  fromProcessor.seed = 4242;
  IMessage* msg = newMessage();
  CHECK(msg);
  if (msg) {
    encodeDecorMessage(msg, fromProcessor);
    CHECK(cp2->notify(msg) == kResultOk);
    msg->release();
  }
  peer2.got.clear();
  MemStream v1b = streamOf(v1Bytes(p));
  CHECK(fresh->setComponentState(&v1b) == kResultOk);
  CHECK(peer2.got.empty());

  // Corrupt state: refused.
  MemStream junk = streamOf({2, 0, 0, 0, 1});
  CHECK(fresh->setComponentState(&junk) != kResultOk);

  cp2->disconnect(&peer2);
  cp2->release();
  fresh->terminate();
  fresh->release();
  cp->disconnect(&peer);
  cp->release();
  ctl->terminate();
  ctl->release();
}

} // namespace

int main() {
  struct stat sb{};
  if (stat(PILLOW_TALK_MODULE, &sb) != 0) {
    std::printf("module not built (%s): skipped\n", PILLOW_TALK_MODULE);
    return kSkip;
  }
  Module m;
  if (!m.open()) {
    std::fprintf(stderr, "cannot open the module\n");
    return 1;
  }
  test_processor_seed_and_state(m);
  test_controller_seed(m);
  m.close();
  std::printf("pillow_talk_state_module_test: %s\n", g_failed ? "FAILED" : "all passed");
  return g_failed ? 1 : 0;
}
