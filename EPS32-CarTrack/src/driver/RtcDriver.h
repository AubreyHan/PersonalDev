#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "../config.h"

// =============================================================================
// RtcDriver - Tab5 Onboard Epson RX8130CE (I2C 0x32) Hardware RTC Driver
// - Coordinates safely with M5Unified's M5.Rtc & M5.In_I2C (GPIO31/32 @ 400kHz)
// - Probes REG_SEC (0x10) instead of 0x00
// - Inspects & clears VLF (0x1D bit 1 = 0x02) supercap power-loss flag via W0C
// - Enforces strict one-hot weekday (1 << (wday % 7)) in REG_WEEK (0x13)
// - Preserves supercapacitor backup charging bits (CHGEN|INIEN = 0x30) in 0x1F
// =============================================================================

struct RtcDateTime {
    uint16_t year    = 2026; // Full 4-digit year (2000..2099)
    uint8_t  month   = 1;    // 1..12
    uint8_t  day     = 1;    // 1..31
    uint8_t  weekDay = 4;    // 0=Sun .. 6=Sat
    uint8_t  hour    = 0;    // 0..23 (UTC)
    uint8_t  minute  = 0;    // 0..59
    uint8_t  second  = 0;    // 0..59
};

class RtcDriver {
public:
    static RtcDriver& instance();

    RtcDriver() = default;

    // Probe RX8130CE at 0x32, ensure supercap backup charge (0x1F) is intact,
    // and sample initial VLF voltage-low status.
    bool begin();

    bool isOnline() const { return _online; }
    bool lastVlfState() const { return _vlfDetected; }

    // Read RX8130CE Flag Register (0x1D) bit 1 (VLF: Voltage Low Flag).
    // Returns true if oscillator/supercap lost backup voltage or I2C read failed.
    bool hasVoltageLowFlag();

    // Clear VLF (bit 1) in 0x1D using Write-0-to-Clear (writes 0xFD) without
    // disturbing other interrupt/status flags.
    bool clearVoltageLowFlag();

    // Read UTC calendar fields from RX8130CE.
    // Returns true ONLY when I2C succeeds, VLF == 0, and calendar fields are valid.
    // Even if VLF == 1, raw decoded fields are populated into outUtc for diagnostics.
    bool readUtc(RtcDateTime& outUtc);

    // Atomically write UTC calendar to RX8130CE:
    // STOP=1 (0x1E bit6) -> burst BCD + one-hot WEEK (0x13) -> STOP=0 -> clear VLF,
    // while strictly preserving 0x1F supercapacitor charging bits (CHGEN|INIEN).
    bool writeUtc(const RtcDateTime& utc);

    // Write Unix UTC epoch seconds directly to RX8130CE
    bool writeEpochUtc(uint32_t epochUtc);

    // Pure-integer civil calendar <-> Unix epoch helpers (timezone-independent)
    static bool     isValidDateTime(const RtcDateTime& dt);
    static uint8_t  computeWeekday(uint16_t year, uint8_t month, uint8_t day);
    static uint32_t toUnixEpoch(const RtcDateTime& utc);
    static void     fromUnixEpoch(uint32_t epochUtc, RtcDateTime& outUtc);

private:
    static uint8_t bcdToDec(uint8_t val);
    static uint8_t decToBcd(uint8_t val);
    bool           ensureSupercapBackupCharge();

    bool _online      = false;
    bool _vlfDetected = true;
};
