/**
 * Fairy Tapes · Deep Throat — state v2 and the "Decor" messages, through the real module.
 * dlopens the built bundle, creates the processor and the controller from the factory with a minimal host (SDK
 * hostclasses: IHostApplication + IMessage) and a fake peer that records the decor messages:
 *   - setState with a v2 blob then getState gives the same blob; a fresh processor, a v1 state and a v2 state with
 *     seed 0 all end with a non-zero seed, kept by getState; a corrupt state leaves the previous one in place;
 *   - the processor tells its peer the decor after setState and on connect, and adopts the decor the controller sends;
 *   - the controller receives the decor through notify, generates a seed only when it has none and sends it to its peer.
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

#ifndef DEEP_THROAT_MODULE
#error DEEP_THROAT_MODULE must point to the built module binary
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace FairyTapes::DeepThroat;

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
    h = dlopen(DEEP_THROAT_MODULE, RTLD_NOW | RTLD_LOCAL);
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

std::vector<uint8_t> stateBytes(const double* p, const Decor& d) {
  std::vector<uint8_t> out;
  serializeState(p, d, out);
  return out;
}

std::vector<uint8_t> savedBytes(IComponent* comp) {
  MemStream s;
  CHECK(comp->getState(&s) == kResultOk);
  return s.data;
}

MemStream streamOf(const std::vector<uint8_t>& bytes) {
  MemStream s;
  s.data = bytes;
  return s;
}

Decor decorOf(const std::vector<uint8_t>& bytes) {
  double v[kNumParams];
  Decor d;
  CHECK(deserializeState(bytes.data(), bytes.size(), v, d));
  CHECK((int32_t)detail::getLE(bytes.data(), 4) == kStateVersion);
  return d;
}

IConnectionPoint* connectionOf(FUnknown* obj) {
  IConnectionPoint* cp = nullptr;
  obj->queryInterface(IConnectionPoint::iid, (void**)&cp);
  return cp;
}

void test_processor_state(Module& m) {
  auto* comp = m.create<IComponent>(kProcessorUID, IComponent::iid);
  CHECK(comp && comp->initialize(&g_host) == kResultOk);
  if (!comp) return;

  // Fresh instance: a seed exists before any setState and stays the same.
  const Decor fresh = decorOf(savedBytes(comp));
  CHECK(fresh.seed != 0);
  CHECK(decorOf(savedBytes(comp)).seed == fresh.seed);

  // The processor tells its peer on connect.
  Peer peer;
  IConnectionPoint* cp = connectionOf(comp);
  CHECK(cp && cp->connect(&peer) == kResultOk);
  CHECK(!peer.got.empty() && peer.got.back().seed == fresh.seed);

  // A v2 blob in, the same blob out; the peer is told the decor.
  double p[kNumParams];
  loadDefaults(p);
  p[kTone] = 0.3;
  Decor d;
  d.seed = 1234;
  d.screwAngles[0] = 10.f;
  d.screwAngles[2] = 90.f;
  d.tapeTexts = {"VOX", "LEAD"};
  const std::vector<uint8_t> blob = stateBytes(p, d);
  MemStream in = streamOf(blob);
  peer.got.clear();
  CHECK(comp->setState(&in) == kResultOk);
  CHECK(savedBytes(comp) == blob);
  CHECK(peer.got.size() == 1 && peer.got[0].seed == 1234 && peer.got[0].tapeTexts == d.tapeTexts &&
        peer.got[0].screwAngles[2] == 90.f);

  // v1 state: parameters restored, a seed generated once and then kept; the peer learns it.
  p[kTone] = 0.7;
  MemStream v1 = streamOf(v1Bytes(p));
  peer.got.clear();
  CHECK(comp->setState(&v1) == kResultOk);
  const std::vector<uint8_t> after1 = savedBytes(comp);
  const Decor a = decorOf(after1);
  CHECK(a.seed != 0 && a.seed != 1234 && a.tapeTexts.empty());
  CHECK(savedBytes(comp) == after1);
  CHECK(!peer.got.empty() && peer.got.back().seed == a.seed);
  double back[kNumParams];
  Decor dummy;
  CHECK(deserializeState(after1.data(), after1.size(), back, dummy) && back[kTone] == 0.7);

  // v2 with seed 0: a new seed.
  Decor z;
  z.tapeTexts = {"hello"};
  MemStream zs = streamOf(stateBytes(p, z));
  CHECK(comp->setState(&zs) == kResultOk);
  const Decor b = decorOf(savedBytes(comp));
  CHECK(b.seed != 0 && b.tapeTexts.size() == 1 && b.tapeTexts[0] == "hello");

  // A corrupt state is refused and leaves the previous one in place.
  MemStream in2 = streamOf(blob);
  CHECK(comp->setState(&in2) == kResultOk);
  MemStream junk = streamOf({9, 0, 0, 0, 1, 2});
  CHECK(comp->setState(&junk) != kResultOk);
  std::vector<uint8_t> cut = blob;
  cut.resize(cut.size() - 1);
  MemStream cutS = streamOf(cut);
  CHECK(comp->setState(&cutS) != kResultOk);
  CHECK(savedBytes(comp) == blob);

  // The decor the controller sends is adopted (sanitized) and saved with the state, parameters untouched.
  Decor edit = d;
  edit.tapeTexts = {"  new text that is too long  ", "b"};
  edit.screwAngles[0] = 400.f;
  IMessage* msg = newMessage();
  CHECK(msg);
  if (msg) {
    encodeDecorMessage(msg, edit);
    CHECK(cp->notify(msg) == kResultOk);
    msg->release();
  }
  const std::vector<uint8_t> edited = savedBytes(comp);
  const Decor e = decorOf(edited);
  CHECK(e.seed == 1234 && e.screwAngles[0] == 40.f);
  CHECK(e.tapeTexts.size() == 2 && e.tapeTexts[0] == "new text that" && e.tapeTexts[1] == "b");
  double ev[kNumParams];
  Decor ed;
  CHECK(deserializeState(edited.data(), edited.size(), ev, ed) && ev[kTone] == 0.3);

  // A decor message with seed 0 never clears the seed.
  edit.seed = 0;
  if ((msg = newMessage())) {
    encodeDecorMessage(msg, edit);
    cp->notify(msg);
    msg->release();
  }
  CHECK(decorOf(savedBytes(comp)).seed == 1234);

  cp->disconnect(&peer);
  cp->release();
  comp->terminate();
  comp->release();
}

void test_controller_decor(Module& m) {
  auto* ctl = m.create<IEditController>(kControllerUID, IEditController::iid);
  CHECK(ctl && ctl->initialize(&g_host) == kResultOk);
  if (!ctl) return;
  Peer peer;
  IConnectionPoint* cp = connectionOf(ctl);
  CHECK(cp && cp->connect(&peer) == kResultOk);

  // The decor arrives through notify (it is not sent back) and the parameters stay as they are.
  Decor fromProcessor;
  fromProcessor.seed = 4242;
  fromProcessor.screwAngles[1] = 33.f;
  fromProcessor.tapeTexts = {"IN", "OUT"};
  IMessage* msg = newMessage();
  CHECK(msg);
  if (msg) {
    encodeDecorMessage(msg, fromProcessor);
    CHECK(cp->notify(msg) == kResultOk);
    msg->release();
  }
  CHECK(peer.got.empty());

  // The seed it received counts: a v1 state (no seed) on this controller must not make it generate and send one. A
  // controller that ignored the notify would still have seed 0 and would message its peer. (The angles and texts the
  // controller adopts are not observable through the VST3 interfaces; the editor harness covers them later.)
  double p0[kNumParams];
  loadDefaults(p0);
  MemStream v1n = streamOf(v1Bytes(p0));
  CHECK(ctl->setComponentState(&v1n) == kResultOk);
  CHECK(peer.got.empty());

  // A stored seed is adopted silently; a seedless state keeps the one known.
  double p[kNumParams];
  loadDefaults(p);
  Decor s;
  s.seed = 777;
  MemStream st = streamOf(stateBytes(p, s));
  CHECK(ctl->setComponentState(&st) == kResultOk);
  CHECK(peer.got.empty());
  s.seed = 0;
  MemStream st0 = streamOf(stateBytes(p, s));
  CHECK(ctl->setComponentState(&st0) == kResultOk);
  CHECK(peer.got.empty());

  // A v1 state on a fresh controller: it generates a seed and sends it to the processor.
  auto* fresh = m.create<IEditController>(kControllerUID, IEditController::iid);
  CHECK(fresh && fresh->initialize(&g_host) == kResultOk);
  Peer peer2;
  IConnectionPoint* cp2 = connectionOf(fresh);
  CHECK(cp2 && cp2->connect(&peer2) == kResultOk);
  MemStream v1 = streamOf(v1Bytes(p));
  CHECK(fresh->setComponentState(&v1) == kResultOk);
  CHECK(peer2.got.size() == 1 && peer2.got[0].seed != 0);

  // The processor's answer (its own seed) wins; a later seedless state keeps it.
  if ((msg = newMessage())) {
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
  if (stat(DEEP_THROAT_MODULE, &sb) != 0) {
    std::printf("module not built (%s): skipped\n", DEEP_THROAT_MODULE);
    return kSkip;
  }
  Module m;
  if (!m.open()) {
    std::fprintf(stderr, "cannot open the module\n");
    return 1;
  }
  test_processor_state(m);
  test_controller_decor(m);
  m.close();
  std::printf("deep_throat_state_module_test: %s\n", g_failed ? "FAILED" : "all passed");
  return g_failed ? 1 : 0;
}
