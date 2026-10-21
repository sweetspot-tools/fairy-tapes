/**
 * Fairy Tapes · shared TONE3000 client — what the interface talks to. One worker thread does all the network and disk
 * work (login, tones, models, lists, downloads); the editor never blocks: it sends a command and reads snapshot() on its
 * timer, comparing `version` to know when something changed. Nothing here touches the audio thread or VSTGUI.
 *
 * The access token is renewed with the refresh token when it is about to expire or a call answers 401; if that fails the
 * client is signed out with a message. A model is downloaded only when asked, one at a time, into
 * `<tonesFolder>/<tone>-<title>/<model>.nam` with a `.t3k.json` file beside it (author, licence, page); a model already
 * there is not downloaded again, so it loads offline.
 */

#pragma once

#include "tone3000/api.h"
#include "tone3000/metadata.h"
#include "tone3000/oauth.h"
#include "tone3000/token_store.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace FairyTapes::Tone3000 {

constexpr std::size_t kMaxModelBytes = 64u * 1024u * 1024u;

struct ClientPaths {
  std::filesystem::path loginFile;     // where the login is kept between sessions
  std::filesystem::path tonesFolder;   // where downloaded models go
};

struct ClientBackends {
  HttpClient* http = nullptr;                                                                  // not owned
  std::function<OAuthOutcome(Prompt, double, const std::atomic<bool>*)> signIn;                // the browser login
  std::function<OAuthOutcome(const std::string&, const std::atomic<bool>*)> refresh;           // a new access token
  std::function<double()> nowUnix;                                                             // seconds since 1970
  std::string baseUrl = kApiBase;
  std::string pageUrlPrefix = "https://www.tone3000.com/";                                     // a tone's page must start with this
};

struct ClientState {
  enum class Phase { SignedOut, SigningIn, SignedIn };
  enum class DownloadPhase { None, Running, Done, Failed };

  Phase phase = Phase::SignedOut;
  std::string userName;
  std::string message;                  // the last thing the user should know (an error), empty when all is well
  bool busy = false;                    // a command is running
  std::string busyText;                 // "Signing in...", "Loading the models...", ...

  std::optional<Tone> tone;             // the tone being looked at
  std::vector<Model> models;            // its models
  ListKind listKind = ListKind::Favorited;
  std::vector<Tone> list;
  int listPage = 1, listTotalPages = 1;
  bool listLoaded = false;

  DownloadPhase download = DownloadPhase::None;
  std::string downloadModelId;
  std::uint64_t downloadBytes = 0;
  std::string downloadPath;             // UTF-8 path of the model file once Done
  std::string downloadMessage;

  unsigned version = 0;                 // grows on every change
};

class Tone3000Client {
public:
  Tone3000Client(ClientPaths paths, ClientBackends backends);
  ~Tone3000Client();
  Tone3000Client(const Tone3000Client&) = delete;
  Tone3000Client& operator=(const Tone3000Client&) = delete;

  ClientState snapshot() const;

  void signIn(Prompt prompt);                       // SelectTone: the tone the user picks is opened afterwards
  void signOut();                                   // forgets the login and deletes the file
  void showTone(const std::string& toneId);         // its details and models
  void showList(ListKind kind, int page = 1);
  void downloadModel(const std::string& modelId);   // one of the models of the tone being shown
  void cancel();                                    // stops the running command and drops the queued ones

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// A model file looks like a NAM file: JSON object, of a plausible size. (The real check is loading it.)
bool looksLikeNamFile(const std::filesystem::path& file);

} // namespace FairyTapes::Tone3000
