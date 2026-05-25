# OmniRemote firmware — main component (pure ESP-IDF).
idf_component_register(
    SRCS         "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES     nvs_flash IRremoteESP8266
)
