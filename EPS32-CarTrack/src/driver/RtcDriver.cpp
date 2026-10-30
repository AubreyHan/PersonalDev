#include "driver/RtcDriver.h"
#include "driver/I2cBusManager.h"
#include <M5Unified.h>

bool RtcDriver::init() {
    if (M5.Rtc.isEnabled()) {
        _isOnline = true;
        return true;
    }
    _isOnline = I2cBusManager::instance().probeAddress(I2cAddrConfig::ADDR_RX8130CE_RTC);
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
            return true;
        }
    }

    // Fallback direct RX8130CE register read (0x10..0x16)
    uint8_t buf[7] = {0};
    if (!I2cBusManager::instance().readRegs(I2cAddrConfig::ADDR_RX8130CE_RTC, 0x10, buf, 7)) {
        return false;
    }
    outDt.second = bcd2dec(buf[0] & 0x7F);
    outDt.minute = bcd2dec(buf[1] & 0x7F);
    outDt.hour   = bcd2dec(buf[2] & 0x3F);
    outDt.day    = bcd2dec(buf[4] & 0x3F);
    outDt.month  = bcd2dec(buf[5] & 0x1F);
    outDt.year   = 2000 + bcd2dec(buf[6]);
    return (outDt.year >= 2024 && outDt.month >= 1 && outDt.month <= 12);
}

bool RtcDriver::writeTime(const RtcDateTime& dt) {
    if (M5.Rtc.isEnabled()) {
        m5::rtc_datetime_t m5dt;
        m5dt.date.year    = dt.year;
        m5dt.date.month   = dt.month;
        m5dt.date.date    = dt.day;
        m5dt.date.weekDay = 0;
        m5dt.time.hours   = dt.hour;
        m5dt.time.minutes = dt.minute;
        m5dt.time.seconds = dt.second;
        M5.Rtc.setDateTime(&m5dt);
        return true;
    }

    auto& bus = I2cBusManager::instance();
    bus.writeReg8(I2cAddrConfig::ADDR_RX8130CE_RTC, 0x10, dec2bcd(dt.second));
    bus.writeReg8(I2cAddrConfig::ADDR_RX8130CE_RTC, 0x11, dec2bcd(dt.minute));
    bus.writeReg8(I2cAddrConfig::ADDR_RX8130CE_RTC, 0x12, dec2bcd(dt.hour));
    bus.writeReg8(I2cAddrConfig::ADDR_RX8130CE_RTC, 0x14, dec2bcd(dt.day));
    bus.writeReg8(I2cAddrConfig::ADDR_RX8130CE_RTC, 0x15, dec2bcd(dt.month));
    bus.writeReg8(I2cAddrConfig::ADDR_RX8130CE_RTC, 0x16, dec2bcd((uint8_t)(dt.year % 100)));
    return true;
}
