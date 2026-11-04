#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

static const char* TAG = "SDIO_WRAP";

extern esp_err_t __real_sdmmc_host_init_slot(int slot, const sdmmc_slot_config_t* slot_config);

esp_err_t __wrap_sdmmc_host_init_slot(int slot, const sdmmc_slot_config_t* slot_config) {
    if (slot == 1) { // SDIO slot for ESP32-C6 on M5Stack Tab5
        sdmmc_slot_config_t custom_cfg;
        if (slot_config) {
            custom_cfg = *slot_config;
        } else {
            sdmmc_slot_config_t def = SDMMC_SLOT_CONFIG_DEFAULT();
            custom_cfg = def;
        }
        // Force Tab5 ESP32-C6 SDIO Pin Mapping
        custom_cfg.clk   = (gpio_num_t)12;
        custom_cfg.cmd   = (gpio_num_t)13;
        custom_cfg.d0    = (gpio_num_t)11;
        custom_cfg.d1    = (gpio_num_t)10;
        custom_cfg.d2    = (gpio_num_t)9;
        custom_cfg.d3    = (gpio_num_t)8;
        custom_cfg.width = 4;
        custom_cfg.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
        ESP_LOGI(TAG, "Overriding SDMMC Slot 1 pins for Tab5: CLK=12, CMD=13, D0=11, D1=10, D2=9, D3=8");
        return __real_sdmmc_host_init_slot(slot, &custom_cfg);
    }
    return __real_sdmmc_host_init_slot(slot, slot_config);
}

extern esp_err_t __real_sdmmc_card_init(const sdmmc_host_t* config, sdmmc_card_t* card);

esp_err_t __wrap_sdmmc_card_init(const sdmmc_host_t* config, sdmmc_card_t* card) {
    if (config && config->slot == 1) {
        sdmmc_host_t custom_host = *config;
        custom_host.max_freq_khz = 20000; // 20 MHz stable on Tab5 GPIO matrix
        ESP_LOGI(TAG, "Overriding SDMMC Slot 1 max_freq_khz: %d -> 20000", config->max_freq_khz);
        return __real_sdmmc_card_init(&custom_host, card);
    }
    return __real_sdmmc_card_init(config, card);
}
