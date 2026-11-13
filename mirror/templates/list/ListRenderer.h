#pragma once
#include "core/TemplateRenderer.h"
#include "templates/list/ListLayout.h"
#include "templates/list/list_gen.h"

namespace mirror::list {

class ListRenderer final : public TemplateRenderer {
 public:
  // Limits from spec/templates/list.yaml.
  static constexpr size_t kMaxItems = 32;

  uint8_t templateId() const override { return gen::list::kTemplateId; }
  uint8_t schemaVersion() const override { return gen::list::kSchemaVersion; }
  bool onFull(const uint8_t* payload, size_t len) override;
  bool onDiff(const uint8_t* payload, size_t len) override;
  DirtyRects render(Gfx& gfx) override;
  bool onButton(Button b, std::vector<uint8_t>& eventPayload) override;
  bool onTap(int x, int y, std::vector<uint8_t>& eventPayload) override;

  std::string dump() const override;
  const gen::list::ListState& state() const { return state_; }

  // Same rules as ListApply in the Android app. False leaves `s` untouched.
  static bool applyOp(gen::list::ListState& s, const gen::list::Op& op);

 private:
  size_t selectedIndex() const;  // items.size() when none

  gen::list::ListState state_;
  size_t first_ = 0;
  std::vector<RowRect> rows_;  // the rows of the last frame, for taps
  std::vector<Item> prev_;
  std::vector<uint64_t> prevKeys_;
  bool havePrev_ = false;
  int prevW_ = 0, prevH_ = 0;
};

}  // namespace mirror::list
