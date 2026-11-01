#include "RtcDriver_PCF85063.h"
#include <Wire.h>

#define PCF85063_ADDR    0x51
#define REG_CONTROL_1    0x00
#define REG_SECONDS      0x04

static inline uint8_t bcd2dec(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

static inline uint8_t dec2bcd(uint8_t val) {
    return ((val / 10) << 4) | (val % 10);
}

bool RtcDriver_PCF85063::init() {
    Serial.println("[PCF85063] Probing PCF85063 RTC at 0x51...");

    Wire.beginTransmission(PCF85063_ADDR);
    if (Wire.endTransmission() == 0) {
        _isOnline = true;
        Serial.println("[PCF85063] RTC is online!");

        // Clear STOP bit in Control_1 if set
        Wire.beginTransmission(PCF85063_ADDR);
        Wire.write(REG_CONTROL_1);
        Wire.write(0x00);
        Wire.endTransmission();
    } else {
        Serial.println("[PCF85063] Probe failed");
        _isOnline = false;
    }
    return _isOnline;
}

bool RtcDriver_PCF85063::readTime(RtcTimeInfo& outTime) {
    if (!_isOnline) return false;

    Wire.beginTransmission(PCF85063_ADDR);
    Wire.write(REG_SECONDS);
    if (Wire.endTransmission(false) != 0) return false;

    if (Wire.requestFrom((uint8_t)PCF85063_ADDR, (size_t)7) != 7) return false;

    uint8_t buf[7];
    for (int i = 0; i < 7; i++) {
        buf[i] = Wire.read();
    }

    outTime.second  = bcd2dec(buf[0] & 0x7F);
    outTime.minute  = bcd2dec(buf[1] & 0x7F);
    outTime.hour    = bcd2dec(buf[2] & 0x3F);
    outTime.day     = bcd2dec(buf[3] & 0x3F);
    outTime.weekday = bcd2dec(buf[4] & 0x07);
    outTime.month   = bcd2dec(buf[5] & 0x1F);
    outTime.year    = 2000 + bcd2dec(buf[6]);

    return (outTime.year >= 2024 && outTime.month >= 1 && outTime.month <= 12 && outTime.day >= 1);
}

bool RtcDriver_PCF85063::writeTime(const RtcTimeInfo& inTime) {
    if (!_isOnline) return false;

    Wire.beginTransmission(PCF85063_ADDR);
    Wire.write(REG_SECONDS);
    Wire.write(dec2bcd(inTime.second) & 0x7F);
    Wire.write(dec2bcd(inTime.minute) & 0x7F);
    Wire.write(dec2bcd(inTime.hour)   & 0x3F);
    Wire.write(dec2bcd(inTime.day)    & 0x3F);
    Wire.write(dec2bcd(inTime.weekday)& 0x07);
    Wire.write(dec2bcd(inTime.month)  & 0x1F);
    Wire.write(dec2bcd((uint8_t)(inTime.year % 100)));

    if (Wire.endTransmission() == 0) {
        Serial.printf("[PCF85063] Time written: %04d-%02d-%02d %02d:%02d:%02d\n",
                      inTime.year, inTime.month, inTime.day,
                      inTime.hour, inTime.minute, inTime.second);
        return true;
    }
    return false;
}
