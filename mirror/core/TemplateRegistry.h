#pragma once
// Templates register themselves with MIRROR_REGISTER_TEMPLATE; the common layer never lists them.
#include <memory>
#include <vector>

#include "core/TemplateRenderer.h"

namespace mirror {

struct TemplateEntry {
  uint8_t id;
  uint8_t minSchema;
  uint8_t maxSchema;
  std::unique_ptr<TemplateRenderer> (*create)();
};

class TemplateRegistry {
 public:
  static TemplateRegistry& instance() {
    static TemplateRegistry r;
    return r;
  }
  void add(const TemplateEntry& e) {
    if (!find(e.id)) entries_.push_back(e);  // a duplicate id keeps the first registration
  }
  const TemplateEntry* find(uint8_t id) const {
    for (const auto& e : entries_)
      if (e.id == id) return &e;
    return nullptr;
  }
  const std::vector<TemplateEntry>& entries() const { return entries_; }

 private:
  std::vector<TemplateEntry> entries_;
};

struct TemplateRegistrar {
  explicit TemplateRegistrar(const TemplateEntry& e) { TemplateRegistry::instance().add(e); }
};

}  // namespace mirror

#define MIRROR_REGISTER_TEMPLATE(ClassName, id, minSchema, maxSchema)            \
  static std::unique_ptr<::mirror::TemplateRenderer> mirrorCreate##ClassName() { \
    return std::make_unique<ClassName>();                                        \
  }                                                                              \
  static const ::mirror::TemplateRegistrar mirrorRegistrar##ClassName(           \
      ::mirror::TemplateEntry{id, minSchema, maxSchema, &mirrorCreate##ClassName});
