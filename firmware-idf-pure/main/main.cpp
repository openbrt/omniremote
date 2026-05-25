/* OmniRemote firmware — pure ESP-IDF + M5Unified (v0.4-pure-idf step 1).
 *
 * This step brings up M5Unified entirely under pure IDF:
 *   - LCD splash via M5.Display (M5GFX, IDF-native)
 *   - Buttons via M5.BtnA / M5.BtnB
 *   - NVS already initialised below
 *
 * IR send, mic, and TinyUSB composite (CDC + MSC) land in later commits.
 */

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "nvs_flash.h"
#include "M5Unified.h"
#include "IRremoteESP8266.h"
#include "IRsend.h"
#include "IRac.h"

static const char *TAG = "omni";

static void draw_splash() {
    auto &d = M5.Display;
    d.setRotation(0);
    d.fillScreen(BLACK);
    const int w = d.width();
    d.setTextDatum(middle_center);

    d.setTextColor(CYAN);
    d.setTextSize(2);
    d.drawString("OmniRemote", w / 2, d.height() / 2 - 28);

    d.setTextColor(WHITE);
    d.setTextSize(1);
    d.drawString("one stick,", w / 2, d.height() / 2);
    d.drawString("every appliance", w / 2, d.height() / 2 + 14);

    d.setTextColor(DARKGREY);
    d.drawString("v0.4-pure-idf", w / 2, d.height() / 2 + 40);
}

extern "C" void app_main(void) {
    /* NVS — profile storage lives here once the full app is ported. */
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        r = nvs_flash_init();
    }
    ESP_ERROR_CHECK(r);

    /* Bring up the StickS3 (LCD + buttons + PMIC + IMU + audio bus). */
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setBrightness(180);

    draw_splash();

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_bytes = 0;
    esp_flash_get_size(NULL, &flash_bytes);

    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  OmniRemote v0.4-pure-idf (M5Unified ok)");
    ESP_LOGI(TAG, "  one stick, every appliance");
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "ESP32-S3 cores=%d  flash=%lu MB  silicon rev=%d.%d",
             chip.cores, (unsigned long)(flash_bytes >> 20),
             chip.revision / 100, chip.revision % 100);
    ESP_LOGI(TAG, "IDF " IDF_VER);
    ESP_LOGI(TAG, "display=%dx%d  free_heap=%lu B",
             (int)M5.Display.width(), (int)M5.Display.height(),
             (unsigned long)esp_get_free_heap_size());

    uint32_t tick = 0;
    while (1) {
        M5.update();
        if (M5.BtnA.wasClicked()) ESP_LOGI(TAG, "BtnA clicked");
        if (M5.BtnA.wasHold())    ESP_LOGI(TAG, "BtnA held");
        if (M5.BtnB.wasClicked()) ESP_LOGI(TAG, "BtnB clicked");
        if (M5.BtnB.wasHold())    ESP_LOGI(TAG, "BtnB held");

        if (++tick % 250 == 0) {
            ESP_LOGI(TAG, "alive t=%lu s  free_heap=%lu B",
                     (unsigned long)(tick / 50),
                     (unsigned long)esp_get_free_heap_size());
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
