/**
 * @file max22200.ipp
 * @brief MAX22200 driver implementation, included by `max22200.hpp`.
 * @copyright Copyright (c) 2026 The driver-max22200 authors. SPDX-License-Identifier: MIT
 *
 * Section, table and figure names refer to the datasheet, Analog Devices 19-100531 Rev 1 (3/25).
 *
 * The driver keeps a copy of every register it writes (`config_`, `on_mask_`): STATUS has no
 * separate ONCH register, so a 32-bit STATUS write carries the on/off state the driver last set,
 * and a single-channel switch is computed from the copy instead of a STATUS read (a read would
 * cost two transfers and clear OVT and UVM as a side effect).
 */
#ifndef MAX22200_IPP
#define MAX22200_IPP

namespace max22200 {

// -------------------------------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------------------------------

template <SpiPort Port>
Result<void> MAX22200<Port>::EnsureInitialized() noexcept {
  if (initialized_) {
    return Result<void>::Success();
  }
  if (auto r = initialize(); !r) {
    return r;
  }
  initialized_ = true;
  // Every register was just read back: nothing written is in doubt. A COMER from here on that
  // follows a fault byte with COMER clear is a new miscount (p34).
  comer_clear_ =
      (last_status_ != 0xFFU) && ((static_cast<uint32_t>(last_status_) & reg::kComer) == 0U);
  comer_new_ = false;
  unverified_onch_ = 0;
  unverified_banks_ = 0;
  stale_banks_ = 0;
  pair_uncertain_ = 0;
  clock_uncertain_ = false;
  unverified_pairs_ = 0;
  unverified_clock_ = false;
  detail::Log(bus_.GetPort(), LogLevel::Info, "max22200: initialised");
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> MAX22200<Port>::initialize() noexcept {
  if (Validate(config_) != Error::Ok) {
    return fail<void>(Error::InvalidArgument, "max22200: invalid Config");
  }
  if (!bus_.GetPort().EnsureInitialized()) {
    return fail<void>(Error::Bus, "max22200: the port could not bring the bus up");
  }
  // CMD idles high (the Command Register is written with CMD high, p25-26); ENABLE rising
  // edge, then tEN before the first access ("Enable Logic Input", Electrical Characteristics). The
  // waits start when the pin write has returned, so a slow expander only adds to them.
  Error e = bus_.SetPin(CtrlPin::Cmd, GpioSignal::ACTIVE);
  if (e == Error::Ok) {
    e = bus_.SetPin(CtrlPin::Enable, GpioSignal::ACTIVE);
  }
  onch_ready_ = false;
  cfg_uncertain_ = 0; // every CFG_CHn is written and read back below
  startup_faults_ = Faults{};
  Result<void> r = Result<void>::Failure(e);
  if (e == Error::Ok) {
    detail::DelayUs(bus_.GetPort(), reg::kEnableUs);
    r = attempt();
  }
  // Figure 6: a COMER repeats the procedure. Between attempts ENABLE goes low for tDIS and high
  // again for tEN: the datasheet does not say what clears COMER, and ENABLE is the strongest reset
  // the driver has short of a power cycle. Only a COMER repeats; every other outcome ends the loop.
  for (uint8_t i = 1; i < kStartAttempts; ++i) {
    if (r.error != Error::CommunicationError) {
      break;
    }
    r = restartEnable();
    if (r) {
      r = attempt();
    }
  }
  latch_uvm_ = true;
  if (!r) {
    // Leave the chip asleep, except when it is still waking: ENABLE low disables the device
    // ("Enable Logic Input", p14), so a retry after Undervoltage or Timeout would wait tEN and the
    // wake again from the start. Every channel is off in both cases.
    if ((r.error != Error::Undervoltage) && (r.error != Error::Timeout)) {
      (void)bus_.SetPin(CtrlPin::Enable, GpioSignal::INACTIVE);
    }
    on_mask_ = 0;
    uncertain_ = 0;
    onch_ready_ = false;
    return fail<void>(r.error, "max22200: start-up failed");
  }
  return r;
}

template <SpiPort Port>
Result<void> MAX22200<Port>::restartEnable() noexcept {
  if (const Error e = bus_.SetPin(CtrlPin::Enable, GpioSignal::INACTIVE); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  detail::DelayUs(bus_.GetPort(), reg::kDisableUs);
  if (const Error e = bus_.SetPin(CtrlPin::Enable, GpioSignal::ACTIVE); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  detail::DelayUs(bus_.GetPort(), reg::kEnableUs);
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> MAX22200<Port>::attempt() noexcept {
  on_mask_ = 0;
  uncertain_ = 0;
  comer_armed_ = false;
  comer_seen_ = false;
  echo_bad_ = false;
  latch_uvm_ = false; // the power-on UVM is what steps 1 and 2 clear: not news
  // Step 1: read STATUS, which clears UVM and releases FAULT.
  const auto first = read32(reg::kStatus);
  if (!first) {
    return Result<void>::Failure(first.error);
  }
  if ((first.value == 0xFFFFFFFFU) && (last_status_ == 0xFFU)) {
    return Result<void>::Failure(Error::NoDevice);
  }
  // Step 2, first half: ACTIVE. UVM must be cleared "to enable operations" (p27) and clears only
  // with VM above UVLO (p22), so a write may not take while the supply settles, and may raise
  // COMER: wake it with bare ACTIVE writes until ACTIVE reads 1 with UVM clear ("wake-up
  // completed?").
  const auto awake = wake();
  if (!awake) {
    return Result<void>::Failure(awake.error);
  }
  // COMER is "Read Only" (p36): nothing says a read clears it. If the wake left it set, a new one
  // cannot be told from the old and the read-back of every register decides; otherwise a COMER
  // from here on is new and repeats the procedure.
  comer_armed_ = (awake.value & reg::kComer) == 0U;
  if (!comer_armed_) {
    // A COMER left by the wake would hide every later miscount for the whole session. ENABLE
    // low is the strongest clear the driver has short of a power cycle (the registers, ACTIVE
    // included, survive it, p14); a bounded number of tries, then the session runs degraded
    // (every switch-off read back, see writeOnch()).
    if (auto r = clearComer(); !r) {
      return r;
    }
  }
  latch_uvm_ = true; // awake with UVM clear: a UVM from here on is a real undervoltage
  // Steps 2 and 3: STATUS (masks, FREQM, CMxy, every ONCH 0, ACTIVE = 1), then every channel.
  if (auto r = writeAll(); !r) {
    return r;
  }
  if (comer_seen_) {
    return Result<void>::Failure(Error::CommunicationError);
  }
  if (echo_bad_) {
    return Result<void>::Failure(Error::DeviceIdMismatch); // Figure 10: SDO echoes the command
  }
  // Step 4: STATUS must hold what was written, ACTIVE included, and UVM must be clear.
  if (auto r = checkStatus(); !r) {
    return r;
  }
  // Faults latched before the start (the registers survive until a POR, p22; ENABLE low is not one,
  // p14): read and clear them, so the FAULT pin shows new faults only, and report them (handbook
  // KB-CPP-058). Before the register read-back, so a COMER in this read's fault byte is still
  // caught.
  if (auto r = clearStartupFaults(); !r) {
    return r;
  }
  return checkRegisters();
}

template <SpiPort Port>
Result<void> MAX22200<Port>::clearComer() noexcept {
  for (uint8_t i = 0; (i < kStartAttempts) && !comer_armed_; ++i) {
    if (auto r = restartEnable(); !r) {
      return r;
    }
    auto s = read32(reg::kStatus);
    if (s && (((s.value & reg::kActive) == 0U) || ((s.value & reg::kUvm) != 0U))) {
      s = wake(); // the chip did not keep ACTIVE through ENABLE low: wake it again
    }
    if (!s) {
      return Result<void>::Failure(s.error);
    }
    comer_armed_ = (s.value & reg::kComer) == 0U;
  }
  if (!comer_armed_) {
    detail::Log(bus_.GetPort(), LogLevel::Warning,
                "max22200: COMER stays set: every switch-off is read back");
  }
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> MAX22200<Port>::clearStartupFaults() noexcept {
  using detail::operator""_u32;
  // "Clearing the FAULT Register": a read with every SDI bit high clears every flag on the
  // MAX22200A; the MAX22200 clears every flag on any read. Either part, every flag.
  const auto f = read32(reg::kFault, reg::FaultClearSdi(0xFFFF'FFFF_u32));
  if (!f) {
    return Result<void>::Failure(f.error);
  }
  latchFaults(f.value, 0xFFFF'FFFF_u32);
  startup_faults_ = startup_faults_.Or(DecodeFaults(f.value));
  if (startup_faults_.Any()) {
    detail::Log(bus_.GetPort(), LogLevel::Warning,
                "max22200: faults latched before the start were cleared (GetStartupFaults)");
  }
  return Result<void>::Success();
}

template <SpiPort Port>
Result<uint32_t> MAX22200<Port>::wake() noexcept {
  // Figure 6, "wake-up completed?", bounded by Config::wake_timeout_us. Each round: a bare ACTIVE
  // write (no mask bits: the masks and pair modes go in the full STATUS write of step 2 once the
  // chip is awake, so a refused write costs only a retry), tWU, a STATUS read, a pause to
  // kWakePollUs. The write's command echo is checked: a stuck or deaf bus fails here at once
  // instead of at the timeout.
  uint32_t waited_us = 0;
  while (true) {
    const auto poke = put32(reg::kStatus, reg::kActive);
    if (!poke) {
      return Result<uint32_t>::Failure(poke.error);
    }
    if (poke.value.echo != reg::Command(reg::kStatus, true, false)) {
      return Result<uint32_t>::Failure(Error::DeviceIdMismatch);
    }
    detail::DelayUs(bus_.GetPort(), reg::kWakeUpUs);
    waited_us += reg::kWakeUpUs;
    const auto s = read32(reg::kStatus);
    if (!s) {
      return s;
    }
    const bool uvm = (s.value & reg::kUvm) != 0U;
    if (((s.value & reg::kActive) != 0U) && !uvm) {
      return s;
    }
    if (waited_us >= config_.wake_timeout_us) {
      return Result<uint32_t>::Failure(uvm ? Error::Undervoltage : Error::Timeout);
    }
    detail::DelayUs(bus_.GetPort(), kWakePollUs - reg::kWakeUpUs);
    waited_us += kWakePollUs - reg::kWakeUpUs;
  }
}

template <SpiPort Port>
Result<void> MAX22200<Port>::writeAll() noexcept {
  using detail::operator""_u8;
  const auto status = put32(reg::kStatus, EncodeStatus(config_, 0_u8, true));
  if (!status) {
    return Result<void>::Failure(status.error);
  }
  echo_bad_ = echo_bad_ || (status.value.echo != reg::Command(reg::kStatus, true, false));
  for (uint8_t ch = 0; ch < kChannels; ++ch) {
    const uint8_t bank = reg::CfgChannel(ch);
    const auto r = put32(bank, EncodeChannel(config_.channels[ch]));
    if (!r) {
      return Result<void>::Failure(r.error);
    }
    echo_bad_ = echo_bad_ || (r.value.echo != reg::Command(bank, true, false));
  }
  const auto dpm = put32(reg::kCfgDpm, EncodeDpm(config_.dpm));
  if (!dpm) {
    return Result<void>::Failure(dpm.error);
  }
  echo_bad_ = echo_bad_ || (dpm.value.echo != reg::Command(reg::kCfgDpm, true, false));
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> MAX22200<Port>::checkStatus() noexcept {
  using detail::operator""_u8;
  const auto s = read32(reg::kStatus);
  if (!s) {
    return Result<void>::Failure(s.error);
  }
  if ((s.value & reg::kWritableMask) != EncodeStatus(config_, 0_u8, true)) {
    return Result<void>::Failure(comer_seen_ ? Error::CommunicationError : Error::DeviceIdMismatch);
  }
  if ((s.value & reg::kUvm) != 0U) {
    return Result<void>::Failure(Error::Undervoltage); // VM dipped during the configuration
  }
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> MAX22200<Port>::checkRegisters() noexcept {
  // Not a datasheet step: the start-up check that every configuration register holds what was
  // written (a frame lost on a shared bus, a chip that ignored a write).
  for (uint8_t ch = 0; ch < kChannels; ++ch) {
    const auto r = read32(reg::CfgChannel(ch));
    if (!r) {
      return Result<void>::Failure(r.error);
    }
    if (r.value != EncodeChannel(config_.channels[ch])) {
      return Result<void>::Failure(comer_seen_ ? Error::CommunicationError : Error::WriteMismatch);
    }
  }
  // CFG_DPM bits 31:15 are reserved, three of them read only with no reset value (p40-41).
  const auto dpm = read32(reg::kCfgDpm);
  if (!dpm) {
    return Result<void>::Failure(dpm.error);
  }
  if ((dpm.value & reg::kDpmUsedMask) != EncodeDpm(config_.dpm)) {
    return Result<void>::Failure(comer_seen_ ? Error::CommunicationError : Error::WriteMismatch);
  }
  return comer_seen_ ? Result<void>::Failure(Error::CommunicationError) : Result<void>::Success();
}

template <SpiPort Port>
Result<void> MAX22200<Port>::Deinitialize() noexcept {
  using detail::operator""_u8;
  if (!initialized_) {
    return Result<void>::Success();
  }
  initialized_ = false;
  comer_new_ = false;
  // ACTIVE = 0 puts the channels Hi-Z (Table 10); ENABLE low puts the chip to sleep.
  const auto w = put32(reg::kStatus, EncodeStatus(config_, 0_u8, false));
  on_mask_ = 0;
  uncertain_ = 0;
  onch_ready_ = false;
  const Error pin = bus_.SetPin(CtrlPin::Enable, GpioSignal::INACTIVE);
  const Error e = !w ? w.error : pin;
  if (e != Error::Ok) {
    return fail<void>(e, "max22200: shutdown failed");
  }
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> MAX22200<Port>::ApplyConfig(const Config& config) noexcept {
  if (Validate(config) != Error::Ok) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (!initialized_) {
    config_ = config;
    return Result<void>::Success();
  }
  if (modeChangeBlocked(config, static_cast<uint8_t>((stale_banks_ >> 1U) & 0xFFU))) {
    return Result<void>::Failure(Error::ChannelsActive);
  }
  config_.variant = config.variant;
  config_.rref_ohm = config.rref_ohm;
  config_.wake_timeout_us = config.wake_timeout_us;
  config_.loaded_channels =
      config.loaded_channels; // never sent; Validate() checked it with the rest
  if (auto r = applyRunning(config); !r) {
    return fail<void>(r.error, "max22200: configuration write failed");
  }
  return report(Result<void>::Success());
}

template <SpiPort Port>
Result<void> MAX22200<Port>::applyRunning(const Config& next) noexcept {
  using detail::operator""_u8;
  // A register a miscounted write may have missed (COMER, p34) is written again even if the copy
  // already holds the value.
  if ((EncodeStatus(next, 0_u8, true) != EncodeStatus(config_, 0_u8, true)) ||
      stale(reg::kStatus)) {
    if (auto r = writeStatus(next, Verify::None); !r) {
      return r;
    }
  }
  for (uint8_t ch = 0; ch < kChannels; ++ch) {
    if ((next.channels[ch] == config_.channels[ch]) && !stale(reg::CfgChannel(ch))) {
      continue;
    }
    if (auto r = put32(reg::CfgChannel(ch), EncodeChannel(next.channels[ch])); !r) {
      cfg_uncertain_ =
          static_cast<uint8_t>(static_cast<uint32_t>(cfg_uncertain_) | detail::ChannelBit(ch));
      return Result<void>::Failure(r.error);
    }
    config_.channels[ch] = next.channels[ch];
    // With COMER set (degraded) nothing will vouch for this write: the channel stays of either
    // mode.
    cfg_uncertain_ = comer_clear_
                         ? static_cast<uint8_t>(static_cast<uint32_t>(cfg_uncertain_) &
                                                ~static_cast<uint32_t>(detail::ChannelBit(ch)))
                         : static_cast<uint8_t>(static_cast<uint32_t>(cfg_uncertain_) |
                                                static_cast<uint32_t>(detail::ChannelBit(ch)));
  }
  if (!(next.dpm == config_.dpm) || stale(reg::kCfgDpm)) {
    if (auto r = put32(reg::kCfgDpm, EncodeDpm(next.dpm)); !r) {
      return Result<void>::Failure(r.error);
    }
    config_.dpm = next.dpm;
  }
  return Result<void>::Success();
}

template <SpiPort Port>
bool MAX22200<Port>::modeChangeBlocked(const Config& next, uint8_t rewrite) const noexcept {
  // Programming Procedure 5c: VDRnCDR and HSnLS change only with every channel off. CMxy: "can be
  // written only when" both channels of the pair are off (Register Details). A channel a failed
  // ONCH write may have switched counts as on: the chip would keep the old mode with the new codes.
  // A channel whose last CFG write failed or read back different may hold either mode: any
  // write to it counts as a mode change.
  const auto on =
      static_cast<uint8_t>(static_cast<uint32_t>(on_mask_) | static_cast<uint32_t>(uncertain_));
  if (on == 0U) {
    return false;
  }
  for (uint8_t ch = 0; ch < kChannels; ++ch) {
    const ChannelConfig& a = config_.channels[ch];
    const ChannelConfig& b = next.channels[ch];
    // Written: it differs, or the caller writes it again (`rewrite`, ApplyConfig()'s stale banks).
    // While a channel is on the chip keeps the old mode bits: the write would not settle the mode.
    const bool unsure = ((static_cast<uint32_t>(cfg_uncertain_) & detail::ChannelBit(ch)) != 0U) &&
                        (!(a == b) || (((static_cast<uint32_t>(rewrite) >> ch) & 1U) != 0U));
    if ((a.drive != b.drive) || (a.side != b.side) || unsure) {
      return true;
    }
  }
  for (uint8_t p = 0; p < kPairs; ++p) {
    const bool unknown = ((static_cast<uint32_t>(pair_uncertain_) >> p) & 1U) != 0U;
    if (((config_.pairs[p] != next.pairs[p]) || unknown) &&
        ((static_cast<uint32_t>(on) & static_cast<uint32_t>(detail::PairBits(p))) != 0U)) {
      return true;
    }
  }
  return false;
}

// -------------------------------------------------------------------------------------------------
// Switching channels
// -------------------------------------------------------------------------------------------------

template <SpiPort Port>
bool MAX22200<Port>::validOnMask(uint8_t mask) const noexcept {
  // Table 6: in parallel mode SPI drives a pair with ONCHx and ONCHy both high. Register Details
  // name only the lower channel's ONCH; switching both together satisfies either reading.
  for (uint8_t p = 0; p < kPairs; ++p) {
    const auto bits = static_cast<uint8_t>(static_cast<uint32_t>(mask) &
                                           static_cast<uint32_t>(detail::PairBits(p)));
    if ((config_.pairs[p] == PairMode::Parallel) && (bits != 0U) && (bits != detail::PairBits(p))) {
      return false;
    }
  }
  return true;
}

template <SpiPort Port>
bool MAX22200<Port>::pairUnknown(uint8_t& mask) const noexcept {
  // Tables 6 and 7: what ONCHx and ONCHy do depends on CMxy. With CMxy unknown only "both off"
  // means the same in every mode (independent, parallel, full-bridge HiZ). In such a pair: bits
  // kept as they are go out unchanged; any other request clears both bits (never one: in a full
  // bridge, Brake 11 with one bit cleared is Forward or Reverse), and a switch-on in it is
  // reported. Off, not the copy's bits: after a miscount the copy may show a bit on that the chip
  // has off, and sending it would switch a channel on in an unknown mode. The other pairs go out as
  // asked, so no switch-off is ever held back.
  bool refuse = false;
  for (uint8_t p = 0; p < kPairs; ++p) {
    const uint32_t pb = detail::PairBits(p);
    const uint32_t want = static_cast<uint32_t>(mask) & pb;
    const uint32_t now = static_cast<uint32_t>(on_mask_) & pb;
    // A bit that may still be on counts as on: asking it off is a switch-off.
    const uint32_t maybe = static_cast<uint32_t>(maybeOn()) & pb;
    const bool asks_on = (want & ~now) != 0U;
    const bool asks_off = (maybe & ~want) != 0U;
    if ((((static_cast<uint32_t>(pair_uncertain_) >> p) & 1U) == 0U) || (!asks_on && !asks_off)) {
      continue;
    }
    refuse = refuse || asks_on;
    mask = static_cast<uint8_t>(static_cast<uint32_t>(mask) & ~pb); // both off
  }
  return refuse;
}

template <SpiPort Port>
Result<void> MAX22200<Port>::SetChannelsOn(uint8_t mask, Verify verify) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  uint8_t send = mask;
  const bool unknown = pairUnknown(send); // adjusted: switch-offs kept, unknown switch-ons dropped
  if (!isVerify(verify) || !validOnMask(send)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (!unknown) {
    return report(writeOnch(send, verify));
  }
  // A switch-on was dropped: send the rest (every switch-off in it), then report the refusal.
  // Nothing to send only when the mask equals the copy and asks no bit that may still be on to go
  // off (maybeOn(): a failed write, or a switch-off no clean fault byte has vouched for yet).
  if ((send != on_mask_) ||
      ((static_cast<uint32_t>(maybeOn()) & ~static_cast<uint32_t>(send)) != 0U)) {
    if (auto r = report(writeOnch(send, verify)); !r) {
      return r;
    }
  }
  return Result<void>::Failure(Error::Uncertain);
}

template <SpiPort Port>
Result<void> MAX22200<Port>::SetChannel(uint8_t channel, bool on, Verify verify) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if ((channel >= kChannels) || !isVerify(verify)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  const auto pair = static_cast<uint8_t>(channel / 2U);
  const PairMode mode = config_.pairs[pair];
  const auto pair_bits = static_cast<uint32_t>(detail::PairBits(pair));
  if (((static_cast<uint32_t>(pair_uncertain_) >> pair) & 1U) != 0U) {
    // The pair's mode is unknown: an off request switches both of its channels off, the one state
    // that means the same in every mode; a switch-on depends on the mode and is refused.
    if (on) {
      return Result<void>::Failure(Error::Uncertain);
    }
    return report(
        writeOnch(static_cast<uint8_t>(static_cast<uint32_t>(on_mask_) & ~pair_bits), verify));
  }
  if (mode == PairMode::FullBridge) {
    if (on) {
      return Result<void>::Failure(Error::InvalidArgument); // use SetBridge()
    }
    // Off is never refused: in a full bridge it is HiZ, both bits 0 (Table 7), the partner too.
    return report(
        writeOnch(static_cast<uint8_t>(static_cast<uint32_t>(on_mask_) & ~pair_bits), verify));
  }
  const auto bits =
      (mode == PairMode::Parallel) ? pair_bits : static_cast<uint32_t>(detail::ChannelBit(channel));
  const uint32_t now = on_mask_;
  const auto next = static_cast<uint8_t>(on ? (now | bits) : (now & ~bits));
  return report(writeOnch(next, verify));
}

template <SpiPort Port>
Result<void> MAX22200<Port>::SetBridge(uint8_t pair, BridgeState state, Verify verify) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if ((pair >= kPairs) || !detail::IsEnumerator(state) || !isVerify(verify)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  const bool unknown = ((static_cast<uint32_t>(pair_uncertain_) >> pair) & 1U) != 0U;
  if (unknown && (state != BridgeState::HiZ)) {
    return Result<void>::Failure(Error::Uncertain);
  }
  // HiZ (both bits 0) is off in every mode: allowed in a pair whose mode is unknown.
  if (!unknown && (config_.pairs[pair] != PairMode::FullBridge)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  // Table 7: ONCHx is the pair's lower bit; Forward = ONCHx only, Reverse = ONCHy only.
  const uint32_t bits = static_cast<uint32_t>(detail::ToUnderlying(state))
                        << (2U * static_cast<uint32_t>(pair));
  const auto next = static_cast<uint8_t>(
      (static_cast<uint32_t>(on_mask_) & ~static_cast<uint32_t>(detail::PairBits(pair))) | bits);
  return report(writeOnch(next, verify));
}

template <SpiPort Port>
Result<void> MAX22200<Port>::writeOnch(uint8_t mask, Verify verify) noexcept {
  // Programming Procedure 5a: ONCH with one 8-bit write of STATUS[31:24]. The command stays in
  // force until it is rewritten (p25): right after another ONCH write only the data byte goes out.
  // The data byte alone carries its fault byte with it: a reset chip has forgotten the command
  // (and ACTIVE is 0, so nothing drives), and note() reports the reset afterwards.
  // While a miscount cannot be seen (COMER already set, p36: it may stay set until ENABLE falls),
  // a switch-off is read back: 2 more transfers, only in that state and only for a switch-off. A
  // switch-off counts if the channel may still be on (maybeOn(): an unverified switch-off too).
  const auto was = static_cast<uint32_t>(maybeOn());
  if (!comer_clear_ && ((was & ~static_cast<uint32_t>(mask)) != 0U)) {
    verify = Verify::ReadBack;
  }
  const bool fast = onch_ready_;
  onch_ready_ = false;
  detail::Reply reply{};
  Error e = note(reply, fast ? bus_.Data8(mask, reply)
                             : bus_.Write8(reg::kStatus, mask, reply, initialized_, false));
  if (fast && (e == Error::Ok) && (reply.status == 0xFFU)) {
    // No fault byte came back (0xFF is every flag set with ACTIVE: no answer). A chip that lost
    // the command in a reset clocks nothing out; send the command again, so its fault byte shows
    // whether the chip is still configured. Writing the same ONCH twice changes nothing.
    e = note(reply, bus_.Write8(reg::kStatus, mask, reply, initialized_, false));
  }
  if (e == Error::DeviceReset) {
    return fail<void>(e, "max22200: the chip was reset; start it again");
  }
  if (e != Error::Ok) {
    uncertain_ =
        static_cast<uint8_t>(static_cast<uint32_t>(uncertain_) |
                             (static_cast<uint32_t>(mask) ^ static_cast<uint32_t>(on_mask_)));
    return fail<void>(e, "max22200: ONCH write failed");
  }
  // Bits that may have been on and are asked off now. A fault byte with COMER clear will vouch for
  // this write later; with COMER set (seen first in this very call, so no read-back was chosen)
  // nothing will, and those bits stay in doubt.
  const auto maybe_on = static_cast<uint8_t>(was & ~static_cast<uint32_t>(mask));
  // The doubt this write settles moves into the unverified set with what it changes: if the chip
  // miscounted it, the COMER that follows restores the doubt instead of losing it (p34, p36).
  // (After note(): bits a clean fault byte of this access just vouched for are not added again.)
  wrote(reg::kStatus,
        static_cast<uint8_t>((static_cast<uint32_t>(mask) ^ static_cast<uint32_t>(on_mask_)) |
                             (static_cast<uint32_t>(uncertain_) & ~static_cast<uint32_t>(mask))));
  on_mask_ = mask;
  uncertain_ = 0;
  if (verify == Verify::None) {
    if (!comer_clear_) {
      uncertain_ = maybe_on;
    }
    onch_ready_ = (static_cast<uint32_t>(reply.status) & reg::kComer) == 0U;
    return Result<void>::Success();
  }
  return verifyOnch(mask, maybe_on);
}

template <SpiPort Port>
Result<void> MAX22200<Port>::verifyOnch(uint8_t mask, uint8_t maybe_on) noexcept {
  uint8_t read = mask;
  const auto r = check8(reg::kStatus, mask, read);
  if (r.error == Error::DeviceReset) {
    return r;
  }
  if (!r && (r.error != Error::WriteMismatch) && (r.error != Error::CommunicationError)) {
    // The read-back itself failed: nothing proves the write landed. With COMER set nothing ever
    // will, so the channels asked off stay in doubt (they count as on and go out again when asked).
    if (!comer_clear_) {
      uncertain_ = maybe_on;
    }
    return r;
  }
  // A mismatch: take the chip's value, and trust neither value for the bits that differ (the read
  // may be the broken part) in the ChannelsActive check.
  on_mask_ = read;
  uncertain_ = static_cast<uint8_t>(static_cast<uint32_t>(read) ^ static_cast<uint32_t>(mask));
  if (r) {
    unverified_onch_ = 0; // ONCH read back as written: every switch is proven, a miscount included
  }
  return r;
}

template <SpiPort Port>
Result<uint8_t> MAX22200<Port>::ReadChannelsOn() noexcept {
  if (!initialized_) {
    return Result<uint8_t>::Failure(Error::NotInitialized);
  }
  auto r = read8(reg::kStatus);
  if (!r) {
    return fail<uint8_t>(r.error, "max22200: ONCH read failed");
  }
  // A channel found on that the copy says is off counts as on; the read never clears doubt (it may
  // come through a broken SDO): only a write that went out does.
  uncertain_ =
      static_cast<uint8_t>(static_cast<uint32_t>(uncertain_) |
                           (static_cast<uint32_t>(r.value) & ~static_cast<uint32_t>(on_mask_)));
  return report(r);
}

// -------------------------------------------------------------------------------------------------
// Channel and chip settings
// -------------------------------------------------------------------------------------------------

template <SpiPort Port>
Result<void> MAX22200<Port>::ConfigureChannel(uint8_t channel, const ChannelConfig& config,
                                              Verify verify) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  // FREQM a failed STATUS write left unknown: the channel must be valid at either clock.
  const MasterClock other =
      (config_.clock == MasterClock::Khz80) ? MasterClock::Khz100 : MasterClock::Khz80;
  if ((channel >= kChannels) || !isVerify(verify) ||
      (ValidateChannel(config, config_.clock) != Error::Ok) ||
      (clock_uncertain_ && (ValidateChannel(config, other) != Error::Ok)) ||
      !ValidLoadDiagnostics(config, config_.loaded_channels, channel)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  Config next = config_;
  next.channels[channel] = config;
  // A channel that may hold either mode is written only with every channel off, even with the
  // copy's own value: only then does the chip take the mode bits (5c) and the doubt end.
  const bool unsure =
      ((static_cast<uint32_t>(cfg_uncertain_) & detail::ChannelBit(channel)) != 0U) &&
      ((static_cast<uint32_t>(on_mask_) | static_cast<uint32_t>(uncertain_)) != 0U);
  if (unsure || modeChangeBlocked(next, 0U)) {
    return Result<void>::Failure(Error::ChannelsActive);
  }
  // Degraded (COMER set): a miscount cannot show, only a read-back proves the write.
  return report(writeChannel(channel, config, comer_clear_ ? verify : Verify::ReadBack));
}

template <SpiPort Port>
Result<void> MAX22200<Port>::writeChannel(uint8_t channel, const ChannelConfig& config,
                                          Verify verify) noexcept {
  using detail::operator""_u32;
  const uint8_t bank = reg::CfgChannel(channel);
  const uint32_t value = EncodeChannel(config);
  const auto bit = static_cast<uint32_t>(detail::ChannelBit(channel));
  const auto w = put32(bank, value);
  if (!w) {
    cfg_uncertain_ =
        static_cast<uint8_t>(static_cast<uint32_t>(cfg_uncertain_) | bit); // either may be in force
    return fail<void>(w.error, "max22200: CFG_CH write failed");
  }
  config_.channels[channel] = config; // sent: the chip holds it unless the read-back says otherwise
  cfg_uncertain_ = static_cast<uint8_t>(static_cast<uint32_t>(cfg_uncertain_) & ~bit);
  if (verify == Verify::None) {
    return Result<void>::Success();
  }
  uint32_t read = value;
  const auto r = checkWord(bank, value, 0xFFFF'FFFF_u32, w.value.echo, read);
  config_.channels[channel] = DecodeChannel(read);
  if (!r) {
    cfg_uncertain_ = static_cast<uint8_t>(static_cast<uint32_t>(cfg_uncertain_) |
                                          bit); // the read may be wrong too
  }
  return r;
}

template <SpiPort Port>
Result<void> MAX22200<Port>::SetHoldCode(uint8_t channel, uint8_t code, Verify verify) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  // A channel whose last CFG_CH write failed or read back different may run either drive mode:
  // its HOLD code must then be valid in both (0 to kMaxVdrCode).
  if (channel >= kChannels) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  const bool unsure = (static_cast<uint32_t>(cfg_uncertain_) &
                       static_cast<uint32_t>(detail::ChannelBit(channel))) != 0U;
  if (!isVerify(verify) ||
      !ValidCode(code, unsure ? DriveMode::Vdr : config_.channels[channel].drive)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  // Programming Procedure 5b: CFG_CHn[31:24] is HFS and HOLD, writable while the channel holds.
  const auto byte = static_cast<uint8_t>(
      (config_.channels[channel].half_scale ? static_cast<uint32_t>(reg::kHfsByte) : 0U) |
      static_cast<uint32_t>(code));
  const uint8_t bank = reg::CfgChannel(channel);
  onch_ready_ = false;
  detail::Reply reply{};
  const Error e = note(reply, bus_.Write8(bank, byte, reply, initialized_, stopOnComer()));
  if (e != Error::Ok) {
    failedWrite(bank, e);
    return fail<void>(e, "max22200: HOLD write failed");
  }
  wrote(bank, 0U);
  config_.channels[channel].hold = code;
  if (verify == Verify::None) {
    return report(Result<void>::Success());
  }
  uint8_t read = byte;
  const auto r = check8(bank, byte, read);
  config_.channels[channel].hold =
      static_cast<uint8_t>(static_cast<uint32_t>(read) & reg::kCodeMask);
  config_.channels[channel].half_scale =
      (static_cast<uint32_t>(read) & static_cast<uint32_t>(reg::kHfsByte)) != 0U;
  return report(r);
}

template <SpiPort Port>
Result<void> MAX22200<Port>::SetPairMode(uint8_t pair, PairMode mode, Verify verify) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if ((pair >= kPairs) || !detail::IsEnumerator(mode) || !isVerify(verify)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  Config next = config_;
  next.pairs[pair] = mode;
  if (modeChangeBlocked(next, 0U)) {
    return Result<void>::Failure(Error::ChannelsActive);
  }
  return report(writeStatus(next, verify));
}

template <SpiPort Port>
Result<void> MAX22200<Port>::SetFaultMask(const FaultMask& mask, Verify verify) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (!isVerify(verify)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  Config next = config_;
  next.fault_mask = mask;
  return report(writeStatus(next, verify));
}

template <SpiPort Port>
Result<void> MAX22200<Port>::writeStatus(const Config& next, Verify verify) noexcept {
  const uint32_t value = EncodeStatus(next, on_mask_, true);
  uint32_t changed = 0; // pairs whose CMxy this write changes
  uint32_t off = 0;     // pairs with both channels off: the chip takes CMxy only then (p37)
  const uint32_t on = static_cast<uint32_t>(on_mask_) | static_cast<uint32_t>(uncertain_);
  for (uint8_t p = 0; p < kPairs; ++p) {
    changed |= (next.pairs[p] != config_.pairs[p]) ? (uint32_t{1} << p) : 0U;
    off |= ((on & static_cast<uint32_t>(detail::PairBits(p))) == 0U) ? (uint32_t{1} << p) : 0U;
  }
  const bool clock_changed = next.clock != config_.clock;
  const auto w = put32(reg::kStatus, value);
  if (!w) {
    // The reply failed, not necessarily the write: the chip may hold either value. The copy keeps
    // the old one for the ONCH bits it writes next; a pair mode or FREQM that differs is unknown.
    if (w.error != Error::DeviceReset) {
      pair_uncertain_ = static_cast<uint8_t>(static_cast<uint32_t>(pair_uncertain_) | changed);
      clock_uncertain_ = clock_uncertain_ || clock_changed;
    }
    return fail<void>(w.error, "max22200: STATUS write failed");
  }
  config_.clock = next.clock;
  config_.fault_mask = next.fault_mask;
  config_.pairs = next.pairs;
  settleStatus(changed, off, clock_changed, verify == Verify::ReadBack);
  if (verify == Verify::ReadBack) {
    uint32_t read = value;
    const auto r = checkWord(reg::kStatus, value, reg::kWritableMask, w.value.echo, read);
    if (r.error != Error::DeviceReset) {
      adoptStatus(read);
    }
    if (r.ok() || (r.error == Error::WriteMismatch) || (r.error == Error::CommunicationError)) {
      pair_uncertain_ = 0; // read from the chip: the copy now holds what the chip holds
      clock_uncertain_ = false;
    }
    return r;
  }
  return Result<void>::Success();
}

template <SpiPort Port>
void MAX22200<Port>::settleStatus(uint32_t changed, uint32_t off, bool clock_changed,
                                  bool read_back) noexcept {
  // A STATUS write went out with ONCH as the driver holds it, CMxy and FREQM: it settles their
  // doubt (CMxy only for the pairs that are off). Until a clean fault byte vouches for it, that
  // doubt moves into the unverified sets, so a COMER (a miscount of this very write) restores it
  // (p34, p36). With COMER already set no fault byte will vouch for it: the doubt stays, and
  // without a read-back the pair modes and FREQM it changes are in doubt too.
  if (!comer_clear_) {
    if (!read_back) {
      pair_uncertain_ = static_cast<uint8_t>(static_cast<uint32_t>(pair_uncertain_) | changed);
      clock_uncertain_ = clock_uncertain_ || clock_changed;
    }
    return;
  }
  unverified_onch_ = static_cast<uint8_t>(static_cast<uint32_t>(unverified_onch_) |
                                          static_cast<uint32_t>(uncertain_));
  unverified_pairs_ =
      static_cast<uint8_t>(static_cast<uint32_t>(unverified_pairs_) |
                           (static_cast<uint32_t>(pair_uncertain_) & off) | changed);
  unverified_clock_ = unverified_clock_ || clock_uncertain_ || clock_changed;
  uncertain_ = 0;
  pair_uncertain_ = static_cast<uint8_t>(static_cast<uint32_t>(pair_uncertain_) & ~off);
  clock_uncertain_ = false;
}

template <SpiPort Port>
Result<void> MAX22200<Port>::SetDpm(const DpmConfig& dpm, Verify verify) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (!isVerify(verify) || (dpm.start > reg::kDpmIstartMask) ||
      (dpm.debounce > reg::kDpmTdebMask) || (dpm.threshold > reg::kDpmIpthMask)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  const uint32_t value = EncodeDpm(dpm);
  const auto w = put32(reg::kCfgDpm, value);
  if (!w) {
    return fail<void>(w.error, "max22200: CFG_DPM write failed");
  }
  config_.dpm = dpm;
  if (verify == Verify::ReadBack) {
    uint32_t read = value;
    const auto r = checkWord(reg::kCfgDpm, value, reg::kDpmUsedMask, w.value.echo, read);
    config_.dpm = DecodeDpm(read);
    return report(r);
  }
  return report(Result<void>::Success());
}

template <SpiPort Port>
Result<ChannelConfig> MAX22200<Port>::ReadChannelConfig(uint8_t channel) noexcept {
  if (!initialized_) {
    return Result<ChannelConfig>::Failure(Error::NotInitialized);
  }
  if (channel >= kChannels) {
    return Result<ChannelConfig>::Failure(Error::InvalidArgument);
  }
  const auto r = read32(reg::CfgChannel(channel));
  if (!r) {
    return fail<ChannelConfig>(r.error, "max22200: CFG_CH read failed");
  }
  return report(Result<ChannelConfig>::Success(DecodeChannel(r.value)));
}

// -------------------------------------------------------------------------------------------------
// Diagnostics
// -------------------------------------------------------------------------------------------------

template <SpiPort Port>
Result<Status> MAX22200<Port>::ReadStatus() noexcept {
  if (!initialized_) {
    return Result<Status>::Failure(Error::NotInitialized);
  }
  // A reset shows in the command byte's ACTIVE, or in the register's (read32 acts on both).
  const auto r = read32(reg::kStatus);
  if (!r) {
    return fail<Status>(r.error, "max22200: STATUS read failed");
  }
  return report(Result<Status>::Success(DecodeStatus(r.value)));
}

template <SpiPort Port>
Result<Faults> MAX22200<Port>::ReadFaults() noexcept {
  using detail::operator""_u32;
  if (!initialized_) {
    return Result<Faults>::Failure(Error::NotInitialized);
  }
  const auto r = read32(reg::kFault, 0_u32);
  if (!r) {
    return fail<Faults>(r.error, "max22200: FAULT read failed");
  }
  latchFaults(r.value, (config_.variant == Variant::Max22200) ? 0xFFFF'FFFF_u32 : 0_u32);
  return report(Result<Faults>::Success(DecodeFaults(r.value)));
}

template <SpiPort Port>
Result<Faults> MAX22200<Port>::ClearFaults(const Faults& which) noexcept {
  if (!initialized_) {
    return Result<Faults>::Failure(Error::NotInitialized);
  }
  if ((config_.variant == Variant::Max22200) && !(which == Faults::All())) {
    return Result<Faults>::Failure(Error::Unsupported);
  }
  // Figure 8: SDI carries a 1 at the position of each flag to clear. With kFaultClearLsbByteFirst
  // the register-aligned value goes out LSB byte first, as in a write, so the DPM byte leads and
  // OCP[4] is bit 4 of the fourth byte, where Figure 8 draws it (not verified on hardware).
  const uint32_t sdi = (static_cast<uint32_t>(which.overcurrent) << reg::kFaultOcpShift) |
                       (static_cast<uint32_t>(which.hit_not_reached) << reg::kFaultHhfShift) |
                       (static_cast<uint32_t>(which.open_load) << reg::kFaultOlfShift) |
                       (static_cast<uint32_t>(which.plunger) << reg::kFaultDpmShift);
  const auto r = read32(reg::kFault, reg::FaultClearSdi(sdi));
  if (!r) {
    return fail<Faults>(r.error, "max22200: FAULT clear failed");
  }
  latchFaults(r.value, (config_.variant == Variant::Max22200) ? 0xFFFFFFFFU : sdi);
  return report(Result<Faults>::Success(DecodeFaults(r.value)));
}

template <SpiPort Port>
Result<bool> MAX22200<Port>::ReadFaultPin() noexcept {
  if (!initialized_) {
    return Result<bool>::Failure(Error::NotInitialized);
  }
  if constexpr (detail::HasGpioRead<Port>) {
    GpioSignal signal = GpioSignal::INACTIVE;
    if (!bus_.GetPort().GpioRead(CtrlPin::Fault, signal)) {
      return fail<bool>(Error::Pin, "max22200: FAULT pin read failed");
    }
    return Result<bool>::Success(signal == GpioSignal::ACTIVE);
  } else {
    return Result<bool>::Failure(Error::Unsupported);
  }
}

template <SpiPort Port>
Result<uint32_t> MAX22200<Port>::ReadRegister(uint8_t bank) noexcept {
  using detail::operator""_u32;
  if (!initialized_) {
    return Result<uint32_t>::Failure(Error::NotInitialized);
  }
  if (bank > reg::kCfgDpm) {
    return Result<uint32_t>::Failure(Error::InvalidArgument); // Register Map: banks 0x00 to 0x0A
  }
  const auto r = read32(bank);
  if (r && (bank == reg::kFault)) {
    latchFaults(r.value, (config_.variant == Variant::Max22200) ? 0xFFFF'FFFF_u32 : 0_u32);
  }
  return report(r);
}

// -------------------------------------------------------------------------------------------------
// Private
// -------------------------------------------------------------------------------------------------

template <SpiPort Port>
void MAX22200<Port>::latchFaults(uint32_t fault, uint32_t cleared) noexcept {
  if (floatingReply(fault)) {
    // SDO floating high: no data, never latched as flags (KB-CPP-116). A FAULT of all ones with a
    // 0xFF fault byte could also be every flag of a chip with every STATUS flag set; the read may
    // have cleared them either way, so the latch records that they are unknown.
    latched_.lost = latched_.lost || (initialized_ && (cleared != 0U));
    return;
  }
  latched_.faults = latched_.faults.Or(DecodeFaults(fault & cleared));
}

template <SpiPort Port>
Result<detail::Reply> MAX22200<Port>::put32(uint8_t bank, uint32_t value) noexcept {
  onch_ready_ = false;
  detail::Reply reply{};
  const Error e = note(reply, bus_.Write32(bank, value, reply, initialized_, stopOnComer()));
  if (e != Error::Ok) {
    failedWrite(bank, e); // the data may have reached the chip before the reply failed
    return Result<detail::Reply>::Failure(e);
  }
  wrote(bank, 0U);
  return Result<detail::Reply>::Success(reply);
}

template <SpiPort Port>
Result<void> MAX22200<Port>::checkWord(uint8_t bank, uint32_t value, uint32_t mask, uint8_t echo,
                                       uint32_t& read) noexcept {
  // Verify::ReadBack: the write's echo (Figure 10), then the register itself. A difference with
  // COMER in the read's fault byte means the chip ignored the write for its clock count. `read`
  // gets the masked register value when the read succeeded.
  const auto r = read32(bank);
  if (!r) {
    return fail<void>(r.error, "max22200: read-back failed");
  }
  read = r.value & mask;
  if ((echo == reg::Command(bank, true, false)) && (read == value)) {
    return Result<void>::Success();
  }
  return fail<void>(((static_cast<uint32_t>(last_status_) & reg::kComer) != 0U)
                        ? Error::CommunicationError
                        : Error::WriteMismatch,
                    "max22200: register read back differs");
}

template <SpiPort Port>
Result<void> MAX22200<Port>::check8(uint8_t bank, uint8_t want, uint8_t& read) noexcept {
  const auto r = read8(bank);
  if (!r) {
    return fail<void>(r.error, "max22200: read-back failed");
  }
  read = r.value;
  if (read == want) {
    return Result<void>::Success();
  }
  return fail<void>(((static_cast<uint32_t>(last_status_) & reg::kComer) != 0U)
                        ? Error::CommunicationError
                        : Error::WriteMismatch,
                    "max22200: register read back differs");
}

template <SpiPort Port>
void MAX22200<Port>::adoptStatus(uint32_t value) noexcept {
  // After a STATUS read-back: the driver's copy becomes what the chip holds; ONCH bits that differ
  // from what was written stay untrusted for the ChannelsActive check.
  const auto read_on = static_cast<uint8_t>(value >> reg::kOnchShift);
  uncertain_ =
      static_cast<uint8_t>(static_cast<uint32_t>(uncertain_) |
                           (static_cast<uint32_t>(read_on) ^ static_cast<uint32_t>(on_mask_)));
  on_mask_ = read_on;
  FaultMask& m = config_.fault_mask;
  m.overtemperature = (value & reg::kMaskOvt) != 0U;
  m.overcurrent = (value & reg::kMaskOcp) != 0U;
  m.open_load = (value & reg::kMaskOlf) != 0U;
  m.hit_not_reached = (value & reg::kMaskHhf) != 0U;
  m.plunger = (value & reg::kMaskDpm) != 0U;
  m.communication = (value & reg::kMaskComf) != 0U;
  m.undervoltage = (value & reg::kMaskUvm) != 0U;
  config_.clock = ((value & reg::kFreqm) != 0U) ? MasterClock::Khz80 : MasterClock::Khz100;
  for (uint8_t p = 0; p < kPairs; ++p) {
    // CMxy = 3 is no mode (KB-CPP20-025): the copy keeps its mode; the compare already failed.
    if (const auto mode = DecodePairMode(value, p); mode) {
      config_.pairs[p] = mode.value;
    }
  }
}

template <SpiPort Port>
Result<uint32_t> MAX22200<Port>::read32(uint8_t bank, uint32_t sdi) noexcept {
  onch_ready_ = false;
  detail::Reply reply{};
  uint32_t value = 0;
  const Error e = note(reply, bus_.Read32(bank, sdi, value, reply, initialized_));
  // A read that clears flags: STATUS always (OVT, UVM), FAULT on the MAX22200 or with a clear mask.
  const bool clears =
      (bank == reg::kStatus) ||
      ((bank == reg::kFault) && ((config_.variant == Variant::Max22200) || (sdi != 0U)));
  if (e != Error::Ok) {
    // A failed transfer may still have clocked the read: its flags may be gone (KB-CPP-119). A
    // reset stopped the access before its data phase: nothing was read.
    latched_.lost = latched_.lost || (initialized_ && clears && (e != Error::DeviceReset));
    return Result<uint32_t>::Failure(e);
  }
  if ((value == 0xFFFFFFFFU) && probeReset()) {
    return Result<uint32_t>::Failure(Error::DeviceReset);
  }
  if ((bank == reg::kStatus) && !takeStatus(value)) {
    return Result<uint32_t>::Failure(Error::DeviceReset);
  }
  return Result<uint32_t>::Success(value);
}

template <SpiPort Port>
bool MAX22200<Port>::probeReset() noexcept {
  // Data of all ones carries no fault byte: it is what a chip clocks out (SDO floating) when a
  // power-on reset between the command byte and the data made it forget the command. One more
  // command byte (an 8-bit ONCH read, 2 transfers, no side effect) shows whether it was reset;
  // a floating SDO answers 0xFF, which is not a reset.
  if (!initialized_) {
    return false;
  }
  detail::Reply reply{};
  uint8_t ignored = 0;
  return note(reply, bus_.Read8(reg::kStatus, ignored, reply, true)) == Error::DeviceReset;
}

template <SpiPort Port>
bool MAX22200<Port>::takeStatus(uint32_t value) noexcept {
  if (floatingReply(value)) {
    latched_.lost = latched_.lost || initialized_; // OVT and UVM were cleared unseen
    return true;
  }
  // "Read Clears All": this read took OVT and UVM from the chip; keep them (KB-CPP-119).
  latched_.overtemperature = latched_.overtemperature || ((value & reg::kOvt) != 0U);
  latched_.undervoltage = latched_.undervoltage || (latch_uvm_ && ((value & reg::kUvm) != 0U));
  if (initialized_ && ((value & reg::kActive) == 0U)) {
    // ACTIVE is never written 0 while running: a power-on reset between the command byte and the
    // data ("Undervoltage Lockout (UVLO)", p22).
    onReset();
    return false;
  }
  return true;
}

template <SpiPort Port>
Result<uint8_t> MAX22200<Port>::read8(uint8_t bank) noexcept {
  onch_ready_ = false;
  detail::Reply reply{};
  uint8_t value = 0;
  const Error e = note(reply, bus_.Read8(bank, value, reply, initialized_));
  if (e != Error::Ok) {
    return Result<uint8_t>::Failure(e);
  }
  if ((value == 0xFFU) && probeReset()) {
    return Result<uint8_t>::Failure(Error::DeviceReset);
  }
  return Result<uint8_t>::Success(value);
}

template <SpiPort Port>
Error MAX22200<Port>::note(const detail::Reply& reply, Error e) noexcept {
  last_status_ = reply.status;
  latchStatusByte(reply.flags);
  const auto status = static_cast<uint32_t>(reply.status);
  if ((static_cast<uint32_t>(reply.flags) & reg::kComer) != 0U) {
    onch_ready_ = false;                       // a write was miscounted: resend the command
    comer_seen_ = comer_seen_ || comer_armed_; // start-up: only a 0 to 1 transition counts
  }
  if (!initialized_) {
    return e;
  }
  if ((e == Error::DeviceReset) || detail::ResetByte(reply.status)) {
    onReset();
    return Error::DeviceReset;
  }
  if ((static_cast<uint32_t>(reply.flags) & reg::kComer) != 0U) {
    if (comer_clear_) {
      // p34: a write since the last clean fault byte had the wrong clock count and was ignored.
      // The driver cannot tell which: every one of them is in doubt.
      comer_new_ = true;
      stale_banks_ |= unverified_banks_;
      uncertain_ = static_cast<uint8_t>(static_cast<uint32_t>(uncertain_) |
                                        static_cast<uint32_t>(unverified_onch_));
      cfg_uncertain_ = static_cast<uint8_t>(static_cast<uint32_t>(cfg_uncertain_) |
                                            ((unverified_banks_ >> 1U) & 0xFFU)); // CFG_CH0 at 1
      pair_uncertain_ = static_cast<uint8_t>(static_cast<uint32_t>(pair_uncertain_) |
                                             static_cast<uint32_t>(unverified_pairs_));
      clock_uncertain_ = clock_uncertain_ || unverified_clock_;
    }
    comer_clear_ = false;
    if (e == Error::CommunicationError) {
      comer_new_ = false; // the write stopped before its data: this call reports it
    }
  } else if (status != 0xFFU) {
    comer_clear_ = true; // every write before this byte was counted
    unverified_banks_ = 0;
    unverified_onch_ = 0;
    unverified_pairs_ = 0;
    unverified_clock_ = false;
  } else {
    // No fault byte came back: it vouches for nothing, and the writes stay in doubt.
  }
  return e;
}

template <SpiPort Port>
void MAX22200<Port>::latchStatusByte(uint8_t flags) noexcept {
  // Figure 7 and 12: every command byte (and an 8-bit write's data byte) clocks out STATUS[7:0].
  // Its summary flags cost nothing to keep, and the chip clears OLF on a switch-on (p21) and HHF
  // on a new cycle (p23) before any FAULT read could see them.
  const auto f = static_cast<uint32_t>(flags);
  latched_.overtemperature = latched_.overtemperature || ((f & reg::kOvt) != 0U);
  latched_.undervoltage = latched_.undervoltage || (latch_uvm_ && ((f & reg::kUvm) != 0U));
  latched_.overcurrent = latched_.overcurrent || ((f & reg::kOcp) != 0U);
  latched_.open_load = latched_.open_load || ((f & reg::kOlf) != 0U);
  latched_.hit_not_reached = latched_.hit_not_reached || ((f & reg::kHhf) != 0U);
  latched_.plunger = latched_.plunger || ((f & reg::kDpm) != 0U);
}

template <SpiPort Port>
void MAX22200<Port>::onReset() noexcept {
  // UVLO with a power-on reset: every register is back at its reset value, ACTIVE = 0 and every
  // CFG_CHn CDR on the low side (p22, p38-39): "requires reconfiguration of the device". Nothing
  // from the copy is written back: the application starts the chip again.
  initialized_ = false;
  on_mask_ = 0;
  uncertain_ = 0;
  cfg_uncertain_ = 0;
  onch_ready_ = false; // the reset also forgot the command in force
  comer_new_ = false;
  unverified_banks_ = 0;
  unverified_onch_ = 0;
  stale_banks_ = 0;
  pair_uncertain_ = 0;
  clock_uncertain_ = false;
  unverified_pairs_ = 0;
  unverified_clock_ = false;
  detail::Log(bus_.GetPort(), LogLevel::Error, "max22200: the chip was reset; start it again");
}

template <SpiPort Port>
void MAX22200<Port>::wrote(uint8_t bank, uint8_t onch_changed) noexcept {
  if (!initialized_) {
    return; // the start-up reads every register back
  }
  const uint32_t bit = uint32_t{1} << (static_cast<uint32_t>(bank) & 0x0FU);
  unverified_banks_ |= bit;
  unverified_onch_ = static_cast<uint8_t>(static_cast<uint32_t>(unverified_onch_) |
                                          static_cast<uint32_t>(onch_changed));
  stale_banks_ &= ~bit;
}

template <SpiPort Port>
template <typename T>
Result<T> MAX22200<Port>::report(Result<T> r) noexcept {
  // A COMER that went from 0 to 1 during the call fails it, once. A call that already fails with
  // CommunicationError (a read-back) has reported it; one that fails for another reason keeps it
  // for the next call.
  if (!comer_new_ || (!r.ok() && (r.error != Error::CommunicationError))) {
    return r;
  }
  comer_new_ = false;
  return r.ok() ? fail<T>(Error::CommunicationError, "max22200: a write was miscounted (COMER)")
                : r;
}

template <SpiPort Port>
template <typename T>
Result<T> MAX22200<Port>::fail(Error error, const char* message) noexcept {
  detail::Log(bus_.GetPort(), LogLevel::Error, message);
  return Result<T>::Failure(error);
}

} // namespace max22200

#endif // MAX22200_IPP
