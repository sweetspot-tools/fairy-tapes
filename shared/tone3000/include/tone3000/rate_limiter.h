/**
 * Fairy Tapes · shared TONE3000 client — a sliding-window request limiter (the API allows 100 requests a minute by default;
 * the client keeps to 60). The clock is injectable (seconds, monotonic) so tests do not sleep. Not thread safe: the Worker
 * owns it.
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>

namespace FairyTapes::Tone3000 {

class RateLimiter {
public:
  using Clock = std::function<double()>;   // seconds from any fixed point, never going back

  RateLimiter(std::size_t maxPerWindow = 60, double windowSeconds = 60.0, Clock clock = steadyClock())
  : max_(std::max<std::size_t>(1, maxPerWindow)), window_(windowSeconds > 0 ? windowSeconds : 60.0), clock_(std::move(clock)) {}

  // True if a request may go out now (and counts it); false if the window is full.
  bool tryAcquire() {
    const double now = clock_();
    evict(now);
    if (stamps_.size() >= max_) return false;
    stamps_.push_back(now);
    return true;
  }
  // Seconds until a request would be allowed (0 when one is allowed now).
  double secondsUntilAvailable() {
    const double now = clock_();
    evict(now);
    if (stamps_.size() < max_) return 0.0;
    return std::max(0.0, stamps_.front() + window_ - now);
  }

  static Clock steadyClock() {
    return [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
  }

private:
  void evict(double now) {
    while (!stamps_.empty() && now - stamps_.front() >= window_) stamps_.pop_front();
  }
  std::size_t max_;
  double window_;
  Clock clock_;
  std::deque<double> stamps_;
};

} // namespace FairyTapes::Tone3000
