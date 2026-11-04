#include "driver/RtcDriver.h"
#include "driver/I2cBusManager.h"
#include <M5Unified.h>

bool RtcDriver::init() {
    if (M5.Rtc.isEnabled()) {
        _isOnline = true;
        return true;
    }
    // Probe register 0x10 (Seconds register) instead of 0x00 because RX8130CE starts at 0x10
    uint8_t dummy = 0;
    _isOnline = I2cBusManager::instance().readReg8(I2cAddrConfig::ADDR_RX8130CE_RTC, 0x10, &dummy);
    return _isOnline;
}

bool RtcDriver::readTime(RtcDateTime& outDt) {
    if (M5.Rtc.isEnabled()) {
        m5::rtc_datetime_t dt;
        if (M5.Rtc.getDateTime(&dt)) {
            outDt.year   = dt.date.year;
            outDt.month  = dt.date.month;
            outDt.day    = dt.date.date;
            outDt.hour   = dt.time.hours;
            outDt.minute = dt.time.minutes;
            outDt.second = dt.time.seconds;
            if (outDt.year >= 2024 && outDt.month >= 1 && outDt.month <= 12) {
                return true;
            }
        }
    }

    // Direct RX8130CE register read fallback (0x10..0x16)
    uint8_t buf[7] = {0};
    if (I2cBusManager::instance().readRegs(I2cAddrConfig::ADDR_RX8130CE_RTC, 0x10, buf, 7)) {
        outDt.second = bcd2dec(buf[0] & 0x7F);
        outDt.minute = bcd2dec(buf[1] & 0x7F);
        outDt.hour   = bcd2dec(buf[2] & 0x3F);
        // buf[3] is weekDay
        outDt.day    = bcd2dec(buf[4] & 0x3F);
        outDt.month  = bcd2dec(buf[5] & 0x1F);
        outDt.year   = 2000 + bcd2dec(buf[6]);
        if (outDt.year >= 2024 && outDt.month >= 1 && outDt.month <= 12) {
            // Repair weekday if it was 0 so M5.Rtc.getDateTime doesn't reject it
            if (buf[3] == 0) {
                I2cBusManager::instance().writeReg8(I2cAddrConfig::ADDR_RX8130CE_RTC, 0x13, 0x01);
            }
            return true;
        }
    }
    return false;
}

bool RtcDriver::writeTime(const RtcDateTime& dt) {
    if (dt.year < 2024) return false;

    // Calculate weekday (0=Sun, 1=Mon, ..., 6=Sat)
    struct tm t = {};
    t.tm_year = dt.year - 1900;
    t.tm_mon  = dt.month - 1;
    t.tm_mday = dt.day;
    t.tm_hour = dt.hour;
    t.tm_min  = dt.minute;
    t.tm_sec  = dt.second;
    mktime(&t);
    uint8_t wday = (uint8_t)(t.tm_wday % 7);

    if (M5.Rtc.isEnabled()) {
        m5::rtc_datetime_t m5dt;
        m5dt.date.year    = dt.year;
        m5dt.date.month   = dt.month;
        m5dt.date.date    = dt.day;
        m5dt.date.weekDay = wday;
        m5dt.time.hours   = dt.hour;
        m5dt.time.minutes = dt.minute;
        m5dt.time.seconds = dt.second;
        M5.Rtc.setDateTime(&m5dt);
    }

    // Direct register write to ensure register 0x13 (weekDay) is never 0
    auto& bus = I2cBusManager::instance();
    bus.writeReg8(I2cAddrConfig::ADDR_RX8130CE_RTC, 0x10, dec2bcd(dt.second));
    bus.writeReg8(I2cAddrConfig::ADDR_RX8130CE_RTC, 0x11, dec2bcd(dt.minute));
    bus.writeReg8(I2cAddrConfig::ADDR_RX8130CE_RTC, 0x12, dec2bcd(dt.hour));
    bus.writeReg8(I2cAddrConfig::ADDR_RX8130CE_RTC, 0x13, (uint8_t)(1u << wday)); // 1-hot bit required by RX8130
    bus.writeReg8(I2cAddrConfig::ADDR_RX8130CE_RTC, 0x14, dec2bcd(dt.day));
    bus.writeReg8(I2cAddrConfig::ADDR_RX8130CE_RTC, 0x15, dec2bcd(dt.month));
    bus.writeReg8(I2cAddrConfig::ADDR_RX8130CE_RTC, 0x16, dec2bcd((uint8_t)(dt.year % 100)));
    return true;
}
