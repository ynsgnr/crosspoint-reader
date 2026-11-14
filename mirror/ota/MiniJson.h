#pragma once
// A small strict JSON reader for the OTA manifest: objects, arrays, strings, integers, booleans, null. Bounded depth,
// size and string length, so a hostile document cannot exhaust the heap. It reads only bytes that were authenticated
// first (see OtaManifest.h), and rejects what it does not understand instead of guessing.
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mirror::ota::json {

struct Value {
  enum class Type { Null, Bool, Int, String, Array, Object } type = Type::Null;
  bool b = false;
  int64_t i = 0;
  std::string s;
  std::vector<Value> items;                         // Array
  std::vector<std::pair<std::string, Value>> keys;  // Object, in document order

  // The member `key` of an object, or nullptr.
  const Value* get(const char* key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& kv : keys)
      if (kv.first == key) return &kv.second;
    return nullptr;
  }
};

constexpr size_t kMaxDocument = 16 * 1024;
constexpr int kMaxDepth = 6;
constexpr size_t kMaxString = 512;
constexpr size_t kMaxNodes = 600;

// True when `text` is exactly one JSON value (surrounding white space allowed). Numbers must be integers.
bool parse(const std::string& text, Value& out);

}  // namespace mirror::ota::json
