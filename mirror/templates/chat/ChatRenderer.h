#pragma once
#include "core/TemplateRenderer.h"
#include "templates/chat/ChatLayout.h"
#include "templates/chat/chat_gen.h"

namespace mirror::chat {

class ChatRenderer final : public TemplateRenderer {
 public:
  static constexpr size_t kMaxMessages = 24;  // spec/templates/chat.yaml ChatState.messages max

  uint8_t templateId() const override { return gen::chat::kTemplateId; }
  uint8_t schemaVersion() const override { return gen::chat::kSchemaVersion; }
  bool onFull(const uint8_t* payload, size_t len) override;
  bool onDiff(const uint8_t* payload, size_t len) override;
  DirtyRects render(Gfx& gfx) override;
  bool onButton(Button b, std::vector<uint8_t>& eventPayload) override;

  const gen::chat::ChatState& state() const { return state_; }

  // Same rules as ChatApply.kt. False leaves `s` untouched.
  static bool applyOp(gen::chat::ChatState& s, const gen::chat::Op& op);

 private:
  gen::chat::ChatState state_;
  std::vector<Item> prev_;
  bool havePrev_ = false;
  int prevW_ = 0, prevH_ = 0;
};

// Golden-file format; same as dump() in the Kotlin tests.
std::string dumpState(const gen::chat::ChatState& s);

}  // namespace mirror::chat
