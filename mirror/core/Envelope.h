#pragma once
// Common-layer framing; payloads are opaque here.
// magic u8 | proto u8 | template u8 | schema u8 | flags u8 | seq u16 | base_seq u16 | len u16 | payload | crc u16 (LE)
#include <cstddef>
#include <cstdint>
#include <vector>

#include "protocol_gen.h"

namespace mirror {

struct Envelope {
  uint8_t protocolVersion = gen::kProtocolVersion;
  uint8_t templateId = 0;
  uint8_t schemaVersion = 0;
  uint8_t flags = 0;
  uint16_t seq = 0;
  uint16_t baseSeq = 0;
  std::vector<uint8_t> payload;

  bool has(uint8_t flag) const { return (flags & flag) != 0; }
};

std::vector<uint8_t> encodeEnvelope(const Envelope& e);

// Reassembles envelopes from a byte stream split at arbitrary points. A bad magic, length or CRC
// drops one byte and rescans for the next magic; crcErrors counts CRC failures only.
class EnvelopeStreamParser {
 public:
  explicit EnvelopeStreamParser(size_t maxPayload = 2048) : maxPayload_(maxPayload) {}
  void feed(const uint8_t* data, size_t len, std::vector<Envelope>& out);
  uint32_t crcErrors() const { return crcErrors_; }
  uint32_t framingErrors() const { return framingErrors_; }
  void reset() { buf_.clear(); }

 private:
  std::vector<uint8_t> buf_;
  size_t maxPayload_;
  uint32_t crcErrors_ = 0;
  uint32_t framingErrors_ = 0;
};

}  // namespace mirror
