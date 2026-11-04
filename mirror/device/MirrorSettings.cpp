#if defined(CROSSPOINT_MIRROR)
#include "device/MirrorSettings.h"

#include <nvs.h>

namespace mirror::settings {
namespace {

struct Nvs {
  nvs_handle_t h = 0;
  bool ok;
  explicit Nvs(nvs_open_mode_t mode) : ok(nvs_open("mirror", mode, &h) == ESP_OK) {}
  ~Nvs() {
    if (ok) nvs_close(h);
  }
};

}  // namespace

bool keyboardEnabled() {
  Nvs n(NVS_READONLY);
  uint8_t v = 0;
  return n.ok && nvs_get_u8(n.h, "kbd", &v) == ESP_OK && v == 1;
}

void setKeyboardEnabled(bool on) {
  Nvs n(NVS_READWRITE);
  if (n.ok) nvs_set_u8(n.h, "kbd", on ? 1 : 0), nvs_commit(n.h);
}

Chord chord() {
  Nvs n(NVS_READONLY);
  Chord c = kDefaultChord;
  uint8_t mods = 0, key = 0;
  if (n.ok && nvs_get_u8(n.h, "chord_mod", &mods) == ESP_OK && nvs_get_u8(n.h, "chord_key", &key) == ESP_OK) {
    const Chord stored{mods, key};
    if (checkChord(stored) == ChordCheck::Ok) c = stored;
  }
  return c;
}

void setChord(const Chord& c) {
  Nvs n(NVS_READWRITE);
  if (!n.ok) return;
  nvs_set_u8(n.h, "chord_mod", c.mods);
  nvs_set_u8(n.h, "chord_key", c.key);
  nvs_commit(n.h);
}

}  // namespace mirror::settings
#endif
