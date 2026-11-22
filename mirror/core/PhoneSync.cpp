#include "core/PhoneSync.h"

namespace mirror {

SyncResult runPhoneSync(PhoneSyncIo& io, const uint32_t answerMs, const uint32_t joinMs) {
  if (!io.enoughMemory()) return {SyncOutcome::NoMemory};
  io.stage(SyncStage::Asking);
  io.sendRequest(gen::kSyncRequestStart);
  gen::SyncOffer offer;
  if (!io.waitOffer(offer, answerMs)) {
    io.sendRequest(gen::kSyncRequestDone);  // a phone that answers late must not leave its hotspot up
    return {SyncOutcome::NoAnswer};
  }
  if (offer.kind == gen::kSyncOfferRefused)
    return {SyncOutcome::Refused, static_cast<uint8_t>(offer.code)};  // nothing was started
  if (offer.kind != gen::kSyncOfferReady || offer.ssid.empty() || offer.proxy_port == 0) {
    io.sendRequest(gen::kSyncRequestDone);
    return {SyncOutcome::Stopped};
  }

  io.stage(SyncStage::Joining);
  if (!io.joinWifi(offer, joinMs)) {
    io.leaveWifi();
    io.sendRequest(gen::kSyncRequestDone);
    return {SyncOutcome::JoinFailed};
  }
  io.stage(SyncStage::Syncing);
  io.enableProxy(offer);
  const bool ok = io.runSync();
  io.disableProxy();
  io.leaveWifi();
  io.stage(SyncStage::Closing);
  io.sendRequest(gen::kSyncRequestDone);
  return {ok ? SyncOutcome::Done : SyncOutcome::SyncFailed};
}

}  // namespace mirror
