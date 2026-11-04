#include "core/Session.h"

namespace mirror {

void Session::emit(uint8_t templateId, uint8_t schema, uint8_t flags, uint16_t base,
                   const std::vector<uint8_t>& payload) {
  Envelope e;
  e.templateId = templateId;
  e.schemaVersion = schema;
  e.flags = flags;
  e.seq = outSeq_;
  e.baseSeq = base;
  e.payload = payload;
  outSeq_ = static_cast<uint16_t>(outSeq_ + 1);
  if (outSeq_ == 0) outSeq_ = 1;
  send_(encodeEnvelope(e));
}

void Session::emitError(uint8_t code, uint8_t detail) {
  Writer w;
  gen::ControlEvent{code, detail}.encode(w);
  emit(static_cast<uint8_t>(gen::kControlTemplateId), 0, gen::kFlagEvent, 0, w.bytes());
}

void Session::emitResync(uint8_t templateId, uint8_t schema) {
  resync_ = true;
  awaitingFull_ = true;
  emit(templateId, schema, gen::kFlagResync, lastApplied_);
}

bool Session::feed(const uint8_t* data, size_t len) {
  std::vector<Envelope> envs;
  parser_.feed(data, len, envs);
  if (parser_.crcErrors() != crcSeen_) {  // header untrusted after a CRC failure: ask for everything
    crcSeen_ = parser_.crcErrors();
    emitResync(static_cast<uint8_t>(gen::kControlTemplateId), 0);
  }
  bool changed = false;
  for (const Envelope& e : envs) handle(e, changed);
  return changed;
}

void Session::handle(const Envelope& e, bool& changed) {
  if (e.protocolVersion != gen::kProtocolVersion) return emitError(gen::kErrBadProtocol, e.protocolVersion);
  if (e.templateId == gen::kControlTemplateId) return handleControl(e, changed);
  const TemplateEntry* t = registry_.find(e.templateId);
  if (!t) return emitError(gen::kErrUnknownTemplate, e.templateId);
  if (e.schemaVersion < t->minSchema || e.schemaVersion > t->maxSchema)
    return emitError(gen::kErrSchemaMismatch, e.schemaVersion);

  const bool sameTemplate = active_ && active_->templateId() == e.templateId;
  std::unique_ptr<TemplateRenderer> fresh;
  TemplateRenderer* target = active_.get();
  if (e.has(gen::kFlagFull)) {
    if (!sameTemplate) {
      fresh = t->create();
      target = fresh.get();
    }
    if (!target->onFull(e.payload.data(), e.payload.size())) return emitError(gen::kErrBadPayload, 0);
    if (fresh) {
      active_ = std::move(fresh);
      switched_ = true;
    }
  } else if (e.has(gen::kFlagDiff)) {
    if (awaitingFull_) return;  // the phone's full reply is on its way; one resync per gap is enough
    if (!sameTemplate || e.baseSeq != lastApplied_)
      return emitResync(e.templateId, target ? target->schemaVersion() : t->maxSchema);
    if (!target->onDiff(e.payload.data(), e.payload.size())) return emitResync(e.templateId, target->schemaVersion());
  } else {
    return;
  }
  if (e.has(gen::kFlagFull)) awaitingFull_ = false;
  lastApplied_ = e.seq;
  changed = true;
  if (e.has(gen::kFlagAckRequest)) emit(e.templateId, e.schemaVersion, gen::kFlagAck, e.seq);
}

void Session::handleControl(const Envelope& e, bool& changed) {
  if (!e.has(gen::kFlagStatus)) return;
  Reader r(e.payload.data(), e.payload.size());
  gen::PhoneStatus status;
  if (!gen::PhoneStatus::decode(r, status) || !r.atEnd()) return;
  phone_ = status;
  changed = true;
}

std::string Session::phoneDump() const {
  if (!phone_) return "-";
  return "dark=" + std::string(phone_->dark_mode ? "1" : "0") + " minutes=" + std::to_string(phone_->minutes_left) +
         " locked=" + (phone_->locked ? "1" : "0");
}

void Session::sendCommand(uint8_t command, uint8_t arg) {
  Writer w;
  gen::ControlCommand{command, arg}.encode(w);
  emit(static_cast<uint8_t>(gen::kControlTemplateId), 0, gen::kFlagCommand, 0, w.bytes());
}

bool Session::onButton(Button b) {
  if (!active_) return false;
  std::vector<uint8_t> payload;
  if (!active_->onButton(b, payload)) return false;
  emit(active_->templateId(), active_->schemaVersion(), gen::kFlagEvent, 0, payload);
  return true;
}

std::vector<uint8_t> Session::capabilities(const Gfx& gfx, const char* deviceName) const {
  gen::Capabilities c;
  c.protocol_version = gen::kProtocolVersion;
  c.max_payload = maxPayload_;
  c.features = gen::kFeaturePhoneStatus;
  c.screen_width = static_cast<uint16_t>(gfx.width());
  c.screen_height = static_cast<uint16_t>(gfx.height());
  c.device_name = deviceName;
  for (const auto& e : registry_.entries()) c.templates.push_back({e.id, e.minSchema, e.maxSchema});
  for (const auto& f : gfx.fonts())
    c.fonts.push_back({f.name, static_cast<uint8_t>(f.sizePx), static_cast<uint8_t>(f.lineHeightPx)});
  Writer w;
  c.encode(w);
  return w.take();
}

}  // namespace mirror
