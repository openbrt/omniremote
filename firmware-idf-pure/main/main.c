/* OmniRemote firmware — pure ESP-IDF skeleton (v0.4-pure-idf-skel).
 *
 * This entry point just proves the toolchain works: prints a boot banner over
 * the M5StickS3's USB-Serial-JTAG and idles. The interesting code (display,
 * IR send, mic, buttons, MSC drive) lands in follow-up commits.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "nvs_flash.h"

static const char *TAG = "omni";

void app_main(void) {
    /* Init NVS — profile storage will live here once we port the app logic. */
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        r = nvs_flash_init();
    }
    ESP_ERROR_CHECK(r);

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_bytes = 0;
    esp_flash_get_size(NULL, &flash_bytes);

    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  OmniRemote firmware v0.4-pure-idf-skel");
    ESP_LOGI(TAG, "  one stick, every appliance");
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "ESP32-S3 cores=%d  flash=%lu MB  silicon rev=%d.%d",
             chip.cores, (unsigned long)(flash_bytes >> 20),
             chip.revision / 100, chip.revision % 100);
    ESP_LOGI(TAG, "IDF " IDF_VER);
    ESP_LOGI(TAG, "skeleton booted; idling (real app coming in next commits)");

    uint32_t tick = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "alive t=%lu s  free_heap=%lu B",
                 (unsigned long)(tick += 5),
                 (unsigned long)esp_get_free_heap_size());
    }
}
