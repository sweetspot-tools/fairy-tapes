// Fairy Tapes · The Stripper — the TONE3000 client of one plug-in instance (see tone3000_service.h).
#include "tone3000_service.h"

#include "preferences.h"
#include "tone3000_config.h"

#ifdef FAIRY_HAS_TONE3000
#include "tone3000/client.h"
#include "tone3000/http.h"
#include "tone3000/oauth.h"
#endif

namespace FairyTapes::TheStripper {

#ifdef FAIRY_HAS_TONE3000

struct Tone3000Service::Impl {
  Impl()
  : flow(makeConfig(), http) {
    const std::filesystem::path dir = preferencesPath("TheStripper").parent_path();
    Tone3000::ClientPaths paths{dir / "tone3000.login", dir / "tone3000" / "models"};
    Tone3000::ClientBackends be;
    be.http = &http;
    be.signIn = [this](Tone3000::Prompt p, double timeout, const std::atomic<bool>* cancel) { return flow.run(p, timeout, cancel); };
    be.refresh = [this](const std::string& token, const std::atomic<bool>* cancel) { return flow.refresh(token, cancel); };
    tones = paths.tonesFolder;
    client = std::make_unique<Tone3000::Tone3000Client>(std::move(paths), std::move(be));
  }
  static Tone3000::OAuthConfig makeConfig() {
    Tone3000::OAuthConfig c;
    c.clientId = kTone3000ClientId;
    return c;
  }
  Tone3000::CurlHttpClient http;
  Tone3000::OAuthFlow flow;
  std::filesystem::path tones;
  std::unique_ptr<Tone3000::Tone3000Client> client;   // last: its thread stops before the rest goes away
};

std::unique_ptr<Tone3000Service> Tone3000Service::create() {
  if (std::string(kTone3000ClientId).empty()) return nullptr;   // the app is not registered yet
  std::unique_ptr<Tone3000Service> s(new Tone3000Service());
  s->impl_ = std::make_unique<Impl>();
  return s;
}

Tone3000Actions Tone3000Service::actions() {
  Tone3000Actions a;
  Tone3000::Tone3000Client* c = impl_->client.get();
  a.configured = true;
  a.snapshot = [c] { return c->snapshot(); };
  a.signIn = [c] { c->signIn(Tone3000::Prompt::Login); };
  a.chooseTone = [c] { c->signIn(Tone3000::Prompt::SelectTone); };
  a.signOut = [c] { c->signOut(); };
  a.cancel = [c] { c->cancel(); };
  a.showList = [c](Tone3000::ListKind k, int page) { c->showList(k, page); };
  a.showTone = [c](const std::string& id) { c->showTone(id); };
  a.download = [c](const std::string& id) { c->downloadModel(id); };
  a.openUrl = [](const std::string& url) { Tone3000::openInSystemBrowser(url); };   // refuses anything but tone3000.com
  const std::u8string tones = impl_->tones.u8string();
  a.tonesFolder.assign(reinterpret_cast<const char*>(tones.data()), tones.size());
  return a;
}

#else

struct Tone3000Service::Impl {};
std::unique_ptr<Tone3000Service> Tone3000Service::create() { return nullptr; }
Tone3000Actions Tone3000Service::actions() { return {}; }

#endif

Tone3000Service::Tone3000Service() = default;
Tone3000Service::~Tone3000Service() = default;

} // namespace FairyTapes::TheStripper
