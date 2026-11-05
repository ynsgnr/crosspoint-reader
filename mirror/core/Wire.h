#pragma once
// Little-endian wire primitives under the generated codecs. Mirrors android/core/.../wire/Wire.kt.
// No exceptions: Writer and Reader latch a failure flag, checked once at the end.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mirror {

class Writer {
 public:
  void writeU8(uint32_t v) { buf_.push_back(static_cast<uint8_t>(v)); }
  void writeU16(uint32_t v) {
    writeU8(v & 0xFF);
    writeU8((v >> 8) & 0xFF);
  }
  void writeU32(uint32_t v) {
    for (int i = 0; i < 4; ++i) writeU8((v >> (8 * i)) & 0xFF);
  }
  void writeUvarint(uint32_t v) {
    while (v >= 0x80) {
      writeU8((v & 0x7F) | 0x80);
      v >>= 7;
    }
    writeU8(v);
  }
  void writeSvarint(int32_t v) { writeUvarint((static_cast<uint32_t>(v) << 1) ^ static_cast<uint32_t>(v >> 31)); }
  void writeBool(bool v) { writeU8(v ? 1 : 0); }
  void writeString(const std::string& s, size_t maxBytes) {
    if (s.size() > maxBytes) {
      fail();
      return;
    }
    writeUvarint(static_cast<uint32_t>(s.size()));
    buf_.insert(buf_.end(), s.begin(), s.end());
  }
  void writeBytes(const uint8_t* p, size_t n) { buf_.insert(buf_.end(), p, p + n); }
  void fail() { ok_ = false; }
  bool ok() const { return ok_; }
  const std::vector<uint8_t>& bytes() const { return buf_; }
  std::vector<uint8_t> take() { return std::move(buf_); }

 private:
  std::vector<uint8_t> buf_;
  bool ok_ = true;
};

class Reader {
 public:
  Reader(const uint8_t* data, size_t size) : p_(data), end_(data + size) {}
  bool ok() const { return ok_; }
  void fail() { ok_ = false; }
  size_t remaining() const { return static_cast<size_t>(end_ - p_); }
  bool atEnd() const { return p_ == end_; }

  uint8_t readU8() {
    if (!need(1)) return 0;
    return *p_++;
  }
  uint16_t readU16() {
    uint16_t lo = readU8();
    uint16_t hi = readU8();
    return static_cast<uint16_t>(lo | (hi << 8));
  }
  uint32_t readU32() {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(readU8()) << (8 * i);
    return v;
  }
  uint32_t readUvarint() {
    uint32_t v = 0;
    for (int shift = 0;; shift += 7) {
      if (shift > 28) {
        fail();
        return 0;
      }
      const uint8_t b = readU8();
      if (!ok_) return 0;
      if (shift == 28 && (b & 0x70) != 0) {  // would overflow 32 bits
        fail();
        return 0;
      }
      v |= static_cast<uint32_t>(b & 0x7F) << shift;
      if ((b & 0x80) == 0) return v;
    }
  }
  int32_t readSvarint() {
    const uint32_t u = readUvarint();
    return static_cast<int32_t>((u >> 1) ^ (~(u & 1) + 1));
  }
  bool readBool() {
    const uint8_t b = readU8();
    if (b > 1) fail();
    return b == 1;
  }
  std::string readString(size_t maxBytes) {
    const uint32_t n = readUvarint();
    if (!ok_ || n > maxBytes || !need(n)) {
      fail();
      return {};
    }
    std::string s(reinterpret_cast<const char*>(p_), n);
    p_ += n;
    return s;
  }

 private:
  bool need(size_t n) {
    if (!ok_ || remaining() < n) {
      ok_ = false;
      return false;
    }
    return true;
  }
  const uint8_t* p_;
  const uint8_t* end_;
  bool ok_ = true;
};

}  // namespace mirror
