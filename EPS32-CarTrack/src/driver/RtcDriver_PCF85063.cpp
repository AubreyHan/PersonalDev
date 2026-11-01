#include "RtcDriver_PCF85063.h"
#include <driver/i2c.h>

#define I2C_PORT         I2C_NUM_0
#define PCF85063_ADDR    0x51
#define REG_CONTROL_1    0x00
#define REG_SECONDS      0x04

bool RtcDriver_PCF85063::init() {
    Serial.println("[PCF85063] Probing PCF85063 RTC at 0x51...");

    // Test probe
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (PCF85063_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK) {
        _isOnline = true;
        Serial.println("[PCF85063] RTC is online!");

        // Clear STOP bit in Control_1 if set
        uint8_t ctrl[2] = {REG_CONTROL_1, 0x00};
        cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (PCF85063_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write(cmd, ctrl, sizeof(ctrl), true);
        i2c_master_stop(cmd);
        i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
    } else {
        Serial.printf("[PCF85063] Probe failed (err=0x%x)\n", ret);
        _isOnline = false;
    }
    return _isOnline;
}

bool RtcDriver_PCF85063::readTime(RtcTimeInfo& outTime) {
    if (!_isOnline) return false;

    // Send register pointer 0x04 (Seconds)
    uint8_t reg = REG_SECONDS;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (PCF85063_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);

    // Repeated start to read 7 bytes (sec, min, hr, day, weekday, month, yr)
    uint8_t buf[7] = {0};
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (PCF85063_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, buf, 7, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) return false;

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

    uint8_t data[8];
    data[0] = REG_SECONDS;
    data[1] = dec2bcd(inTime.second) & 0x7F;
    data[2] = dec2bcd(inTime.minute) & 0x7F;
    data[3] = dec2bcd(inTime.hour)   & 0x3F;
    data[4] = dec2bcd(inTime.day)    & 0x3F;
    data[5] = dec2bcd(inTime.weekday)& 0x07;
    data[6] = dec2bcd(inTime.month)  & 0x1F;
    data[7] = dec2bcd((uint8_t)(inTime.year % 100));

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (PCF85063_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, sizeof(data), true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK) {
        Serial.printf("[PCF85063] Time written: %04d-%02d-%02d %02d:%02d:%02d\n",
                      inTime.year, inTime.month, inTime.day,
                      inTime.hour, inTime.minute, inTime.second);
        return true;
    }
    return false;
}
