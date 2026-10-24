#include "devices.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace FairyTapes::Hotline {

const DeviceSpec kDevices[kNumDevices] = {
  //            button      name               hp     lp     drive coneHz coneDb coneQ horn  crushHz  bits ratio squelch noise room
  {"MEGA",     "Megaphone",     {400.0, 4500.0, 0.5,  1800.0, 6.0, 2.0, 4.0, 192000.0, 24.0, 3.0, -120.0, -60.0, 0.05}},
  {"PHONE",    "Telephone",     {300.0, 3400.0, 0.25, 1200.0, 3.0, 1.0, 0.0, 8000.0,   10.0, 2.0, -120.0, -54.0, 0.0}},
  {"WALKIE",   "Walkie-talkie", {400.0, 2800.0, 0.7,  1500.0, 4.0, 1.0, 0.0, 11025.0,  8.0,  6.0, -50.0,  -40.0, 0.0}},
  {"AM RADIO", "AM radio",      {150.0, 5000.0, 0.3,  900.0,  2.0, 0.8, 0.0, 192000.0, 24.0, 2.0, -120.0, -48.0, 0.0}},
  {"PA",       "Station PA",    {250.0, 6000.0, 0.2,  2500.0, 4.0, 1.5, 0.0, 192000.0, 24.0, 2.0, -120.0, -66.0, 0.6}},
  {"CHEAP",    "Cheap speaker", {180.0, 7000.0, 0.4,  1100.0, 5.0, 3.0, 0.0, 192000.0, 24.0, 1.0, -120.0, -70.0, 0.0}},
};

BlockParams neutralParams() {
  return {20.0, 20000.0, 0.0, 1000.0, 0.0, 1.0, 0.0, 192000.0, 24.0, 1.0, -120.0, -120.0, 0.0};
}

namespace {

// a*(1-t) + b*t: exact at both ends.
double lin(double a, double b, double t) { return a * (1.0 - t) + b * t; }
double logInterp(double a, double b, double t) {
  if (t <= 0.0) return a;
  if (t >= 1.0) return b;
  return std::exp(lin(std::log(a), std::log(b), t));
}

std::string freq(double hz) {
  char buf[32];
  if (hz >= 1000.0) std::snprintf(buf, sizeof buf, "%.1f kHz", hz / 1000.0);
  else std::snprintf(buf, sizeof buf, "%.0f Hz", hz);
  return buf;
}

std::string fmt(const char* f, double a, double b = 0.0) {
  char buf[64];
  std::snprintf(buf, sizeof buf, f, a, b);
  return buf;
}

} // namespace

BlockParams morphParams(const BlockParams& d, double amount) {
  const BlockParams n = neutralParams();
  const double t = std::isfinite(amount) ? std::clamp(amount, 0.0, 1.0) : 0.0;
  BlockParams r;
  r.hpHz = logInterp(n.hpHz, d.hpHz, t);
  r.lpHz = logInterp(n.lpHz, d.lpHz, t);
  r.drive = lin(n.drive, d.drive, t);
  r.coneHz = logInterp(n.coneHz, d.coneHz, t);
  r.coneGainDb = lin(n.coneGainDb, d.coneGainDb, t);
  r.coneQ = lin(n.coneQ, d.coneQ, t);
  r.hornGainDb = lin(n.hornGainDb, d.hornGainDb, t);
  r.crushRateHz = logInterp(n.crushRateHz, d.crushRateHz, t);
  r.crushBits = lin(n.crushBits, d.crushBits, t);
  r.compRatio = lin(n.compRatio, d.compRatio, t);
  r.squelchDb = lin(n.squelchDb, d.squelchDb, t);
  r.noiseDb = lin(n.noiseDb, d.noiseDb, t);
  r.room = lin(n.room, d.room, t);
  return r;
}

std::string describeDevice(int index) {
  if (index < 0 || index >= kNumDevices) return {};
  const BlockParams& p = kDevices[index].p;
  const BlockParams n = neutralParams();
  std::string out;
  auto line = [&out](const std::string& s) {
    if (!out.empty()) out += '\n';
    out += s;
  };
  if (p.hpHz != n.hpHz) line("HIGH PASS " + freq(p.hpHz));
  if (p.lpHz != n.lpHz) line("LOW PASS " + freq(p.lpHz));
  if (p.drive != n.drive) line("DRIVE " + fmt("%.0f %%", p.drive * 100.0));
  if (p.coneGainDb != n.coneGainDb) line("CONE " + freq(p.coneHz) + fmt(" %+.0f dB", p.coneGainDb));
  if (p.hornGainDb != n.hornGainDb) line(fmt("HORN %+.0f dB", p.hornGainDb));
  if (p.crushRateHz != n.crushRateHz || p.crushBits != n.crushBits)
    line("CRUSH " + freq(p.crushRateHz) + fmt(" %.0f bit", p.crushBits));
  if (p.compRatio != n.compRatio || p.squelchDb != n.squelchDb) {
    std::string s = fmt("COMPANDER %.0f:1", p.compRatio);
    if (p.squelchDb > n.squelchDb) s += fmt(" squelch %.0f dB", p.squelchDb);
    line(s);
  }
  if (p.noiseDb != n.noiseDb) line(fmt("NOISE %.0f dB", p.noiseDb));
  if (p.room != n.room) line(fmt("ROOM %.0f %%", p.room * 100.0));
  return out;
}

} // namespace FairyTapes::Hotline
