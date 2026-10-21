/**
 * Fairy Tapes · shared TONE3000 client — the receiver of the OAuth redirect: a tiny HTTP server that listens on 127.0.0.1
 * only, on a port the system picks, for as long as a login takes. It answers GET /callback once (with a page that says the
 * tab can be closed) and hands the request target to the caller; anything else gets a 404 or 400 and the wait goes on.
 * Requests are size- and time-limited, the Host header must be exactly 127.0.0.1:<port> (a page on the web cannot steer a
 * browser to it under another name), and no file is ever served.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace FairyTapes::Tone3000 {

class LoopbackServer {
public:
  LoopbackServer();                       // starts listening right away; check listening()
  ~LoopbackServer();
  LoopbackServer(const LoopbackServer&) = delete;
  LoopbackServer& operator=(const LoopbackServer&) = delete;

  bool listening() const { return fd_ >= 0; }
  int port() const { return port_; }

  // Waits for a callback request that `accept` approves (it sees the target, "/callback?code=...&state=..."; others get a
  // 400 and the wait goes on, so a stray or hostile request cannot end the login). Returns the accepted target or an empty
  // string on timeout, cancellation or failure. The server stops listening afterwards.
  std::string waitForCallback(double timeoutSeconds, const std::atomic<bool>* cancel,
                              const std::function<bool(const std::string& target)>& accept = {});
  void close();

private:
  std::intptr_t fd_ = -1;   // a socket (int on POSIX, SOCKET on Windows)
  int port_ = 0;
};

// The part of a request that decides what to do (pure, tested).
struct LoopbackRequest {
  std::string method, target, host;
  bool complete = false;      // the end of the headers was seen
  bool malformed = false;
};
LoopbackRequest parseLoopbackRequest(const std::string& raw);

} // namespace FairyTapes::Tone3000
