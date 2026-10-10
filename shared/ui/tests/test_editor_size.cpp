/** Fairy Tapes · shared UI — unit tests for the editor size math. */
#include "fairy_ui/editor_size.h"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace FairyTapes::Ui;

static int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

int main() {
  const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
  CHECK(sanitizeContentScale(1.0) == 1.0);
  CHECK(sanitizeContentScale(nan) == 1.0);
  CHECK(sanitizeContentScale(inf) == 1.0);
  CHECK(sanitizeContentScale(0.0) == 1.0);
  CHECK(sanitizeContentScale(-2.0) == 1.0);
  CHECK(sanitizeContentScale(100.0) == kMaxContentScale);
  const PixelSize a = scaledSize(1000, 628, 1.0);
  CHECK(a.width == 1000 && a.height == 628);
  const PixelSize b = scaledSize(1000, 628, 1.5);
  CHECK(b.width == 1500 && b.height == 942);
  const PixelSize c = scaledSize(1000, 628, nan);
  CHECK(c.width == 1000 && c.height == 628);
  if (g_failed) return 1;
  std::puts("test_editor_size: ok");
  return 0;
}
