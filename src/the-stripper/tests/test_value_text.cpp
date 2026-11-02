/** Fairy Tapes · The Stripper — typed values: parsing "3", "+3.5 dB", "1.2k", "45 %" and the inverse of the display (no VSTGUI). */

#include "ui/value_text.h"
#include "params.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace FairyTapes::TheStripper::Ui;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)
bool near(double a, double b, double e = 1e-9) { return std::fabs(a - b) <= e; }

void test_typed_numbers() {
  CHECK(near(*parseTypedNumber("3"), 3) && near(*parseTypedNumber("+3.5 dB"), 3.5) && near(*parseTypedNumber("-6"), -6));
  CHECK(near(*parseTypedNumber("1.2k"), 1200) && near(*parseTypedNumber("1.2 kHz"), 1200) && near(*parseTypedNumber("2K"), 2000));
  CHECK(near(*parseTypedNumber("45 %"), 45) && near(*parseTypedNumber("45%"), 45));
  CHECK(near(*parseTypedNumber("440 Hz"), 440) && near(*parseTypedNumber("  7  "), 7) && near(*parseTypedNumber("0.25"), 0.25));
  CHECK(near(*parseTypedNumber("3,5"), 3.5));               // a decimal comma
  CHECK(near(*parseTypedNumber("-.5"), -0.5) && near(*parseTypedNumber("+.5"), 0.5));
  CHECK(!parseTypedNumber("") && !parseTypedNumber("abc") && !parseTypedNumber("+") && !parseTypedNumber("-") && !parseTypedNumber("k"));
  CHECK(!parseTypedNumber("1.2.3") && !parseTypedNumber("3 db db") && !parseTypedNumber("3 volts") && !parseTypedNumber("nan") && !parseTypedNumber("inf"));
  CHECK(!parseTypedNumber("1e999") && !parseTypedNumber(std::string(500, '9')));   // not finite / absurd: refused
  CHECK(near(*parseTypedNumber("1e3"), 1000));
}

void test_numbers_of_display_strings() {
  CHECK(near(*numberOfDisplay("+0.0 dB"), 0) && near(*numberOfDisplay("-20.0 dB"), -20) && near(*numberOfDisplay("-72 dB"), -72));
  CHECK(near(*numberOfDisplay("20000 Hz"), 20000) && near(*numberOfDisplay("2.5 kHz"), 2500) && near(*numberOfDisplay("100 Hz"), 100));
  CHECK(near(*numberOfDisplay("45%"), 45) && near(*numberOfDisplay("0.70"), 0.7) && near(*numberOfDisplay("5.0"), 5));
  CHECK(!numberOfDisplay("Bypass") && !numberOfDisplay(""));
  // the bottom of the gate threshold (2026-11-02): "-inf dB" is minus infinity, not "no number"
  CHECK(numberOfDisplay("-inf dB") && std::isinf(*numberOfDisplay("-inf dB")) && *numberOfDisplay("-inf dB") < 0);
  CHECK(!numberOfDisplay("info") && !numberOfDisplay("-information"));
}

// The real gate display: -inf at 0, 80 log10(v/4) dB above (about -48 dB at the top).
std::string gateDisplay(double v) { return FairyTapes::TheStripper::gateThresholdText(v); }

void test_gate_threshold_is_typed_back() {
  const double v60 = solveNormalized(gateDisplay, -60);
  CHECK(*numberOfDisplay(gateDisplay(v60)) == -60);                 // typed "-60": the knob lands where the text says -60 dB
  CHECK(near(v60, 4.0 * std::pow(10.0, -60.0 / 80.0), 0.02));     // as exact as the display (1 dB steps)
  CHECK(solveNormalized(gateDisplay, -48) == 1.0 || *numberOfDisplay(gateDisplay(solveNormalized(gateDisplay, -48))) == -48);
  CHECK(solveNormalized(gateDisplay, 0) == 1.0);                   // above the top (-48 dB): the top
  CHECK(solveNormalized(gateDisplay, -1000) < 1e-3);               // far below: practically the bottom
  CHECK(editFieldText(gateDisplay(0.0)) == "-inf");
  CHECK(editFieldText(gateDisplay(1.0)) == "-48");
}

// A display like the gain knobs: -20..+20 dB with one decimal.
std::string gainDisplay(double v) { char b[32]; std::snprintf(b, sizeof b, "%+.1f dB", (v - 0.5) * 40.0); return b; }
// A display like the frequency knobs: 200..2500 Hz, shown in kHz from 1000.
std::string freqDisplay(double v) { const double f = 200 + v * 2300; char b[32]; if (f >= 1000) std::snprintf(b, sizeof b, "%.1f kHz", f / 1000); else std::snprintf(b, sizeof b, "%.0f Hz", f); return b; }
std::string pctDisplay(double v) { char b[32]; std::snprintf(b, sizeof b, "%.0f%%", v * 100); return b; }
std::string stepDisplay(double v) { return v < 0.25 ? "0 dB" : v < 0.75 ? "5 dB" : "10 dB"; }

void test_inverse_of_the_display() {
  CHECK(near(solveNormalized(gainDisplay, 3.0), 0.575, 0.002));
  CHECK(near(solveNormalized(gainDisplay, 0.0), 0.5, 0.002));
  CHECK(solveNormalized(gainDisplay, 500) == 1.0 && solveNormalized(gainDisplay, -500) == 0.0);   // out of range: the end
  CHECK(near(solveNormalized(freqDisplay, 1200), (1200 - 200) / 2300.0, 0.03));                     // typed "1.2k": as exact as the display (0.1 kHz steps)
  CHECK(near(solveNormalized(freqDisplay, 440), (440 - 200) / 2300.0, 0.003));
  CHECK(near(solveNormalized(pctDisplay, 45), 0.45, 0.006));
  CHECK(near(solveNormalized(pctDisplay, 100), 1.0, 1e-9) && near(solveNormalized(pctDisplay, 0), 0.0, 1e-9));
  // a stepped display lands on a value that really shows the typed number
  const double v = solveNormalized(stepDisplay, 5);
  CHECK(*numberOfDisplay(stepDisplay(v)) == 5);
  // a decreasing display works too (the end with the larger number is 0)
  auto inverted = [](double v) { return pctDisplay(1.0 - v); };
  CHECK(near(solveNormalized(inverted, 30), 0.7, 0.006));
  // a display that is not a number: the answer is a safe value in range
  const double odd = solveNormalized([](double) { return std::string("n/a"); }, 3);
  CHECK(odd >= 0.0 && odd <= 1.0);
  CHECK(solveNormalized(gainDisplay, std::nan("")) >= 0.0);
}

void test_text_for_the_edit_field() {
  CHECK(editFieldText("+3.5 dB") == "3.5");
  CHECK(editFieldText("100 Hz") == "100");
  CHECK(editFieldText("2.5 kHz") == "2500");
  CHECK(editFieldText("45%") == "45");
  CHECK(editFieldText("0.70") == "0.7");
  CHECK(editFieldText("-72 dB") == "-72");
  CHECK(editFieldText("Bypass") == "Bypass");   // not a number: shown as it is
}
} // namespace

int main() {
  test_typed_numbers();
  test_numbers_of_display_strings();
  test_inverse_of_the_display();
  test_text_for_the_edit_field();
  test_gate_threshold_is_typed_back();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("test_value_text: ok");
  return 0;
}
