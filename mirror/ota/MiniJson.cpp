#include "ota/MiniJson.h"

#include <cstring>

namespace mirror::ota::json {
namespace {

class Parser {
 public:
  explicit Parser(const std::string& t) : t_(t) {}

  bool run(Value& out) {
    if (t_.size() > kMaxDocument) return false;
    ws();
    if (!value(out, 0)) return false;
    ws();
    return pos_ == t_.size();
  }

 private:
  const std::string& t_;
  size_t pos_ = 0;
  size_t nodes_ = 0;

  void ws() {
    while (pos_ < t_.size() && (t_[pos_] == ' ' || t_[pos_] == '\t' || t_[pos_] == '\n' || t_[pos_] == '\r')) ++pos_;
  }
  bool more() const { return pos_ < t_.size(); }
  bool eat(char c) {
    if (more() && t_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }
  bool word(const char* w) {
    const size_t n = std::strlen(w);
    if (t_.compare(pos_, n, w) != 0) return false;
    pos_ += n;
    return true;
  }

  bool value(Value& v, int depth) {
    if (depth > kMaxDepth || ++nodes_ > kMaxNodes || !more()) return false;
    const char c = t_[pos_];
    if (c == '{') return object(v, depth);
    if (c == '[') return array(v, depth);
    if (c == '"') {
      v.type = Value::Type::String;
      return string(v.s);
    }
    if (c == '-' || (c >= '0' && c <= '9')) return number(v);
    if (word("true")) {
      v.type = Value::Type::Bool;
      v.b = true;
      return true;
    }
    if (word("false")) {
      v.type = Value::Type::Bool;
      return true;
    }
    if (word("null")) {
      v.type = Value::Type::Null;
      return true;
    }
    return false;
  }

  bool object(Value& v, int depth) {
    v.type = Value::Type::Object;
    ++pos_;
    ws();
    if (eat('}')) return true;
    for (;;) {
      ws();
      std::string key;
      if (!more() || t_[pos_] != '"' || !string(key)) return false;
      for (const auto& kv : v.keys)
        if (kv.first == key) return false;  // a duplicate key is ambiguous
      ws();
      if (!eat(':')) return false;
      ws();
      Value member;
      if (!value(member, depth + 1)) return false;
      v.keys.emplace_back(std::move(key), std::move(member));
      ws();
      if (eat('}')) return true;
      if (!eat(',')) return false;
    }
  }

  bool array(Value& v, int depth) {
    v.type = Value::Type::Array;
    ++pos_;
    ws();
    if (eat(']')) return true;
    for (;;) {
      ws();
      Value item;
      if (!value(item, depth + 1)) return false;
      v.items.push_back(std::move(item));
      ws();
      if (eat(']')) return true;
      if (!eat(',')) return false;
    }
  }

  bool number(Value& v) {
    v.type = Value::Type::Int;
    const bool neg = eat('-');
    if (!more() || t_[pos_] < '0' || t_[pos_] > '9') return false;
    if (t_[pos_] == '0' && pos_ + 1 < t_.size() && t_[pos_ + 1] >= '0' && t_[pos_ + 1] <= '9') return false;  // 01
    int64_t n = 0;
    int digits = 0;
    while (more() && t_[pos_] >= '0' && t_[pos_] <= '9') {
      if (++digits > 15) return false;  // far beyond anything the manifest holds; keeps int64 safe
      n = n * 10 + (t_[pos_++] - '0');
    }
    if (more() && (t_[pos_] == '.' || t_[pos_] == 'e' || t_[pos_] == 'E')) return false;  // integers only
    v.i = neg ? -n : n;
    return true;
  }

  static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  bool unit(uint32_t& u) {
    if (pos_ + 4 > t_.size()) return false;
    u = 0;
    for (int k = 0; k < 4; ++k) {
      const int h = hex(t_[pos_ + k]);
      if (h < 0) return false;
      u = u * 16 + static_cast<uint32_t>(h);
    }
    pos_ += 4;
    return true;
  }

  static void utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  bool string(std::string& out) {
    ++pos_;  // opening quote
    out.clear();
    while (more()) {
      const unsigned char c = static_cast<unsigned char>(t_[pos_++]);
      if (c == '"') return true;
      if (c < 0x20) return false;  // raw control characters are not allowed
      if (out.size() >= kMaxString) return false;
      if (c != '\\') {
        out += static_cast<char>(c);
        continue;
      }
      if (!more()) return false;
      const char e = t_[pos_++];
      switch (e) {
        case '"':
          out += '"';
          break;
        case '\\':
          out += '\\';
          break;
        case '/':
          out += '/';
          break;
        case 'b':
          out += '\b';
          break;
        case 'f':
          out += '\f';
          break;
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        case 'u': {
          uint32_t u = 0;
          if (!unit(u)) return false;
          if (u >= 0xD800 && u <= 0xDBFF) {  // a surrogate pair: the low half must follow
            uint32_t low = 0;
            if (!(eat('\\') && eat('u') && unit(low)) || low < 0xDC00 || low > 0xDFFF) return false;
            u = 0x10000 + ((u - 0xD800) << 10) + (low - 0xDC00);
          } else if (u >= 0xDC00 && u <= 0xDFFF) {
            return false;
          }
          utf8(out, u);
          break;
        }
        default:
          return false;
      }
    }
    return false;  // unterminated
  }
};

}  // namespace

bool parse(const std::string& text, Value& out) {
  out = Value{};
  Parser p(text);
  if (p.run(out)) return true;
  out = Value{};
  return false;
}

}  // namespace mirror::ota::json
