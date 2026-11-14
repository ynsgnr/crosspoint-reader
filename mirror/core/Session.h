#pragma once
// Device-side common layer: reassembly, version checks, sequence tracking, ack/resync/error, dispatch to the
// active TemplateRenderer. Payloads are opaque here.
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "core/Envelope.h"
#include "core/Gfx.h"
#include "core/TemplateRegistry.h"

namespace mirror {

class Session {
 public:
  using SendFn = std::function<void(const std::vector<uint8_t>& frame)>;

  static constexpr uint16_t kDefaultMaxPayload = 2048;  // reported to the phone, enforced on input

  Session(const TemplateRegistry& registry, SendFn send, uint16_t maxPayload = kDefaultMaxPayload)
      : registry_(registry), send_(std::move(send)), maxPayload_(maxPayload), parser_(maxPayload) {}

  // Raw bytes from the phone, split anywhere. True when the renderer state changed.
  bool feed(const uint8_t* data, size_t len);

  // Use after dropping inbound bytes.
  void requestResync() { emitResync(static_cast<uint8_t>(gen::kControlTemplateId), 0); }

  // False when the template has no event for this button.
  bool onButton(Button b);

  // A tap at (x, y) in the active template's frame coordinates; false when the template has no touch target there.
  bool onTap(int x, int y);

  // Null until the phone sends a full state.
  TemplateRenderer* active() { return active_.get(); }
  uint16_t lastApplied() const { return lastApplied_; }
  // What the phone last reported (dark mode, lock state, the apps for the launcher), once it has sent it.
  const std::optional<gen::PhoneStatus>& phoneStatus() const { return phone_; }
  std::string phoneDump() const;  // golden-file form: "dark=1 minutes=30 locked=0 apps=Signal,WhatsApp" or "-"

  // Asks the phone to do something (gen::kCmd*). Only valid while connected.
  void sendCommand(uint8_t command, uint8_t arg = 0, uint8_t arg2 = 0);

  // The templates the device showed, oldest first (a chat opened from the list: list, chat). A template switch pushes;
  // a switch back to one already in the stack pops to it, like Android's back stack. Back is only offered while there
  // is something to go back to; the phone does the going back (gen::kCmdBack) and its screen decides what comes to the
  // front.
  bool canGoBack() const { return stack_.size() > 1; }
  void goBack() { sendCommand(gen::kCmdBack); }

  // One-shot flags; either one means the next frame should be a full refresh.
  bool takeTemplateSwitched() {
    bool s = switched_;
    switched_ = false;
    return s;
  }
  bool takeResyncHappened() {
    bool s = resync_;
    resync_ = false;
    return s;
  }

  std::vector<uint8_t> capabilities(const Gfx& gfx, const char* deviceName, uint8_t extraFeatures = 0) const;

 private:
  void handle(const Envelope& e, bool& changed);
  void handleControl(const Envelope& e, bool& changed);
  void emit(uint8_t templateId, uint8_t schema, uint8_t flags, uint16_t base, const std::vector<uint8_t>& payload = {});
  void emitError(uint8_t code, uint8_t detail);
  void emitResync(uint8_t templateId, uint8_t schema);

  const TemplateRegistry& registry_;
  SendFn send_;
  uint16_t maxPayload_;
  EnvelopeStreamParser parser_;
  void pushView(uint8_t templateId);

  static constexpr size_t kMaxViewStack = 4;
  std::unique_ptr<TemplateRenderer> active_;
  std::vector<uint8_t> stack_;
  uint16_t lastApplied_ = 0;
  uint16_t outSeq_ = 1;
  uint32_t crcSeen_ = 0;
  std::optional<gen::PhoneStatus> phone_;
  bool awaitingFull_ = false;  // resync requested: diffs are dropped until a full state arrives
  bool switched_ = false;
  bool resync_ = false;
};

}  // namespace mirror
