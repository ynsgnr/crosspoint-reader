#pragma once
#include "core/TemplateRenderer.h"
#include "templates/chat/ChatLayout.h"
#include "templates/chat/chat_gen.h"

namespace mirror::chat {

class ChatRenderer final : public TemplateRenderer {
 public:
  // Limits from spec/templates/chat.yaml.
  static constexpr size_t kMaxMessages = 24;
  static constexpr size_t kMaxDraftBytes = 1024;

  uint8_t templateId() const override { return gen::chat::kTemplateId; }
  uint8_t schemaVersion() const override { return gen::chat::kSchemaVersion; }
  bool onFull(const uint8_t* payload, size_t len) override;
  bool onDiff(const uint8_t* payload, size_t len) override;
  DirtyRects render(Gfx& gfx) override;
  bool onButton(Button b, std::vector<uint8_t>& eventPayload) override;

  std::string dump() const override;
  const gen::chat::ChatState& state() const { return state_; }

  // Same rules as ChatApply.kt. False leaves `s` untouched.
  static bool applyOp(gen::chat::ChatState& s, const gen::chat::Op& op);

 private:
  gen::chat::ChatState state_;
  std::vector<Item> prev_;
  std::vector<uint64_t> prevKeys_;
  bool havePrev_ = false;
  int prevW_ = 0, prevH_ = 0;
};

}  // namespace mirror::chat
