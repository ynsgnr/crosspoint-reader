#include "core/Envelope.h"

#include <algorithm>

#include "core/Crc16.h"

namespace mirror {

std::vector<uint8_t> encodeEnvelope(const Envelope& e) {
  std::vector<uint8_t> b;
  b.reserve(gen::kHeaderSize + e.payload.size() + gen::kCrcSize);
  b.push_back(static_cast<uint8_t>(gen::kMagic));
  b.push_back(e.protocolVersion);
  b.push_back(e.templateId);
  b.push_back(e.schemaVersion);
  b.push_back(e.flags);
  b.push_back(e.seq & 0xFF);
  b.push_back(e.seq >> 8);
  b.push_back(e.baseSeq & 0xFF);
  b.push_back(e.baseSeq >> 8);
  const uint16_t len = static_cast<uint16_t>(e.payload.size());
  b.push_back(len & 0xFF);
  b.push_back(len >> 8);
  b.insert(b.end(), e.payload.begin(), e.payload.end());
  const uint16_t crc = crc16(b.data(), b.size());
  b.push_back(crc & 0xFF);
  b.push_back(crc >> 8);
  return b;
}

namespace {
uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
}  // namespace

void EnvelopeStreamParser::feed(const uint8_t* data, size_t len, std::vector<Envelope>& out) {
  buf_.insert(buf_.end(), data, data + len);
  size_t pos = 0;
  while (true) {
    while (pos < buf_.size() && buf_[pos] != gen::kMagic) {
      ++pos;
      ++framingErrors_;
    }
    const size_t avail = buf_.size() - pos;
    if (avail < gen::kHeaderSize) break;
    const uint8_t* h = buf_.data() + pos;
    const size_t plen = u16(h + 9);
    if (plen > maxPayload_) {
      ++framingErrors_;
      ++pos;
      continue;
    }
    const size_t total = gen::kHeaderSize + plen + gen::kCrcSize;
    if (avail < total) break;
    if (u16(h + total - 2) != crc16(h, total - 2)) {
      ++crcErrors_;
      ++pos;
      continue;
    }
    Envelope e;
    e.protocolVersion = h[1];
    e.templateId = h[2];
    e.schemaVersion = h[3];
    e.flags = h[4];
    e.seq = u16(h + 5);
    e.baseSeq = u16(h + 7);
    e.payload.assign(h + gen::kHeaderSize, h + gen::kHeaderSize + plen);
    out.push_back(std::move(e));
    pos += total;
  }
  buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(pos));
}

}  // namespace mirror
