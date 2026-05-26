/* Preferences.h — minimal compat shim for Arduino's Preferences (NVS wrapper)
 * implemented in terms of ESP-IDF's nvs_flash C API. Provides only the
 * surface OmniRemote actually uses: begin/end/isKey/getBytes/putBytes plus
 * getUChar/putUChar/getBool/putBool/clear.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_err.h"

class Preferences {
 public:
    Preferences() {}
    ~Preferences() { end(); }

    bool begin(const char* name, bool readonly = false) {
        end();
        esp_err_t r = nvs_open(name,
                               readonly ? NVS_READONLY : NVS_READWRITE,
                               &h_);
        if (r == ESP_ERR_NVS_NOT_FOUND && readonly) {
            // Read-only open of a never-written namespace: create it once.
            r = nvs_open(name, NVS_READWRITE, &h_);
        }
        open_ = (r == ESP_OK);
        return open_;
    }

    void end() {
        if (open_) {
            nvs_commit(h_);
            nvs_close(h_);
            open_ = false;
        }
    }

    bool isKey(const char* key) {
        if (!open_) return false;
        size_t len = 0;
        if (nvs_get_blob(h_, key, nullptr, &len) == ESP_OK) return true;
        uint8_t u8;
        if (nvs_get_u8(h_, key, &u8) == ESP_OK) return true;
        uint16_t u16;
        if (nvs_get_u16(h_, key, &u16) == ESP_OK) return true;
        uint32_t u32;
        if (nvs_get_u32(h_, key, &u32) == ESP_OK) return true;
        return false;
    }

    size_t getBytes(const char* key, void* buf, size_t maxlen) {
        if (!open_) return 0;
        size_t out = maxlen;
        if (nvs_get_blob(h_, key, buf, &out) == ESP_OK) return out;
        return 0;
    }

    size_t putBytes(const char* key, const void* buf, size_t len) {
        if (!open_) return 0;
        if (nvs_set_blob(h_, key, buf, len) == ESP_OK) return len;
        return 0;
    }

    uint8_t getUChar(const char* key, uint8_t def = 0) {
        if (!open_) return def;
        uint8_t v;
        return (nvs_get_u8(h_, key, &v) == ESP_OK) ? v : def;
    }
    size_t putUChar(const char* key, uint8_t v) {
        if (!open_) return 0;
        return (nvs_set_u8(h_, key, v) == ESP_OK) ? sizeof(v) : 0;
    }

    bool getBool(const char* key, bool def = false) {
        return getUChar(key, def ? 1 : 0) != 0;
    }
    size_t putBool(const char* key, bool v) {
        return putUChar(key, v ? 1 : 0);
    }

    bool clear() {
        if (!open_) return false;
        return nvs_erase_all(h_) == ESP_OK;
    }

 private:
    nvs_handle_t h_ = 0;
    bool         open_ = false;
};
