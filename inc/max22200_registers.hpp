/**
 * @file max22200_registers.hpp
 * @brief MAX22200 register map: the Command Register, the 32-bit register banks, their fields,
 * reset values and the chip's timing.
 * @copyright Copyright (c) 2026 The driver-max22200 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Section, table and figure names refer to the datasheet, Analog Devices 19-100531 Rev 1 (3/25).
 *
 * Every access is two SPI transfers (Programming Information, "Command Register Description"):
 * first the 8-bit Command Register, written with CMD high, which selects a bank, the direction and
 * the length of what follows; then, with CMD low, the data: 32 bits, or only the most significant
 * byte when bit 0 of the command is set. While the command is clocked in, SDO carries STATUS[7:0]
 * (the Fault Flag Byte, Figure 7). Data written in 32-bit mode goes LSB byte first (Figure 10),
 * data read comes MSB byte first (Figure 11); an 8-bit access touches bits 31:24 (Figures 12, 13).
 */
#ifndef MAX22200_REGISTERS_HPP
#define MAX22200_REGISTERS_HPP

#include <cstddef>
#include <cstdint>

#include "max22200_detail.hpp"

/** @brief The MAX22200 register map. */
namespace max22200::reg {

using detail::operator""_u8;

/**
 * @defgroup max22200_registers Register map
 * @brief Banks, fields, reset values and timing.
 * @{
 */

// --- Register banks (A_BNK, Register Map) --------------------------------------------------------
inline constexpr uint8_t kStatus = 0x00; ///< STATUS: ONCH, fault masks, FREQM, CMxy, flags, ACTIVE.
inline constexpr uint8_t kCfgCh0 = 0x01; ///< CFG_CH0; CFG_CHn is at 0x01 + n, up to 0x08.
inline constexpr uint8_t kFault = 0x09;  ///< FAULT (read only): OCP, HHF, OLF, DPM per channel.
inline constexpr uint8_t kCfgDpm = 0x0A; ///< CFG_DPM: the plunger-movement detection settings.

/**
 * @brief The bank of a channel's configuration register.
 * @param channel 0 to 7.
 * @return 0x01 to 0x08.
 */
constexpr uint8_t CfgChannel(uint8_t channel) noexcept {
  return static_cast<uint8_t>(static_cast<uint32_t>(kCfgCh0) +
                              (static_cast<uint32_t>(channel) & 0x07U));
}

// --- Command Register (Table 9) ------------------------------------------------------------------
inline constexpr uint8_t kCmdWrite = 0x80;    ///< RB/W = 1: the next transfer writes.
inline constexpr uint8_t kCmdBankShift = 1;   ///< A_BNK is bits 4:1; bits 6:5 (RFU) stay 0.
inline constexpr uint8_t kCmdBankMask = 0x0F; ///< A_BNK width.
inline constexpr uint8_t kCmd8Bit = 0x01; ///< 8bit/n32bits = 1: the next transfer is bits 31:24.

/**
 * @brief The Command Register byte for one access.
 * @param bank The register bank (`kStatus`, `CfgChannel()`, `kFault`, `kCfgDpm`).
 * @param write true for a write, false for a read.
 * @param eight_bit true for the most significant byte only, false for all 32 bits.
 * @return The command byte; RFU bits 6:5 are 0.
 */
constexpr uint8_t Command(uint8_t bank, bool write, bool eight_bit) noexcept {
  return static_cast<uint8_t>((write ? static_cast<uint32_t>(kCmdWrite) : 0U) |
                              ((static_cast<uint32_t>(bank) & kCmdBankMask) << kCmdBankShift) |
                              (eight_bit ? static_cast<uint32_t>(kCmd8Bit) : 0U));
}

// --- STATUS (0x00), Table 10 and Register Details ------------------------------------------------
inline constexpr uint32_t kOnchShift = 24; ///< ONCH[7:0]: bit 24 + n switches channel n on.
inline constexpr uint32_t kMaskOvt = uint32_t{1} << 23U;  ///< M_OVT: OVT does not assert FAULT.
inline constexpr uint32_t kMaskOcp = uint32_t{1} << 22U;  ///< M_OCP.
inline constexpr uint32_t kMaskOlf = uint32_t{1} << 21U;  ///< M_OLF.
inline constexpr uint32_t kMaskHhf = uint32_t{1} << 20U;  ///< M_HHF.
inline constexpr uint32_t kMaskDpm = uint32_t{1} << 19U;  ///< M_DPM.
inline constexpr uint32_t kMaskComf = uint32_t{1} << 18U; ///< M_COMF; 1 at reset (COMER masked).
inline constexpr uint32_t kMaskUvm = uint32_t{1} << 17U;  ///< M_UVM.
inline constexpr uint32_t kFreqm = uint32_t{1}
                                   << 16U;          ///< FREQM: 0 = 100 kHz, 1 = 80 kHz oscillator.
inline constexpr uint32_t kPairModeShift = 8;       ///< CM10 at bits 9:8, CM32 11:10, CM54 13:12,
                                                    ///< CM76 15:14.
inline constexpr uint32_t kOvt = uint32_t{1} << 7U; ///< OVT flag; a STATUS read clears it.
inline constexpr uint32_t kOcp = uint32_t{1}
                                 << 6U; ///< OCP flag: some channel has an OCP bit in FAULT.
inline constexpr uint32_t kOlf = uint32_t{1}
                                 << 5U; ///< OLF flag: some channel has an OLF bit in FAULT.
inline constexpr uint32_t kHhf = uint32_t{1}
                                 << 4U; ///< HHF flag: some channel has an HHF bit in FAULT.
inline constexpr uint32_t kDpm = uint32_t{1}
                                 << 3U; ///< DPM flag: some channel has a DPM bit in FAULT.
inline constexpr uint32_t kComer = uint32_t{1}
                                   << 2U; ///< COMER: a write had the wrong number of clocks.
inline constexpr uint32_t kUvm = uint32_t{1} << 1U; ///< UVM: VM undervoltage; set at power-up, a
                                                    ///< STATUS read clears it.
inline constexpr uint32_t kActive = uint32_t{1}
                                    << 0U;    ///< ACTIVE: 0 = low-power, channels Hi-Z (reset).
inline constexpr uint32_t kFlagsMask = 0xFEU; ///< The seven read-only flags, bits 7:1.
inline constexpr uint32_t kWritableMask = 0xFFFFFF01U;     ///< Every bit a write sets.
inline constexpr uint32_t kStatusReset = kMaskComf | kUvm; ///< 0x00040002 after power-up.

/**
 * @brief Shift of a channel pair's CMxy field in STATUS.
 * @param pair 0 (channels 0 and 1) to 3 (channels 6 and 7).
 * @return 8, 10, 12 or 14.
 */
constexpr uint32_t PairModeShift(uint8_t pair) noexcept {
  return kPairModeShift + (2U * (static_cast<uint32_t>(pair) & 0x03U));
}

// --- CFG_CHn (0x01 to 0x08), Table 11 and Register Details ---------------------------------------
inline constexpr uint32_t kHfs = uint32_t{1}
                                 << 31U;   ///< HFS: half full-scale (KFS 7.5k instead of 15k).
inline constexpr uint32_t kHoldShift = 24; ///< HOLD[6:0] at bits 30:24.
inline constexpr uint32_t kTrgnspi = uint32_t{1}
                                     << 23U; ///< TRGnSPI: the TRIGA/TRIGB pin switches it.
inline constexpr uint32_t kHitShift = 16;    ///< HIT[6:0] at bits 22:16.
inline constexpr uint32_t kHitTimeShift = 8; ///< HIT_T[7:0] at bits 15:8.
inline constexpr uint32_t kVdrncdr = uint32_t{1}
                                     << 7U; ///< VDRnCDR: 1 = voltage drive, 0 = current drive.
inline constexpr uint32_t kHsnls = uint32_t{1} << 6U; ///< HSnLS: 1 = high side (VDR only).
inline constexpr uint32_t kFreqCfgShift = 4;          ///< FREQ_CFG[1:0] at bits 5:4.
inline constexpr uint32_t kSrc = uint32_t{1} << 3U;   ///< SRC: slew-rate controlled edges.
inline constexpr uint32_t kOlEn = uint32_t{1} << 2U;  ///< OL_EN: open-load detection.
inline constexpr uint32_t kDpmEn = uint32_t{1} << 1U; ///< DPM_EN: plunger-movement detection.
inline constexpr uint32_t kHhfEn = uint32_t{1} << 0U; ///< HHF_EN: HIT-current check.
inline constexpr uint32_t kCodeMask = 0x7FU;          ///< Width of HIT and HOLD.
inline constexpr uint8_t kHfsByte = 0x80;             ///< HFS in the 8-bit (bits 31:24) access.

// --- FAULT (0x09), Table 12 ----------------------------------------------------------------------
inline constexpr uint32_t kFaultOcpShift = 24; ///< OCP[7:0]; channel 0 is the LSB of each byte.
inline constexpr uint32_t kFaultHhfShift = 16; ///< HHF[7:0].
inline constexpr uint32_t kFaultOlfShift = 8;  ///< OLF[7:0].
inline constexpr uint32_t kFaultDpmShift = 0;  ///< DPM[7:0].

/// The SDI byte order of a selective FAULT clear (MAX22200A). true: LSB byte first, as a 32-bit
/// write (Figure 10) and as Figure 8 draws OCP[4] in the fourth byte, so the DPM byte leads. false:
/// MSB byte first (OCP byte first), the other reading. Not verified on hardware: the one place to
/// change if a bench check shows the other order.
inline constexpr bool kFaultClearLsbByteFirst = true;

/**
 * @brief The register-aligned clear mask as SDI must carry it, for `kFaultClearLsbByteFirst`.
 * @param mask Bit n of FAULT set to clear it.
 * @return The value to shift out LSB byte first.
 */
constexpr uint32_t FaultClearSdi(uint32_t mask) noexcept {
  if constexpr (kFaultClearLsbByteFirst) {
    return mask;
  } else {
    return ((mask & 0xFFU) << 24U) | ((mask & 0xFF00U) << 8U) | ((mask >> 8U) & 0xFF00U) |
           (mask >> 24U);
  }
}

// --- CFG_DPM (0x0A), "Detection of Plunger Movement Register Description" ------------------------
inline constexpr uint32_t kDpmIstartShift = 8;    ///< DPM_ISTART[6:0] at bits 14:8.
inline constexpr uint32_t kDpmIstartMask = 0x7FU; ///< DPM_ISTART width.
inline constexpr uint32_t kDpmTdebShift = 4;      ///< DPM_TDEB[3:0] at bits 7:4.
inline constexpr uint32_t kDpmTdebMask = 0x0FU;   ///< DPM_TDEB width.
inline constexpr uint32_t kDpmIpthShift = 0;      ///< DPM_IPTH[3:0] at bits 3:0.
inline constexpr uint32_t kDpmIpthMask = 0x0FU;   ///< DPM_IPTH width.
inline constexpr uint32_t kDpmUsedMask = 0x7FFFU; ///< Bits 31:15 "must be set at zero".

// --- Timing (Electrical Characteristics) ---------------------------------------------------------
inline constexpr uint32_t kEnableUs = 500;   ///< tEN max: ENABLE rising edge to SPI ready.
inline constexpr uint32_t kWakeUpUs = 2500;  ///< tWU max: ACTIVE = 1 to outputs active.
inline constexpr uint32_t kDisableUs = 2500; ///< tDIS max: ENABLE falling edge to outputs Hi-Z.
// The SPI bus requirements (mode, clock, CSB and CMD timing) are in max22200_spi_interface.hpp.

// --- Layout checks (handbook: register layouts asserted) -----------------------------------------
static_assert(Command(kStatus, false, false) == 0x00U, "Table 9: read STATUS, 32 bits");
static_assert(Command(kCfgDpm, true, false) == 0x94U, "Table 9: A_BNK at bits 4:1, RB/W at bit 7");
static_assert(Command(kStatus, true, true) == 0x81U, "Table 9: 8bit/n32bits at bit 0");
static_assert(CfgChannel(7_u8) == 0x08U, "Register Map: CFG_CH7 at 0x08");
static_assert(kStatusReset == 0x00040002U, "Register Details: M_COMF = 1, UVM = 1 at reset");
static_assert((kWritableMask & kFlagsMask) == 0U, "the flags are read only");
static_assert(PairModeShift(3_u8) == 14U, "Register Details: CM76 at bits 15:14");

/** @} */

} // namespace max22200::reg

#endif // MAX22200_REGISTERS_HPP
