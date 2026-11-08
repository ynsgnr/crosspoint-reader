// Host test for the macro table. No framework: a failed check prints and makes the exit code 1.
#include <cstdio>

#include "Macro.h"

using namespace macro;

static int g_fail = 0;
#define CHECK(c)                                               \
  do {                                                         \
    if (!(c)) {                                                \
      ++g_fail;                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
    }                                                          \
  } while (0)

int main() {
  CHECK(kMacroCount > 0 && kMacroCount <= kMacroCols * 3);  // fits the 3x3 grid
  CHECK(kMacroRows * kMacroCols >= kMacroCount);
  for (const Macro& m : kMacros) {
    CHECK(m.label && m.label[0] != '\0');
    CHECK((m.consumer != 0) != (m.usage != 0));  // exactly one of media key or keyboard key
    CHECK(m.consumer == 0 || m.mods == 0);       // media keys carry no modifiers
    CHECK(m.consumer <= 0x03FF);                 // the HID report map's logical maximum
  }
  std::printf(g_fail ? "%d check(s) failed\n" : "macro table ok\n", g_fail);
  return g_fail ? 1 : 0;
}
