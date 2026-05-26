# IRremoteESP8266 vendored as an ESP-IDF component.
#
# Upstream is Arduino-only. We bridge it to pure IDF by including a minimal
# "Arduino.h" shim (see arduino_shim/) that provides the small set of Arduino
# APIs IRremoteESP8266 actually touches (digitalWrite, delayMicroseconds,
# millis, micros, pinMode, HIGH/LOW/INPUT/OUTPUT, a stub String + Serial).

file(GLOB SRCS "src/*.cpp")
# IRrecv.cpp uses attachInterrupt/detachInterrupt + CHANGE — Arduino-only
# interrupt API. OmniRemote only sends IR (no learn mode), so exclude it.
list(REMOVE_ITEM SRCS "${CMAKE_CURRENT_LIST_DIR}/src/IRrecv.cpp")

idf_component_register(
    SRCS         ${SRCS}
    INCLUDE_DIRS "src" "arduino_shim"
    REQUIRES     driver esp_timer nvs_flash
)

# IRremoteESP8266 ifdefs on ARDUINO_ARCH_ESP32 to pick the ESP32 GPIO/RMT
# code paths; force that even though we no longer run under Arduino.
# Note: do NOT define plain ARDUINO=... — M5GFX's LGFXBase.hpp gates an
# `#include <Print.h>` on `#if defined(ARDUINO)`, and Print.h only exists
# inside the Arduino-ESP32 framework we are not pulling in.
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    ARDUINO_ARCH_ESP32=1
)
target_compile_options(${COMPONENT_LIB} PRIVATE
    # Prepend our shim Arduino.h to every IR translation unit. Many ir_*.h
    # headers gate `#include <Arduino.h>` on `#ifdef ARDUINO`; we deliberately
    # do not define ARDUINO globally (M5GFX gates Print.h on the same macro
    # and we don't ship Print.h), so we force-include the shim ourselves.
    -include "${CMAKE_CURRENT_LIST_DIR}/arduino_shim/Arduino.h"
    -Wno-missing-field-initializers
    -Wno-deprecated-declarations
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-format-truncation
)
