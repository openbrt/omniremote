# OmniRemote firmware — main component (pure ESP-IDF).
idf_component_register(
    SRCS         "main.cpp" "msc_disk.c"
    INCLUDE_DIRS "."
    REQUIRES     nvs_flash IRremoteESP8266 esp_tinyusb
)
