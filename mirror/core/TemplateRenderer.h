#pragma once
// Template plugin interface (device side): one class per template.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Gfx.h"

namespace mirror {

// Top and Bottom are a long press: jump to the oldest or newest content.
enum class Button { Up, Down, Left, Right, Confirm, Top, Bottom };

struct DirtyRects {
  std::vector<Rect> rects;
  bool full = false;  // everything changed (first frame, state replaced)
  bool empty() const { return !full && rects.empty(); }
};

// Zero = policy default.
struct RefreshOverrides {
  uint16_t fullRefreshEveryN = 0;
};

class TemplateRenderer {
 public:
  virtual ~TemplateRenderer() = default;
  virtual uint8_t templateId() const = 0;
  virtual uint8_t schemaVersion() const = 0;

  // False = invalid payload; the session asks for a resync.
  virtual bool onFull(const uint8_t* payload, size_t len) = 0;
  // False = invalid for the current state (which stays unchanged); the session asks for a resync.
  virtual bool onDiff(const uint8_t* payload, size_t len) = 0;

  // Draws the whole frame; returns what changed since the previous call.
  virtual DirtyRects render(Gfx& gfx) = 0;

  // True when the phone should hear about this button.
  virtual bool onButton(Button b, std::vector<uint8_t>& eventPayload) = 0;

  // Canonical one-line state description; golden transcripts compare it with the Kotlin codec's dump().
  virtual std::string dump() const { return {}; }

  virtual RefreshOverrides refreshOverrides() const { return {}; }
};

}  // namespace mirror
