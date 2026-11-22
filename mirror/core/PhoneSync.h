#pragma once
// The device side of a Wi-Fi sync over the phone, as one blocking sequence with the platform behind an interface so the
// order is host-tested: ask the phone, wait for its offer, join its hotspot, send everything through its proxy, and
// always undo (proxy off, Wi-Fi off, tell the phone) whatever happened. The phone stops the hotspot itself when it
// hears "done", loses the BLE link, or after the offer's valid_seconds.
#include <cstdint>

#include "generated/protocol_gen.h"

namespace mirror {

enum class SyncStage { Asking, Joining, Syncing, Closing };
enum class SyncOutcome {
  Done,        // the sync ran and nothing failed
  SyncFailed,  // it ran but something failed (see the OPDS result log)
  NoMemory,    // not enough free heap for a TLS download next to the Bluetooth stack
  NoAnswer,    // the phone's app did not answer (not running, Wi-Fi sync off, or too old)
  Refused,     // the phone said no: SyncResult::refusal is gen::kSyncRefused*
  Stopped,     // the phone ended it, or its offer was unusable
  JoinFailed,  // the hotspot could not be joined
};

struct SyncResult {
  SyncOutcome outcome = SyncOutcome::Done;
  uint8_t refusal = 0;
};

class PhoneSyncIo {
 public:
  virtual ~PhoneSyncIo() = default;
  virtual bool enoughMemory() = 0;
  virtual void stage(SyncStage s) = 0;
  virtual void sendRequest(uint8_t kind) = 0;                      // gen::kSyncRequest*
  virtual bool waitOffer(gen::SyncOffer& offer, uint32_t ms) = 0;  // keeps the BLE link served while it waits
  virtual bool joinWifi(const gen::SyncOffer& offer, uint32_t ms) = 0;
  virtual void enableProxy(const gen::SyncOffer& offer) = 0;  // the shim: all connections now go through the phone
  virtual void disableProxy() = 0;
  virtual bool runSync() = 0;  // blocking; true when nothing failed
  virtual void leaveWifi() = 0;
};

SyncResult runPhoneSync(PhoneSyncIo& io, uint32_t answerMs = 20000, uint32_t joinMs = 30000);

}  // namespace mirror
