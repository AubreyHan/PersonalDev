#include "RtcDriver.h"
#include <M5Unified.h>

static_assert(RtcRx8130Config::I2C_ADDR == 0x32,
              "RX8130CE I2C address on M5Stack Tab5 MUST be 0x32!");
static_assert(RtcRx8130Config::I2C_ADDR != SysI2cConfig::ADDR_ES8388_AUDIO_DO_NOT_TOUCH,
              "RTC address must never collide with ES8388 (0x10)!");
static_assert(RtcRx8130Config::I2C_ADDR != SysI2cConfig::ADDR_ES7210_MIC_DO_NOT_TOUCH,
              "RTC address must never collide with ES7210 (0x40)!");

namespace {
    // RX8130CE Control Register 1 (0x1F):
    // bit 5 (0x20) = INIEN (backup switchover enable)
    // bit 4 (0x10) = CHGEN (supercapacitor trickle charger enable)
    constexpr uint8_t RX8130_CTRL1_SUPERCAP_MASK = 0x30U;

    uint8_t daysInMonth(uint16_t year, uint8_t month) {
        static constexpr uint8_t kDays[12] = {
            31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
        };
        if (month < 1U || month > 12U) {
            return 0U;
        }
        if (month == 2U) {
            const bool leap = ((year % 4U == 0U) && (year % 100U != 0U)) || (year % 400U == 0U);
            return leap ? 29U : 28U;
        }
        return kDays[month - 1U];
    }
} // namespace

RtcDriver& RtcDriver::instance() {
    static RtcDriver s_instance;
    return s_instance;
}

uint8_t RtcDriver::bcdToDec(uint8_t val) {
    return static_cast<uint8_t>(((val >> 4) * 10U) + (val & 0x0FU));
}

uint8_t RtcDriver::decToBcd(uint8_t val) {
    return static_cast<uint8_t>(((val / 10U) << 4) | (val % 10U));
}

bool RtcDriver::ensureSupercapBackupCharge() {
    uint8_t ctrl1 = 0;
    if (!M5.In_I2C.readRegister(RtcRx8130Config::I2C_ADDR,
                                RtcRx8130Config::REG_CTRL1,
                                &ctrl1, 1,
                                SysI2cConfig::FREQ_HZ)) {
        return false;
    }
    if ((ctrl1 & RX8130_CTRL1_SUPERCAP_MASK) != RX8130_CTRL1_SUPERCAP_MASK) {
        const uint8_t updated = static_cast<uint8_t>(ctrl1 | RX8130_CTRL1_SUPERCAP_MASK);
        return M5.In_I2C.writeRegister8(RtcRx8130Config::I2C_ADDR,
                                       RtcRx8130Config::REG_CTRL1,
                                       updated,
                                       SysI2cConfig::FREQ_HZ);
    }
    return true;
}

bool RtcDriver::begin() {
    uint8_t secReg = 0;
    // Probe REG_SEC (0x10), never 0x00 (RX8130CE calendar map starts at 0x10)
    const bool ack = M5.In_I2C.readRegister(RtcRx8130Config::I2C_ADDR,
                                            RtcRx8130Config::REG_SEC,
                                            &secReg, 1,
                                            SysI2cConfig::FREQ_HZ);
    _online = ack;
    if (!_online) {
        _vlfDetected = true;
        return false;
    }

    ensureSupercapBackupCharge();
    _vlfDetected = hasVoltageLowFlag();
    return true;
}

bool RtcDriver::hasVoltageLowFlag() {
    uint8_t flagReg = 0xFFU;
    if (!M5.In_I2C.readRegister(RtcRx8130Config::I2C_ADDR,
                                RtcRx8130Config::REG_FLAG,
                                &flagReg, 1,
                                SysI2cConfig::FREQ_HZ)) {
        _online      = false;
        _vlfDetected = true;
        return true;
    }
    _online      = true;
    _vlfDetected = ((flagReg & RtcRx8130Config::FLAG_VLF_BIT) != 0U);
    return _vlfDetected;
}

bool RtcDriver::clearVoltageLowFlag() {
    // RX8130CE Flag Register (0x1D) uses Write-0-to-Clear semantics:
    // writing 1 preserves other status/alarm flags while 0 clears VLF (bit 1).
    const uint8_t clearMask = static_cast<uint8_t>(~RtcRx8130Config::FLAG_VLF_BIT);
    const bool ok = M5.In_I2C.writeRegister8(RtcRx8130Config::I2C_ADDR,
                                             RtcRx8130Config::REG_FLAG,
                                             clearMask,
                                             SysI2cConfig::FREQ_HZ);
    if (ok) {
        _vlfDetected = false;
    }
    return ok;
}

