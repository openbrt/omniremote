/* Arduino.h — minimal compatibility shim for IRremoteESP8266 under pure
 * ESP-IDF. Provides only the Arduino APIs the library actually touches:
 * digitalWrite / pinMode / delay / delayMicroseconds / millis / micros,
 * the HIGH/LOW/INPUT/OUTPUT macros, a no-op PROGMEM, and stub Serial +
 * String just enough to make the heavy debug-helper code compile (those
 * paths are not actually exercised in OmniRemote, but they're in headers
 * that IRsend.cpp transitively includes).
 *
 * Do NOT use this shim outside the IRremoteESP8266 component — it is a
 * targeted bridge, not a real Arduino compatibility layer.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- Pin-mode + level macros --------------------------------------------------
#define HIGH 1
#define LOW  0
#define INPUT        0x01
#define OUTPUT       0x02
#define INPUT_PULLUP 0x05

// ---- PROGMEM is a no-op on ESP32 (all flash data is memory-mapped). ----------
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PGM_P
#define PGM_P const char*
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr)  (*(const uint8_t*)(addr))
#define pgm_read_word(addr)  (*(const uint16_t*)(addr))
#define pgm_read_dword(addr) (*(const uint32_t*)(addr))
#endif
#ifndef F
#define F(x) x
#endif

// ---- GPIO -------------------------------------------------------------------
static inline void pinMode(uint8_t pin, uint8_t mode) {
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << pin;
    if (mode == OUTPUT)            io.mode = GPIO_MODE_OUTPUT;
    else if (mode == INPUT_PULLUP) {
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
    } else                          io.mode = GPIO_MODE_INPUT;
    gpio_config(&io);
}

static inline void digitalWrite(uint8_t pin, uint8_t v) {
    gpio_set_level((gpio_num_t)pin, v != 0);
}

static inline int digitalRead(uint8_t pin) {
    return gpio_get_level((gpio_num_t)pin);
}

// ---- Time -------------------------------------------------------------------
static inline uint32_t millis(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static inline uint32_t micros(void) {
    return (uint32_t)esp_timer_get_time();
}

static inline void delay(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static inline void delayMicroseconds(uint32_t us) {
    esp_rom_delay_us(us);
}

// ---- POSIX math typedefs not provided by ESP-IDF newlib ---------------------
typedef double double_t;
typedef float  float_t;

// ---- Misc -------------------------------------------------------------------
#define yield()  vTaskDelay(0)

#ifdef __cplusplus
}  // extern "C"
#endif

// Forward-declare String so SerialReal_ below can take it by reference.
// Full String class definition lives further down.
#ifdef __cplusplus
class String;

#include <stdarg.h>

// "Serial" routes into IDF's console (USB-Serial-JTAG by default on M5StickS3
// via sdkconfig). Just enough surface for OmniRemote's logging calls.
class SerialReal_ {
 public:
    void begin(unsigned long) {}
    void end() {}
    void flush() { fflush(stdout); }
    int printf(const char* fmt, ...) {
        va_list ap; va_start(ap, fmt);
        int n = vprintf(fmt, ap);
        va_end(ap);
        return n;
    }
    size_t print(const char* s)        { return s ? fputs(s, stdout) >= 0 ? strlen(s) : 0 : 0; }
    size_t print(char c)               { putchar(c); return 1; }
    size_t print(int n)                { return ::printf("%d",  n); }
    size_t print(unsigned int n)       { return ::printf("%u",  n); }
    size_t print(long n)               { return ::printf("%ld", n); }
    size_t print(unsigned long n)      { return ::printf("%lu", n); }
    inline size_t print(const String& s);    // defined below, after String
    size_t println()                   { putchar('\n'); return 1; }
    size_t println(const char* s)      { size_t n = print(s); putchar('\n'); return n + 1; }
    template <typename T> size_t println(T v) { size_t n = print(v); putchar('\n'); return n + 1; }
    inline size_t println(const String& s);  // defined below, after String
    size_t write(uint8_t b)            { putchar(b); return 1; }
    size_t write(const uint8_t* b, size_t n) { return fwrite(b, 1, n, stdout); }
};
static SerialReal_ Serial;
#endif

// ============================================================================
// Minimal String class — enough to make IRutils.h declarations compile.
// Actual OmniRemote code never instantiates these.
// ============================================================================
#ifdef __cplusplus
class String {
 public:
    String() {}
    String(const char* c)              : s_(c ? c : "") {}
    String(const std::string& o)       : s_(o) {}
    String(char c)                     : s_(1, c) {}
    String(int n)                      : s_(std::to_string(n)) {}
    String(unsigned int n)             : s_(std::to_string(n)) {}
    String(long n)                     : s_(std::to_string(n)) {}
    String(unsigned long n)            : s_(std::to_string(n)) {}
    String(long long n)                : s_(std::to_string(n)) {}
    String(unsigned long long n)       : s_(std::to_string(n)) {}
    String(float f)                    : s_(std::to_string(f)) {}
    String(double f)                   : s_(std::to_string(f)) {}
    String(int n, int /*base*/)        : s_(std::to_string(n)) {}
    String(unsigned int n, int)        : s_(std::to_string(n)) {}
    String(long n, int)                : s_(std::to_string(n)) {}
    String(unsigned long n, int)       : s_(std::to_string(n)) {}
    String(unsigned char n, int)       : s_(std::to_string((int)n)) {}

    String& operator+=(const String& o) { s_ += o.s_;    return *this; }
    String& operator+=(const char* o)   { s_ += (o?o:""); return *this; }
    String& operator+=(char c)          { s_ += c;       return *this; }
    String& operator+=(int n)           { s_ += std::to_string(n); return *this; }
    String& operator+=(unsigned n)      { s_ += std::to_string(n); return *this; }
    String& operator+=(long n)          { s_ += std::to_string(n); return *this; }
    String& operator+=(unsigned long n) { s_ += std::to_string(n); return *this; }

    String  operator+ (const String& o) const { String r(*this); r += o; return r; }
    String  operator+ (const char* o)   const { String r(*this); r += o; return r; }
    String  operator+ (char c)          const { String r(*this); r += c; return r; }

    bool operator==(const String& o) const { return s_ == o.s_; }
    bool operator!=(const String& o) const { return s_ != o.s_; }
    bool operator< (const String& o) const { return s_ <  o.s_; }
    bool equals(const String& o)     const { return s_ == o.s_; }
    bool equalsIgnoreCase(const String&) const { return false; }

    bool        startsWith(const String& o) const { return s_.rfind(o.s_, 0) == 0; }
    bool        endsWith(const String&)     const { return false; }
    const char* c_str()                     const { return s_.c_str(); }
    size_t      length()                    const { return s_.size(); }
    bool        isEmpty()                   const { return s_.empty(); }
    char        charAt(size_t i)            const { return i < s_.size() ? s_[i] : '\0'; }
    void        clear()                           { s_.clear(); }
    int         indexOf(char)               const { return -1; }
    int         indexOf(const String&)      const { return -1; }
    String      substring(size_t)           const { return *this; }
    String      substring(size_t, size_t)   const { return *this; }
    void        replace(const String&, const String&) {}
    void        replace(char, char)               {}
    void        trim()                            {}
    void        toUpperCase()                     {}
    void        toLowerCase()                     {}
    void        toCharArray(char* buf, size_t n)  const { strncpy(buf, s_.c_str(), n); }
    long        toInt()                     const { return atol(s_.c_str()); }
    float       toFloat()                   const { return atof(s_.c_str()); }
    bool        reserve(size_t)                   { return true; }
    bool        concat(const String& o)           { s_ += o.s_; return true; }
    bool        concat(const char* o)             { s_ += (o?o:""); return true; }
    char        operator[](size_t i)        const { return i < s_.size() ? s_[i] : '\0'; }

 private:
    std::string s_;
};

inline String operator+(const char* lhs, const String& rhs) {
    return String(lhs) + rhs;
}
inline String operator+(char lhs, const String& rhs) {
    return String(lhs) + rhs;
}

// SerialReal_ methods that depend on the now-complete String type.
inline size_t SerialReal_::print(const String& s)   { return print(s.c_str()); }
inline size_t SerialReal_::println(const String& s) { return println(s.c_str()); }
#endif

// ============================================================================
// IPAddress — only used by IR receive dump code that we don't link in.
// ============================================================================
#ifdef __cplusplus
class IPAddress {
 public:
    IPAddress() {}
    IPAddress(uint8_t, uint8_t, uint8_t, uint8_t) {}
    operator uint32_t() const { return 0; }
};
#endif
