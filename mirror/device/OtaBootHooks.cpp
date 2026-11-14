#if defined(CROSSPOINT_MIRROR)
// Where the automatic fallback (device/OtaBoot.cpp) is called from, without touching src/main.cpp:
//  * initVariant() is the Arduino core's weak hook: it runs once at the end of initArduino(), after nvs_flash_init()
//  and
//    before setup(). The project defines no initVariant of its own.
//  * mirror/platformio_mirror.ini links with -Wl,--wrap=_Z4loopv ("loop()"), so the core's loopTask calls
//  __wrap__Z4loopv, which
//    ticks the guard and then runs the real loop(). The linker fails if the flag is missing, so this cannot go silent.
#include <Arduino.h>

#include "device/OtaBoot.h"

extern "C" void initVariant(void) { mirror::ota::bootGuardAtStart(); }

extern "C" void __real__Z4loopv();
extern "C" void __wrap__Z4loopv() {
  mirror::ota::bootGuardTick(static_cast<uint32_t>(millis()));
  __real__Z4loopv();
}
#endif