bool RtcDriver::readUtc(RtcDateTime& outUtc) {
    const bool vlf = hasVoltageLowFlag();
    if (!_online) {
        return false;
    }

    if (M5.Rtc.isEnabled()) {
        m5::rtc_datetime_t dt{};
        if (!M5.Rtc.getDateTime(&dt)) {
            return false;
        }
        outUtc.year    = static_cast<uint16_t>(dt.date.year);
        outUtc.month   = static_cast<uint8_t>(dt.date.month);
        outUtc.day     = static_cast<uint8_t>(dt.date.date);
        outUtc.hour    = static_cast<uint8_t>(dt.time.hours);
        outUtc.minute  = static_cast<uint8_t>(dt.time.minutes);
        outUtc.second  = static_cast<uint8_t>(dt.time.seconds);
        outUtc.weekDay = computeWeekday(outUtc.year, outUtc.month, outUtc.day);
    } else {
        uint8_t buf[7] = {};
        if (!M5.In_I2C.readRegister(RtcRx8130Config::I2C_ADDR,
                                    RtcRx8130Config::REG_SEC,
                                    buf, sizeof(buf),
                                    SysI2cConfig::FREQ_HZ)) {
            _online = false;
            return false;
        }
        outUtc.second  = bcdToDec(buf[0] & 0x7FU);
        outUtc.minute  = bcdToDec(buf[1] & 0x7FU);
        outUtc.hour    = bcdToDec(buf[2] & 0x3FU);
        outUtc.day     = bcdToDec(buf[4] & 0x3FU);
        outUtc.month   = bcdToDec(buf[5] & 0x1FU);
        outUtc.year    = static_cast<uint16_t>(bcdToDec(buf[6]) + 2000U);
        outUtc.weekDay = computeWeekday(outUtc.year, outUtc.month, outUtc.day);
    }

    if (vlf) {
        return false;
    }
    return isValidDateTime(outUtc) && (outUtc.year >= 2025U && outUtc.year <= 2099U);
}

bool RtcDriver::writeUtc(const RtcDateTime& utc) {
    if (!isValidDateTime(utc) || utc.year < 2000U || utc.year > 2099U) {
        return false;
    }

    const uint8_t wday = computeWeekday(utc.year, utc.month, utc.day);

    // Assert STOP bit (0x1E bit 6) to freeze carry chain during calendar update
    if (!M5.In_I2C.bitOn(RtcRx8130Config::I2C_ADDR,
                         RtcRx8130Config::REG_CTRL0,
                         RtcRx8130Config::CTRL0_STOP_BIT,
                         SysI2cConfig::FREQ_HZ)) {
        _online = false;
        return false;
    }

    bool writeOk = false;
    if (M5.Rtc.isEnabled()) {
        m5::rtc_datetime_t dt{};
        dt.date.year    = static_cast<int16_t>(utc.year);
        dt.date.month   = static_cast<int8_t>(utc.month);
        dt.date.date    = static_cast<int8_t>(utc.day);
        dt.date.weekDay = static_cast<int8_t>(wday); // M5.Rtc encodes (1u << (7 & wday)) into 0x13
        dt.time.hours   = static_cast<int8_t>(utc.hour);
        dt.time.minutes = static_cast<int8_t>(utc.minute);
        dt.time.seconds = static_cast<int8_t>(utc.second);
        M5.Rtc.setDateTime(&dt);
        writeOk = true;
    } else {
        const uint8_t oneHotWeek = static_cast<uint8_t>(1U << (wday % 7U)); // NEVER 0x00!
        const uint8_t raw[7] = {
            decToBcd(utc.second),
            decToBcd(utc.minute),
            decToBcd(utc.hour),
            oneHotWeek,
            decToBcd(utc.day),
            decToBcd(utc.month),
            decToBcd(static_cast<uint8_t>(utc.year - 2000U))
        };
        writeOk = M5.In_I2C.writeRegister(RtcRx8130Config::I2C_ADDR,
                                          RtcRx8130Config::REG_SEC,
                                          raw, sizeof(raw),
                                          SysI2cConfig::FREQ_HZ);
    }

    // Release STOP bit (0x1E bit 6 = 0) so oscillator resumes counting
    const bool startOk = M5.In_I2C.bitOff(RtcRx8130Config::I2C_ADDR,
                                          RtcRx8130Config::REG_CTRL0,
                                          RtcRx8130Config::CTRL0_STOP_BIT,
                                          SysI2cConfig::FREQ_HZ);

    // Clear VLF after authoritative calibration & verify 0x1F supercap backup charge bits remain set
    clearVoltageLowFlag();
    ensureSupercapBackupCharge();

    _online = writeOk && startOk;
    return _online;
}

