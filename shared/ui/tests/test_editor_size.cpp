/** Fairy Tapes · shared UI — unit tests for the editor size math. */
#include "fairy_ui/editor_size.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

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

  // SIZE: the editor's own choice, 75 / 90 / 100 / 125 %, on top of the host's factor
  CHECK(sizeFactor(EditorSize::S75) == 0.75 && sizeFactor(EditorSize::S90) == 0.9);
  CHECK(sizeFactor(EditorSize::S100) == 1.0 && sizeFactor(EditorSize::S125) == 1.25);

  CHECK(std::string(sizeLabel(EditorSize::S75)) == "75 %" && std::string(sizeLabel(EditorSize::S125)) == "125 %");
  for (EditorSize e : {EditorSize::S75, EditorSize::S90, EditorSize::S100, EditorSize::S125}) {
    EditorSize back = EditorSize::S100;
    CHECK(parseSizeLabel(sizeLabel(e), back) && back == e);                       // the label parses back
    back = EditorSize::S75;
    CHECK(parseSizeLabel(sizeKey(e), back) && back == e);                         // so does the key of a prefs file
  }
  EditorSize keep = EditorSize::S90;
  CHECK(!parseSizeLabel("33", keep) && keep == EditorSize::S90);                  // unknown: false, output untouched
  CHECK(!parseSizeLabel("", keep) && !parseSizeLabel("100x", keep) && !parseSizeLabel("075", keep) && keep == EditorSize::S90);

  const PixelSize s90 = scaledSize(1280, 890, 1.5, EditorSize::S90);
  CHECK(s90.width == 1728 && s90.height == 1202);
  const PixelSize s75 = scaledSize(1280, 890, 1.0, EditorSize::S75);
  CHECK(s75.width == 960 && s75.height == 668);                                   // 667.5 rounds up
  const PixelSize sn = scaledSize(1280, 890, nan, EditorSize::S125);
  CHECK(sn.width == 1600 && sn.height == 1113);                                   // NaN host: as 1 (1112.5 rounds up)
  const PixelSize old = scaledSize(1280, 890, 1.5), same = scaledSize(1280, 890, 1.5, EditorSize::S100);
  CHECK(old.width == same.width && old.height == same.height && old.width == 1920 && old.height == 1335);
  CHECK(frameZoom(1.5, EditorSize::S90) == sanitizeContentScale(1.5) * 0.9);
  CHECK(frameZoom(nan, EditorSize::S100) == 1.0 && frameZoom(0.0, EditorSize::S125) == 1.25);
  CHECK(frameZoom(100.0, EditorSize::S100) == kMaxContentScale);

  if (g_failed) return 1;
  std::puts("test_editor_size: ok");
  return 0;
}