bool RtcDriver::writeEpochUtc(uint32_t epochUtc) {
    RtcDateTime dt{};
    fromUnixEpoch(epochUtc, dt);
    return writeUtc(dt);
}

bool RtcDriver::isValidDateTime(const RtcDateTime& dt) {
    if (dt.year < 2000U || dt.year > 2099U) {
        return false;
    }
    const uint8_t maxDay = daysInMonth(dt.year, dt.month);
    if (maxDay == 0U || dt.day < 1U || dt.day > maxDay) {
        return false;
    }
    if (dt.hour > 23U || dt.minute > 59U || dt.second > 59U) {
        return false;
    }
    return true;
}

uint8_t RtcDriver::computeWeekday(uint16_t year, uint8_t month, uint8_t day) {
    // Sakamoto's algorithm: returns 0 = Sunday .. 6 = Saturday
    static constexpr uint8_t kTable[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (month < 1U || month > 12U) {
        return 0U;
    }
    uint16_t y = year;
    if (month < 3U) {
        y -= 1U;
    }
    return static_cast<uint8_t>((y + y / 4U - y / 100U + y / 400U + kTable[month - 1U] + day) % 7U);
}

uint32_t RtcDriver::toUnixEpoch(const RtcDateTime& utc) {
    if (!isValidDateTime(utc)) {
        return 0UL;
    }
    // Howard Hinnant's civil-to-days algorithm (days since 1970-01-01)
    int32_t y = static_cast<int32_t>(utc.year);
    const uint32_t m = utc.month;
    const uint32_t d = utc.day;
    y -= (m <= 2U) ? 1 : 0;
    const int32_t era = (y >= 0 ? y : y - 399) / 400;
    const uint32_t yoe = static_cast<uint32_t>(y - era * 400);
    const uint32_t doy = (153U * (m + (m > 2U ? -3U : 9U)) + 2U) / 5U + d - 1U;
    const uint32_t doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    const int32_t days = era * 146097 + static_cast<int32_t>(doe) - 719468;
    if (days < 0) {
        return 0UL;
    }
    return static_cast<uint32_t>(days) * 86400UL
         + static_cast<uint32_t>(utc.hour) * 3600UL
         + static_cast<uint32_t>(utc.minute) * 60UL
         + static_cast<uint32_t>(utc.second);
}

void RtcDriver::fromUnixEpoch(uint32_t epochUtc, RtcDateTime& outUtc) {
    const uint32_t secOfDay = epochUtc % 86400UL;
    const int32_t  z        = static_cast<int32_t>(epochUtc / 86400UL) + 719468;
    const int32_t  era      = (z >= 0 ? z : z - 146096) / 146097;
    const uint32_t doe      = static_cast<uint32_t>(z - era * 146097);
    const uint32_t yoe      = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    int32_t        y        = static_cast<int32_t>(yoe) + era * 400;
    const uint32_t doy      = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    const uint32_t mp       = (5U * doy + 2U) / 153U;
    const uint32_t d        = doy - (153U * mp + 2U) / 5U + 1U;
    const uint32_t m        = mp + (mp < 10U ? 3U : -9U);
    y += (m <= 2U) ? 1 : 0;

    outUtc.year    = static_cast<uint16_t>(y);
    outUtc.month   = static_cast<uint8_t>(m);
    outUtc.day     = static_cast<uint8_t>(d);
    outUtc.hour    = static_cast<uint8_t>(secOfDay / 3600UL);
    outUtc.minute  = static_cast<uint8_t>((secOfDay % 3600UL) / 60UL);
    outUtc.second  = static_cast<uint8_t>(secOfDay % 60UL);
    outUtc.weekDay = static_cast<uint8_t>((epochUtc / 86400UL + 4UL) % 7UL); // 1970-01-01 was Thu (4)
}
