// M5StickS3 universal AC remote.
//
// Hardware: M5Stack StickS3 (ESP32-S3-PICO-1, 8 MB flash + 8 MB OPI PSRAM).
// IR transmitter LED on GPIO 46, IR receiver on GPIO 42 (RX unused here).
//
// UX (mirrors the "auto-search universal AC remote" sold on Taobao for cheap):
//
//   1. Boot. If NVS has no saved protocol -> SEARCH mode.
//   2. SEARCH: every ~2 s, send "Power ON, Cool, 26 C, Fan Auto" for the next
//      protocol in BRANDS[]. Screen shows current brand and progress.
//      - BtnB short -> lock current protocol, save to NVS, jump to READY.
//      - BtnA short -> skip to next protocol immediately.
//   3. READY: large temperature, brand and mode below. Send on every press.
//      - BtnA short -> temp -1 (clamped). Send.
//      - BtnA hold  -> cycle mode (Cool/Heat/Dry/Fan/Auto). Send.
//      - BtnB short -> temp +1. Send.
//      - BtnB hold  -> toggle power. Send.
//   4. Both buttons held >3 s in either mode -> wipe NVS, restart in SEARCH.

#include <Arduino.h>      // shim — provides delay/millis/Serial/String
#include <M5Unified.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRac.h>
#include <IRtext.h>
#include <Preferences.h>  // shim — Preferences-compatible wrapper over nvs_flash
#include <math.h>

// ESP-IDF entry-point glue.
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_log.h"

// TinyUSB composite (CDC for the IDF console + MSC for the OmniRemote drive).
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "tusb_console.h"
#include "msc_disk.h"

// Software-triggered download mode (1200-baud touch on CDC, like Arduino).
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

// ----------------------------------------------------------------------------
// Brand-enable bitmap — persisted in NVS, settable via CDC "SET_ENABLED <hex>".
// Bit i corresponds to BRANDS[i]. Default = all-1s (every brand tried).
// ----------------------------------------------------------------------------
static constexpr size_t CFG_MASK_BYTES = (256 / 8);   // room for 256 brands
static uint8_t cfg_mask[CFG_MASK_BYTES];

static bool brand_enabled(uint8_t i) {
    return (cfg_mask[i >> 3] >> (i & 7)) & 1;
}

// -----------------------------------------------------------------------------
// Hardware
// -----------------------------------------------------------------------------
static constexpr uint16_t IR_TX_PIN = 46;
static constexpr uint16_t IR_RX_PIN = 42;

// -----------------------------------------------------------------------------
// Brand table - ordered roughly by China-market share so search lands fast.
// Each entry maps to an IRremoteESP8266 protocol enum + optional model variant.
// -----------------------------------------------------------------------------
struct BrandEntry {
    decode_type_t protocol;
    const char*   display_name;
    int16_t       model;   // -1 = library default
};

static const BrandEntry BRANDS[] = {
    // === China top brands: big brand first, old model variants first ===

    // Gree (all 3 known model variants — old YAW1F first, newer YBOFB next)
    {decode_type_t::GREE,                 "Gree YAW1F",   1},  // ~2010-15 default
    {decode_type_t::GREE,                 "Gree YBOFB",   2},  // ~2016+ YAPOF3
    {decode_type_t::GREE,                 "Gree YX1FSF",  3},  // Soleus niche

    // Midea (old narrow protocol first, newer 24-bit next).
    // Many post-2018 Midea models (KFR-*/WDAD3 etc.) actually speak COOLIX
    // under the hood — Midea is one of the brands that bundles COOLIX-based
    // IR boards. Try both protocols.
    {decode_type_t::MIDEA,                "Midea",        -1},
    {decode_type_t::MIDEA24,              "Midea-24",     -1},
    {decode_type_t::COOLIX,               "Midea/Coolix", -1},
    {decode_type_t::COOLIX48,             "Coolix-48",    -1},

    // Haier (old simpler first, newest variants last)
    {decode_type_t::HAIER_AC,             "Haier",        -1},
    {decode_type_t::HAIER_AC_YRW02,       "Haier YRW02",  -1},
    {decode_type_t::HAIER_AC176,          "Haier-176A",   1},  // V9014557_A
    {decode_type_t::HAIER_AC176,          "Haier-176B",   2},  // V9014557_B
    {decode_type_t::HAIER_AC160,          "Haier-160",    -1},

    // Hisense / Kelon family
    {decode_type_t::KELON,                "Kelon/Hisense",-1},
    {decode_type_t::KELON168,             "Kelon-168",    -1},

    // TCL (112-bit older more common, 96-bit variant)
    {decode_type_t::TCL112AC,             "TCL-112",      -1},
    {decode_type_t::TCL96AC,              "TCL-96",       -1},

    // Hitachi (old 224-bit first, newer wider variants after)
    {decode_type_t::HITACHI_AC,           "Hitachi",      -1},
    {decode_type_t::HITACHI_AC1,          "Hitachi-1A",   1},
    {decode_type_t::HITACHI_AC1,          "Hitachi-1B",   2},
    {decode_type_t::HITACHI_AC2,          "Hitachi-2",    -1},
    {decode_type_t::HITACHI_AC3,          "Hitachi-3",    -1},
    {decode_type_t::HITACHI_AC344,        "Hitachi-344",  -1},
    {decode_type_t::HITACHI_AC264,        "Hitachi-264",  -1},
    {decode_type_t::HITACHI_AC296,        "Hitachi-296",  -1},
    {decode_type_t::HITACHI_AC424,        "Hitachi-424",  -1},

    // === Japan high-end ===
    {decode_type_t::DAIKIN,               "Daikin",       -1},
    {decode_type_t::DAIKIN2,              "Daikin-2",     -1},
    {decode_type_t::DAIKIN64,             "Daikin-64",    -1},
    {decode_type_t::DAIKIN128,            "Daikin-128",   -1},
    {decode_type_t::DAIKIN152,            "Daikin-152",   -1},
    {decode_type_t::DAIKIN160,            "Daikin-160",   -1},
    {decode_type_t::DAIKIN176,            "Daikin-176",   -1},
    {decode_type_t::DAIKIN200,            "Daikin-200",   -1},
    {decode_type_t::DAIKIN216,            "Daikin-216",   -1},
    {decode_type_t::DAIKIN312,            "Daikin-312",   -1},
    {decode_type_t::MITSUBISHI_AC,        "Mitsubishi",   -1},
    {decode_type_t::MITSUBISHI112,        "Mits-112",     -1},
    {decode_type_t::MITSUBISHI136,        "Mits-136",     -1},
    {decode_type_t::MITSUBISHI_HEAVY_88,  "Mits.Hvy-88",  -1},
    {decode_type_t::MITSUBISHI_HEAVY_152, "Mits.Hvy-152", -1},
    {decode_type_t::PANASONIC_AC,         "Panasonic",    -1},
    {decode_type_t::PANASONIC_AC32,       "Panasonic-32", -1},
    {decode_type_t::TOSHIBA_AC,           "Toshiba",      -1},
    {decode_type_t::SHARP_AC,             "Sharp",        -1},
    {decode_type_t::FUJITSU_AC,           "Fujitsu ARRAH",1},  // AR-RAH2E
    {decode_type_t::FUJITSU_AC,           "Fujitsu ARDB", 2},  // AR-DB1
    {decode_type_t::FUJITSU_AC,           "Fujitsu ARREB",3},  // AR-REB1E
    {decode_type_t::SANYO_AC,             "Sanyo",        -1},
    {decode_type_t::SANYO_AC88,           "Sanyo-88",     -1},
    {decode_type_t::SANYO_AC152,          "Sanyo-152",    -1},
    {decode_type_t::CORONA_AC,            "Corona",       -1},

    // === Korean / global ===
    {decode_type_t::LG,                   "LG",           -1},
    {decode_type_t::LG2,                  "LG-2",         -1},
    {decode_type_t::SAMSUNG_AC,           "Samsung",      -1},
    {decode_type_t::CARRIER_AC,           "Carrier",      -1},
    {decode_type_t::CARRIER_AC40,         "Carrier-40",   -1},
    {decode_type_t::CARRIER_AC64,         "Carrier-64",   -1},
    {decode_type_t::CARRIER_AC128,        "Carrier-128",  -1},
    {decode_type_t::KELVINATOR,           "Kelvinator",   -1},
    {decode_type_t::WHIRLPOOL_AC,         "Whirlpool",    -1},
    {decode_type_t::ELECTRA_AC,           "Electra",      -1},
    {decode_type_t::TECO,                 "Teco",         -1},
    {decode_type_t::VESTEL_AC,            "Vestel",       -1},
    {decode_type_t::GOODWEATHER,          "Goodweather",  -1},
    {decode_type_t::AIRWELL,              "Airwell",      -1},
    {decode_type_t::AMCOR,                "Amcor",        -1},
    {decode_type_t::ARGO,                 "Argo",         -1},
    {decode_type_t::AIRTON,               "Airton",       -1},
    {decode_type_t::DELONGHI_AC,          "DeLonghi",     -1},
    {decode_type_t::TROTEC,               "Trotec",       -1},
    {decode_type_t::TROTEC_3550,          "Trotec-3550",  -1},
    {decode_type_t::NEOCLIMA,             "Neoclima",     -1},
    {decode_type_t::TECHNIBEL_AC,         "Technibel",    -1},
    {decode_type_t::VOLTAS,               "Voltas",       -1},
    {decode_type_t::GORENJE,              "Gorenje",      -1},
    {decode_type_t::MIRAGE,               "Mirage",       -1},
    {decode_type_t::BOSCH144,             "Bosch-144",    -1},
    {decode_type_t::ECOCLIM,              "Ecoclim",      -1},
    {decode_type_t::RHOSS,                "Rhoss",        -1},
    {decode_type_t::TRUMA,                "Truma",        -1},
    {decode_type_t::YORK,                 "York",         -1},
    {decode_type_t::BLUESTARHEAVY,        "BlueStarHvy",  -1},
    {decode_type_t::TRANSCOLD,            "TransCold",    -1},
};
static constexpr size_t BRAND_COUNT = sizeof(BRANDS) / sizeof(BRANDS[0]);

// (TV/Fan support deferred: when added, will fetch a code library over WiFi
//  with model-selection UI rather than hardcoding tables in firmware.)

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------
enum AppMode : uint8_t { MODE_HOME, MODE_SEARCH, MODE_HISTORY /* unused, kept for log clarity */ };

// --- Saved AC profiles --------------------------------------------------------
// User has 5-6 ACs across ~4 brands. Each press of BtnA-short broadcasts
// "POWER" to every saved brand; only the AC pointed at us reacts.
static constexpr size_t MAX_PROFILES = 6;
struct Profile {
    bool    used      = false;
    uint8_t brand_idx = 0;
};
static Profile profiles[MAX_PROFILES];

// --- Home-screen menu ---------------------------------------------------------
// A short = TOGGLE power (ON if off, OFF if on).
// A long-hold = SCAN to pair (works from any state) — so the menu doesn't
// need its own "+ Add AC" entry; long-press A IS the universal "add" gesture.
enum MenuItem : uint8_t {
    MENU_TEMP_UP,
    MENU_TEMP_DOWN,
    MENU_MODE,
    MENU_FAN,
    MENU_PROFILES,   // B-long here -> shows saved-brand list
    MENU_COUNT
};

struct State {
    AppMode  app_mode      = MODE_HOME;
    uint8_t  brand_idx     = 0;            // transient: which BRANDS[] entry to send next
    uint8_t  temp_c        = 26;           // user-facing setpoint (HOME); SEARCH overrides
    bool     power_on      = true;         // last broadcast intent
    stdAc::opmode_t op_mode = stdAc::opmode_t::kCool;
    stdAc::fanspeed_t fan  = stdAc::fanspeed_t::kAuto;
    uint8_t  menu_cursor   = 0;            // 0..MENU_COUNT-1
    uint32_t last_send_ms  = 0;
    uint32_t both_held_since_ms = 0;

    // SCAN mode: hold A to start, release to lock-or-abandon
    uint32_t scan_started_ms = 0;
    bool     scan_found      = false;
    uint8_t  scan_found_brand = 0;
};
static State st;

static IRac ir(IR_TX_PIN);
static Preferences prefs;

static constexpr uint32_t SEARCH_INTERVAL_MS = 800;   // faster cycle, hold-to-scan
static constexpr uint32_t HOLD_MS            = 600;
static constexpr uint32_t RESET_HOLD_MS      = 3000;
static constexpr uint32_t SCAN_MAX_MS        = 90000; // abandon after 90 s

// --- Microphone-based beep detection ---
// AC units almost universally emit a short "beep" on accepting a command.
// We listen briefly after each TX and trigger auto-pause if the peak audio
// level jumps well above the recent ambient baseline.
static constexpr uint32_t MIC_LISTEN_MS      = 700;
static constexpr uint32_t MIC_SAMPLE_RATE    = 16000;
static constexpr size_t   MIC_BUF_SAMPLES    = (MIC_SAMPLE_RATE * MIC_LISTEN_MS) / 1000;
static int16_t  mic_buf[MIC_BUF_SAMPLES];
static uint32_t mic_baseline_peak = 0;       // learned during warmup
static bool     mic_ready         = false;
static bool     mic_skip_first_ms = true;    // ignore first 80ms (RMT switching noise)
static uint8_t  mic_warmup        = 0;
static constexpr uint8_t  MIC_WARMUP_NEEDED  = 5;
static constexpr uint32_t MIC_ABSOLUTE_FLOOR = 6000;

// -----------------------------------------------------------------------------
// IR send
// -----------------------------------------------------------------------------
static void build_state(stdAc::state_t* s, const BrandEntry& brand) {
    bool in_search = (st.app_mode == MODE_SEARCH || st.app_mode == MODE_HISTORY);
    s->protocol = brand.protocol;
    s->model    = brand.model;
    // SEARCH/HISTORY always sends Power=ON so the AC will react audibly.
    // HOME broadcasts use st.power_on (set by A short / A long).
    s->power    = in_search ? true : st.power_on;
    s->mode     = st.op_mode;
    s->celsius  = true;
    // SEARCH: 18 C + max fan + turbo + beep -> easiest for user to notice.
    // READY:  user's chosen temp + auto fan + quiet.
    s->degrees  = in_search ? 18 : st.temp_c;
    s->sensorTemperature = kNoTempValue;
    s->iFeel    = false;
    s->fanspeed = in_search ? stdAc::fanspeed_t::kMax : stdAc::fanspeed_t::kAuto;
    s->swingv   = stdAc::swingv_t::kOff;
    s->swingh   = stdAc::swingh_t::kOff;
    // light=true asks the AC to light up its on-unit display panel (temperature
    // readout, mode icons). Many Gree/Midea models keep panel dark when light=0,
    // which feels broken to users even though the unit did accept the command.
    s->light    = true;
    s->beep     = in_search;
    s->econo    = false;
    s->filter   = false;
    s->turbo    = in_search;
    s->quiet    = false;
    s->sleep    = -1;
    s->clean    = false;
    s->clock    = -1;
    s->command  = stdAc::ac_command_t::kControlCommand;
}

// Returns true if a beep-like transient was detected during the listen window.
// Side effect: when no beep, slowly drifts mic_baseline_peak toward observed peak.
static bool listen_for_beep() {
    if (!mic_ready) return false;
    if (!M5.Mic.record(mic_buf, MIC_BUF_SAMPLES, MIC_SAMPLE_RATE)) {
        Serial.println("[mic] record() returned false");
        return false;
    }
    while (M5.Mic.isRecording()) {
        delay(2);
    }
    // Skip the first ~80 ms (RMT artefacts + tail of TX).
    const size_t skip = mic_skip_first_ms
        ? (MIC_SAMPLE_RATE * 80) / 1000
        : 0;
    int32_t peak = 0;
    int64_t sum_sq = 0;
    size_t  n = 0;
    for (size_t i = skip; i < MIC_BUF_SAMPLES; i++) {
        int32_t v  = mic_buf[i];
        int32_t av = v < 0 ? -v : v;
        if (av > peak) peak = av;
        sum_sq += (int64_t)v * v;
        n++;
    }
    uint32_t rms = n ? (uint32_t)sqrtf((float)(sum_sq / (int64_t)n)) : 0;

    bool warming = (mic_warmup < MIC_WARMUP_NEEDED);
    // During warmup, gate only by absolute floor (baseline isn't trusted yet);
    // after warmup, also require peak >> learned baseline. This way the first
    // brands in the cycle (Gree, Midea, Haier — the most likely matches) still
    // get auto-detection.
    uint32_t threshold = warming
        ? MIC_ABSOLUTE_FLOOR
        : (mic_baseline_peak * 4 > MIC_ABSOLUTE_FLOOR
              ? mic_baseline_peak * 4 : MIC_ABSOLUTE_FLOOR);
    bool beep = (peak > (int32_t)threshold);

    if (!beep) {
        if (warming) {
            // Track running max of warmup peaks as initial baseline.
            if ((uint32_t)peak > mic_baseline_peak) mic_baseline_peak = (uint32_t)peak;
            mic_warmup++;
        } else {
            // Steady-state: slow IIR toward observed peak.
            mic_baseline_peak = (mic_baseline_peak * 7 + (uint32_t)peak) / 8;
        }
    }
    Serial.printf("[mic] peak=%5ld rms=%5lu base=%5lu thr=%5lu warm=%u/%u beep=%d\n",
                  (long)peak, (unsigned long)rms,
                  (unsigned long)mic_baseline_peak,
                  (unsigned long)threshold,
                  (unsigned)mic_warmup, (unsigned)MIC_WARMUP_NEEDED,
                  beep ? 1 : 0);
    return beep;
}

static bool send_ir() {
    if (st.brand_idx >= BRAND_COUNT) return false;
    const auto& brand = BRANDS[st.brand_idx];
    if (!IRac::isProtocolSupported(brand.protocol)) {
        Serial.printf("[TX] skip %s (unsupported by IRac)\n", brand.display_name);
        st.last_send_ms = millis();
        return false;
    }
    stdAc::state_t s;
    build_state(&s, brand);
    uint32_t t0 = millis();
    bool ok = ir.sendAc(s, nullptr);
    Serial.printf("[TX] %-14s pow=%d temp=%uC mode=%d -> sent=%d (%lu ms)\n",
                  brand.display_name, (int)s.power, (unsigned)s.degrees,
                  (int)s.mode, ok ? 1 : 0, (unsigned long)(millis() - t0));
    st.last_send_ms = millis();
    return ok;
}


// -----------------------------------------------------------------------------
// NVS persistence + profile helpers
// -----------------------------------------------------------------------------
static uint8_t profile_count() {
    uint8_t n = 0;
    for (auto& p : profiles) if (p.used) n++;
    return n;
}

static int8_t profile_index_of(uint8_t brand_idx) {
    for (uint8_t i = 0; i < MAX_PROFILES; i++) {
        if (profiles[i].used && profiles[i].brand_idx == brand_idx) return i;
    }
    return -1;
}

static int8_t next_empty_profile_slot() {
    for (uint8_t i = 0; i < MAX_PROFILES; i++) {
        if (!profiles[i].used) return i;
    }
    return -1;
}

// Returns slot index if added, -1 if duplicate, -2 if full.
static int8_t add_profile(uint8_t brand_idx) {
    if (profile_index_of(brand_idx) >= 0) return -1;
    int8_t slot = next_empty_profile_slot();
    if (slot < 0) return -2;
    profiles[slot].used      = true;
    profiles[slot].brand_idx = brand_idx;
    return slot;
}

static void nvs_load() {
    prefs.begin("acremote", true);
    memset(profiles, 0, sizeof(profiles));
    if (prefs.isKey("profiles")) {
        prefs.getBytes("profiles", profiles, sizeof(profiles));
    }
    st.temp_c   = prefs.getUChar("temp_c", 26);
    st.power_on = prefs.getBool("power_on", false);
    st.op_mode  = static_cast<stdAc::opmode_t>(prefs.getUChar("op_mode",
                    static_cast<uint8_t>(stdAc::opmode_t::kCool)));
    st.fan      = static_cast<stdAc::fanspeed_t>(prefs.getUChar("fan",
                    static_cast<uint8_t>(stdAc::fanspeed_t::kAuto)));
    if (st.temp_c < 16 || st.temp_c > 30) st.temp_c = 26;
    for (auto& p : profiles) {
        if (p.brand_idx >= BRAND_COUNT) { p.used = false; p.brand_idx = 0; }
    }
    // Brand-enable bitmap. Missing key = "all enabled" so existing installs
    // keep working before the user touches the configure page.
    if (prefs.isKey("brand_mask")) {
        prefs.getBytes("brand_mask", cfg_mask, sizeof(cfg_mask));
    } else {
        memset(cfg_mask, 0xFF, sizeof(cfg_mask));
    }
    st.app_mode = MODE_HOME;
    prefs.end();
}

static void nvs_save() {
    prefs.begin("acremote", false);
    prefs.putBytes("profiles",   profiles, sizeof(profiles));
    prefs.putBytes("brand_mask", cfg_mask, sizeof(cfg_mask));
    prefs.putUChar("temp_c",   st.temp_c);
    prefs.putBool ("power_on", st.power_on);
    prefs.putUChar("op_mode",  static_cast<uint8_t>(st.op_mode));
    prefs.putUChar("fan",      static_cast<uint8_t>(st.fan));
    prefs.end();
}

static void nvs_wipe() {
    prefs.begin("acremote", false);
    prefs.clear();
    prefs.end();
    memset(profiles, 0, sizeof(profiles));
}

// Broadcast the current st.{power_on,temp_c,op_mode,fan} to every saved
// profile's AC brand. The AC pointing at us picks the one it understands.
static void broadcast_all() {
    uint8_t saved = st.brand_idx;
    uint8_t sent_count = 0;
    Serial.printf("[bcast] power=%d temp=%uC mode=%d profiles=%u\n",
                  (int)st.power_on, (unsigned)st.temp_c, (int)st.op_mode,
                  (unsigned)profile_count());
    for (uint8_t i = 0; i < MAX_PROFILES; i++) {
        if (!profiles[i].used) continue;
        st.brand_idx = profiles[i].brand_idx;
        if (send_ir()) sent_count++;
        delay(80);
    }
    st.brand_idx = saved;
    Serial.printf("[bcast] done, sent=%u\n", (unsigned)sent_count);
}

// -----------------------------------------------------------------------------
// Display
// -----------------------------------------------------------------------------
static const char* mode_name(stdAc::opmode_t m) {
    switch (m) {
        case stdAc::opmode_t::kCool: return "Cool";
        case stdAc::opmode_t::kHeat: return "Heat";
        case stdAc::opmode_t::kDry:  return "Dry";
        case stdAc::opmode_t::kFan:  return "Fan";
        case stdAc::opmode_t::kAuto: return "Auto";
        default:                     return "Off";
    }
}

static void draw_scan_active() {
    auto& d = M5.Display;
    d.fillScreen(BLACK);
    const int w = d.width();

    // SCAN — huge.
    d.setTextDatum(top_center);
    d.setTextColor(YELLOW);
    d.setTextSize(4);
    d.drawString("SCAN", w / 2, 6);

    // Progress N/total — medium.
    char prog[20];
    snprintf(prog, sizeof(prog), "%u/%u",
             (unsigned)(st.brand_idx + 1), (unsigned)BRAND_COUNT);
    d.setTextSize(2);
    d.setTextColor(LIGHTGREY);
    d.drawString(prog, w / 2, 60);

    // Current brand — big.
    d.setTextSize(2);
    d.setTextColor(CYAN);
    d.drawString(BRANDS[st.brand_idx].display_name, w / 2, 96);

    d.drawFastHLine(8, 138, w - 16, DARKGREY);

    // Primary hint — big.
    d.setTextSize(2);
    d.setTextColor(WHITE);
    d.drawString("release", w / 2, 148);
    d.drawString("to lock", w / 2, 174);

    d.setTextSize(1);
    d.setTextColor(YELLOW);
    d.drawString("B = manual lock", w / 2, 206);
    d.setTextColor(DARKGREY);
    d.drawString("(if no beep)", w / 2, 222);
}

static void draw_scan_found(uint8_t brand_idx) {
    auto& d = M5.Display;
    d.fillScreen(BLACK);
    const int w = d.width(), h = d.height();

    // Big green banner.
    d.fillRect(0, 0, w, 84, GREEN);
    d.setTextDatum(middle_center);
    d.setTextColor(BLACK);
    d.setTextSize(4);
    d.drawString("FOUND", w / 2, 42);

    // Brand name big.
    d.setTextDatum(top_center);
    d.setTextSize(3);
    d.setTextColor(WHITE);
    d.drawString(BRANDS[brand_idx].display_name, w / 2, 104);

    // Release to save.
    d.setTextSize(2);
    d.setTextColor(YELLOW);
    d.drawString("RELEASE", w / 2, h - 76);
    d.setTextColor(LIGHTGREY);
    d.drawString("to save", w / 2, h - 50);
}

static const char* fan_name(stdAc::fanspeed_t f) {
    switch (f) {
        case stdAc::fanspeed_t::kMin:    return "Min";
        case stdAc::fanspeed_t::kLow:    return "Low";
        case stdAc::fanspeed_t::kMedium: return "Med";
        case stdAc::fanspeed_t::kHigh:   return "High";
        case stdAc::fanspeed_t::kMax:    return "Max";
        default:                         return "Auto";
    }
}

static void draw_home() {
    auto& d = M5.Display;
    d.fillScreen(BLACK);
    const int w = d.width();

    // --- Header: HUGE setpoint, mode, AC count ---
    d.setTextDatum(top_center);
    d.setTextColor(st.power_on ? CYAN : DARKGREY);
    d.setTextSize(7);                       // ~56 px tall
    char temp_buf[8];
    snprintf(temp_buf, sizeof(temp_buf), "%u", (unsigned)st.temp_c);
    d.drawString(temp_buf, w / 2, 4);

    d.setTextSize(2);
    d.setTextColor(WHITE);
    d.drawString(mode_name(st.op_mode), w / 2, 66);

    // Power-OFF indicator (the AC count line moved into the menu so we don't
    // duplicate it here in the header).
    if (!st.power_on && profile_count() > 0) {
        d.setTextSize(1);
        d.setTextColor(RED);
        d.drawString("OFF", w / 2, 96);
    }

    d.drawFastHLine(6, 110, w - 12, DARKGREY);

    // --- Menu list (size 2 for legibility) ---
    const char* labels[MENU_COUNT];
    char mode_lbl[16], fan_lbl[16], prof_lbl[16];
    snprintf(mode_lbl, sizeof(mode_lbl), "Mode %s", mode_name(st.op_mode));
    snprintf(fan_lbl,  sizeof(fan_lbl),  "Fan %s",  fan_name(st.fan));
    snprintf(prof_lbl, sizeof(prof_lbl), "%u AC",   (unsigned)profile_count());
    labels[MENU_TEMP_UP]   = "Temp +";
    labels[MENU_TEMP_DOWN] = "Temp -";
    labels[MENU_MODE]      = mode_lbl;
    labels[MENU_FAN]       = fan_lbl;
    labels[MENU_PROFILES]  = prof_lbl;

    d.setTextSize(2);
    int y = 116;
    for (uint8_t i = 0; i < MENU_COUNT; i++) {
        bool sel = (i == st.menu_cursor);
        if (sel) {
            d.fillRect(2, y - 2, w - 4, 18, NAVY);
            d.setTextColor(WHITE);
        } else {
            d.setTextColor(LIGHTGREY);
        }
        d.setTextDatum(top_left);
        d.drawString(sel ? ">" : " ", 4, y);
        d.drawString(labels[i], 22, y);
        y += 18;
    }
}

static void flash_feedback(uint16_t color, uint16_t ms) {
    auto& d = M5.Display;
    d.fillRect(0, d.height() - 4, d.width(), 4, color);
    // Caller redraws full screen next loop tick.
    (void)ms;
}

// -----------------------------------------------------------------------------
// Mode transitions and input handling
// -----------------------------------------------------------------------------
static void enter_scan() {
    st.app_mode = MODE_SEARCH;
    st.brand_idx = 0;
    st.last_send_ms = 0;     // send immediately on next tick
    st.scan_started_ms = millis();
    st.scan_found = false;
    st.scan_found_brand = 0;
    mic_warmup = 0;
    mic_baseline_peak = 0;
    draw_scan_active();
}
static void enter_search() { enter_scan(); }

static void enter_home() {
    st.app_mode = MODE_HOME;
    if (st.menu_cursor >= MENU_COUNT) st.menu_cursor = 0;
    nvs_save();
    draw_home();
}

static void complete_search_with(uint8_t brand_idx) {
    int8_t r = add_profile(brand_idx);
    // SCAN's last successful frame already powered the AC on, so reflect
    // that in our internal state — A-short toggle then correctly sends OFF.
    st.power_on = true;
    auto& d = M5.Display;
    d.fillScreen(BLACK);
    d.setTextDatum(middle_center);
    d.setTextSize(2);
    if (r >= 0) {
        d.setTextColor(GREEN);
        d.drawString("Saved!", d.width() / 2, d.height() / 2 - 10);
        d.setTextSize(1);
        d.setTextColor(WHITE);
        d.drawString(BRANDS[brand_idx].display_name,
                     d.width() / 2, d.height() / 2 + 18);
    } else if (r == -1) {
        d.setTextColor(YELLOW);
        d.drawString("Already saved", d.width() / 2, d.height() / 2);
    } else {
        d.setTextColor(RED);
        d.drawString("Full!", d.width() / 2, d.height() / 2);
    }
    delay(900);
    // Normalize the just-paired AC away from SCAN-mode params (18C/Max/turbo)
    // toward sensible HOME defaults (st.temp_c / Auto fan / no turbo). Without
    // this, the AC keeps blasting at 18C until the user toggles via A-short.
    if (r >= 0) broadcast_all();
    enter_home();
}

static void factory_reset() {
    nvs_wipe();
    st.temp_c     = 26;
    st.op_mode    = stdAc::opmode_t::kCool;
    st.fan        = stdAc::fanspeed_t::kAuto;
    st.power_on   = false;
    st.menu_cursor = 0;
    enter_home();
}

static stdAc::opmode_t next_mode(stdAc::opmode_t m) {
    switch (m) {
        case stdAc::opmode_t::kCool: return stdAc::opmode_t::kHeat;
        case stdAc::opmode_t::kHeat: return stdAc::opmode_t::kDry;
        case stdAc::opmode_t::kDry:  return stdAc::opmode_t::kFan;
        case stdAc::opmode_t::kFan:  return stdAc::opmode_t::kAuto;
        default:                     return stdAc::opmode_t::kCool;
    }
}

static stdAc::fanspeed_t next_fan(stdAc::fanspeed_t f) {
    switch (f) {
        case stdAc::fanspeed_t::kAuto: return stdAc::fanspeed_t::kLow;
        case stdAc::fanspeed_t::kLow:  return stdAc::fanspeed_t::kMedium;
        case stdAc::fanspeed_t::kMedium: return stdAc::fanspeed_t::kHigh;
        case stdAc::fanspeed_t::kHigh: return stdAc::fanspeed_t::kMax;
        default:                       return stdAc::fanspeed_t::kAuto;
    }
}

static void show_profiles_list() {
    auto& d = M5.Display;
    d.fillScreen(BLACK);
    int w = d.width();

    d.setTextDatum(top_center);
    d.setTextColor(CYAN);
    d.setTextSize(2);
    d.drawString("Profiles", w / 2, 6);

    d.setTextSize(1);
    d.setTextColor(DARKGREY);
    char hdr[24];
    snprintf(hdr, sizeof(hdr), "%u of %u saved",
             (unsigned)profile_count(), (unsigned)MAX_PROFILES);
    d.drawString(hdr, w / 2, 32);

    d.drawFastHLine(8, 48, w - 16, DARKGREY);

    d.setTextDatum(top_left);
    d.setTextSize(2);
    int slot = 1;
    int y = 56;
    for (uint8_t i = 0; i < MAX_PROFILES; i++) {
        if (!profiles[i].used) continue;
        char line[24];
        snprintf(line, sizeof(line), "%d. %s",
                 slot++, BRANDS[profiles[i].brand_idx].display_name);
        d.setTextColor(WHITE);
        d.drawString(line, 8, y);
        y += 22;
        if (y > d.height() - 40) break;   // overflow guard
    }
    if (profile_count() == 0) {
        d.setTextColor(YELLOW);
        d.drawString("(empty)", 8, 60);
        d.setTextSize(1);
        d.setTextColor(LIGHTGREY);
        d.drawString("hold A to pair", 8, 90);
    }

    d.drawFastHLine(8, d.height() - 28, w - 16, DARKGREY);
    d.setTextDatum(top_center);
    d.setTextSize(1);
    d.setTextColor(GREEN);
    d.drawString("press any key", w / 2, d.height() - 20);
    d.setTextColor(LIGHTGREY);
    d.drawString("to return", w / 2, d.height() - 8);

    // Wait for any button press, then return.
    while (true) {
        M5.update();
        if (M5.BtnA.wasClicked() || M5.BtnA.wasHold()
         || M5.BtnB.wasClicked() || M5.BtnB.wasHold()) break;
        delay(20);
    }
}

static void do_menu_action() {
    switch (st.menu_cursor) {
        case MENU_TEMP_UP:
            if (st.temp_c < 30) st.temp_c++;
            st.power_on = true;
            broadcast_all();
            break;
        case MENU_TEMP_DOWN:
            if (st.temp_c > 16) st.temp_c--;
            st.power_on = true;
            broadcast_all();
            break;
        case MENU_MODE:
            st.op_mode = next_mode(st.op_mode);
            st.power_on = true;
            broadcast_all();
            break;
        case MENU_FAN:
            st.fan = next_fan(st.fan);
            st.power_on = true;
            broadcast_all();
            break;
        case MENU_PROFILES:
            show_profiles_list();
            break;
    }
    nvs_save();
    draw_home();
}

static void nudge_no_profiles() {
    auto& d = M5.Display;
    d.fillScreen(BLACK);
    d.setTextDatum(middle_center);
    d.setTextSize(2);
    d.setTextColor(YELLOW);
    d.drawString("No AC saved", d.width() / 2, d.height() / 2 - 12);
    d.setTextSize(1);
    d.setTextColor(WHITE);
    d.drawString("hold A to add", d.width() / 2, d.height() / 2 + 16);
    delay(1100);
    draw_home();
}

static void handle_home_buttons() {
    if (M5.BtnA.wasClicked()) {                  // TOGGLE power broadcast
        if (profile_count() == 0) { nudge_no_profiles(); return; }
        st.power_on = !st.power_on;
        broadcast_all();
        nvs_save();
        auto& d = M5.Display;
        d.fillRect(0, d.height() - 4, d.width(), 4,
                   st.power_on ? GREEN : RED);
        draw_home();
    }
    if (M5.BtnA.wasHold()) {                     // enter SCAN to pair new AC
        enter_scan();
    }
    if (M5.BtnB.wasClicked()) {                  // scroll cursor next
        st.menu_cursor = (st.menu_cursor + 1) % MENU_COUNT;
        draw_home();
    }
    if (M5.BtnB.wasHold()) {                     // execute selected item
        do_menu_action();
    }
}

// Detect "hold both buttons >= 3 s" -> factory reset, in either mode.
static bool check_dual_hold_reset() {
    bool both = M5.BtnA.isPressed() && M5.BtnB.isPressed();
    uint32_t now = millis();
    if (both) {
        if (st.both_held_since_ms == 0) st.both_held_since_ms = now;
        if (now - st.both_held_since_ms >= RESET_HOLD_MS) {
            st.both_held_since_ms = 0;
            factory_reset();
            return true;
        }
    } else {
        st.both_held_since_ms = 0;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Arduino setup / loop
// -----------------------------------------------------------------------------
void setup() {
    auto cfg = M5.config();
    cfg.internal_spk = false;       // free codec for mic-only use
    cfg.internal_mic = true;
    M5.begin(cfg);
    M5.Display.setRotation(0);    // portrait, USB-C points down
    M5.Display.setBrightness(180);

    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== OmniRemote boot ===");
    Serial.printf("IR_TX_PIN=%u BRAND_COUNT=%u SEARCH_INTERVAL=%lu ms\n",
                  IR_TX_PIN, (unsigned)BRAND_COUNT,
                  (unsigned long)SEARCH_INTERVAL_MS);

    mic_ready = M5.Mic.begin();
    Serial.printf("[mic] M5.Mic.begin() = %d  rate=%lu  buf=%u samples\n",
                  mic_ready, (unsigned long)MIC_SAMPLE_RATE,
                  (unsigned)MIC_BUF_SAMPLES);

    ir.next.protocol = decode_type_t::UNKNOWN;
    nvs_load();
    Serial.printf("[boot] profiles=%u temp=%uC mode=%d\n",
                  (unsigned)profile_count(), (unsigned)st.temp_c,
                  (int)st.op_mode);

    // Slogan splash.
    {
        auto& d = M5.Display;
        d.fillScreen(BLACK);
        d.setTextDatum(middle_center);
        d.setTextSize(2);
        d.setTextColor(CYAN);
        d.drawString("OmniRemote", d.width() / 2, d.height() / 2 - 28);
        d.setTextColor(WHITE);
        d.setTextSize(1);
        d.drawString("one stick,", d.width() / 2, d.height() / 2);
        d.drawString("every appliance", d.width() / 2, d.height() / 2 + 14);
        d.setTextColor(DARKGREY);
        d.drawString("v0.5.1", d.width() / 2, d.height() / 2 + 40);
        delay(1100);
    }

    // First-run UX: jump straight into SEARCH so the device is useful on
    // boot. Returning users land on the HOME menu.
    if (profile_count() == 0) {
        enter_search();
    } else {
        enter_home();
    }
}

void loop() {
    M5.update();

    if (check_dual_hold_reset()) {
        delay(30);
        return;
    }

    if (st.app_mode == MODE_SEARCH) {
        uint32_t now = millis();
        bool a_held = M5.BtnA.isPressed();

        // 0) Manual lock fallback: B tap while holding A locks the current
        //    brand even if the mic missed the AC's beep.
        if (a_held && M5.BtnB.wasClicked() && !st.scan_found) {
            Serial.printf("[scan] manual lock on %s\n",
                          BRANDS[st.brand_idx].display_name);
            st.scan_found = true;
            st.scan_found_brand = st.brand_idx;
            draw_scan_found(st.scan_found_brand);
        }

        // 1) Release => either save the found brand or abandon.
        if (!a_held) {
            if (st.scan_found) {
                Serial.printf("[scan] released after FOUND %s -> save\n",
                              BRANDS[st.scan_found_brand].display_name);
                complete_search_with(st.scan_found_brand);
            } else {
                Serial.println("[scan] released without match -> abandon");
                auto& d = M5.Display;
                d.fillScreen(BLACK);
                d.setTextDatum(middle_center);
                d.setTextSize(2);
                d.setTextColor(YELLOW);
                d.drawString("no match",  d.width() / 2, d.height() / 2 - 14);
                d.setTextColor(LIGHTGREY);
                d.drawString("released",  d.width() / 2, d.height() / 2 + 14);
                delay(900);
                enter_home();
            }
            return;
        }

        // 2) Already found: hold the FOUND screen, don't keep sending.
        if (st.scan_found) {
            delay(20);
            return;
        }

        // 3) Timeout while still pressed.
        if (now - st.scan_started_ms > SCAN_MAX_MS) {
            Serial.println("[scan] timeout -> abandon");
            auto& d = M5.Display;
            d.fillScreen(BLACK);
            d.setTextDatum(middle_center);
            d.setTextSize(2);
            d.setTextColor(YELLOW);
            d.drawString("no match",   d.width() / 2, d.height() / 2 - 14);
            d.setTextColor(LIGHTGREY);
            d.drawString("timeout",    d.width() / 2, d.height() / 2 + 14);
            delay(900);
            enter_home();
            return;
        }

        // 4) Otherwise cycle to next brand and try. Honour the user-configured
        //    brand_mask (set via the configure web page) so SCAN only iterates
        //    brands the user actually has at home.
        if (now - st.last_send_ms >= SEARCH_INTERVAL_MS) {
            if (st.last_send_ms != 0) {
                // Advance to next enabled brand. If none are enabled (e.g.
                // user unchecked all and pushed) fall back to plain rotate.
                uint8_t guard = 0;
                do {
                    st.brand_idx = (st.brand_idx + 1) % BRAND_COUNT;
                } while (!brand_enabled(st.brand_idx) && ++guard < BRAND_COUNT);
            }
            draw_scan_active();
            bool sent = send_ir();
            if (sent && listen_for_beep()) {
                Serial.printf("[scan] BEEP on %s -> freeze, wait for release\n",
                              BRANDS[st.brand_idx].display_name);
                st.scan_found = true;
                st.scan_found_brand = st.brand_idx;
                draw_scan_found(st.scan_found_brand);
            }
        }
    } else if (st.app_mode == MODE_HISTORY) {
        // Legacy mode, not normally reachable in the hold-A UX. Kept so an
        // unexpected entry doesn't soft-lock — just bounce back home.
        enter_home();
    } else {
        handle_home_buttons();
    }

    delay(20);
}

// ESP-IDF entry point. Initialise NVS for the underlying nvs_flash storage
// that our Preferences shim wraps, then drive the same setup() / loop()
// pair we used under Arduino. The 20 ms delay inside loop() keeps the task
// from starving FreeRTOS.
// Visual debug helper — paints a line on the LCD AND echoes via ESP_LOGI to
// USB-Serial-JTAG so we can see boot progress on either surface.
static int g_dbg_y = 4;
static void dbg(const char* msg, uint16_t color = WHITE) {
    ESP_LOGI("boot", "%s", msg);
    M5.Display.setTextColor(color);
    M5.Display.setCursor(4, g_dbg_y);
    M5.Display.print(msg);
    g_dbg_y += 12;
}

// CDC RX line-buffered command parser. Supports:
//   "SET_ENABLED <hex>\n"  — replace cfg_mask with given hex bitmap, persist,
//                            ACK "OK <bits>", then esp_restart() so SCAN sees
//                            the new mask on the next run.
//   "GET_BRANDS\n"         — print one "BRAND <i> <name>\n" line per BRANDS[i],
//                            then "END\n".
//   "GET_ENABLED\n"        — print "MASK <hex>\n" with current cfg_mask.
static char  cdc_rx_buf[512];
static size_t cdc_rx_len = 0;

static int hex_nyb(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void cdc_send(const char* s) {
    if (!s) return;
    tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t*)s, strlen(s));
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(50));
}

static void cdc_handle_cmd(char* line) {
    if (!strncmp(line, "SET_ENABLED ", 12)) {
        const char* hex = line + 12;
        uint8_t fresh[CFG_MASK_BYTES];
        memset(fresh, 0, sizeof(fresh));
        size_t bytes_in = 0;
        while (hex[0] && hex[1] && bytes_in < sizeof(fresh)) {
            int hi = hex_nyb(hex[0]), lo = hex_nyb(hex[1]);
            if (hi < 0 || lo < 0) break;
            fresh[bytes_in++] = (hi << 4) | lo;
            hex += 2;
        }
        if (bytes_in == 0) { cdc_send("ERR no hex\n"); return; }
        memcpy(cfg_mask, fresh, sizeof(cfg_mask));
        nvs_save();
        int enabled = 0;
        for (size_t i = 0; i < BRAND_COUNT; i++) if (brand_enabled(i)) enabled++;
        char ack[48];
        snprintf(ack, sizeof(ack), "OK %d/%u enabled, rebooting\n",
                 enabled, (unsigned)BRAND_COUNT);
        cdc_send(ack);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    } else if (!strcmp(line, "GET_BRANDS")) {
        for (size_t i = 0; i < BRAND_COUNT; i++) {
            char ln[64];
            snprintf(ln, sizeof(ln), "BRAND %u %s\n",
                     (unsigned)i, BRANDS[i].display_name);
            cdc_send(ln);
        }
        cdc_send("END\n");
    } else if (!strcmp(line, "GET_ENABLED")) {
        char ln[2 * CFG_MASK_BYTES + 8] = "MASK ";
        size_t p = strlen(ln);
        for (size_t i = 0; i < sizeof(cfg_mask); i++)
            p += snprintf(ln + p, sizeof(ln) - p, "%02x", cfg_mask[i]);
        snprintf(ln + p, sizeof(ln) - p, "\n");
        cdc_send(ln);
    } else if (line[0]) {
        cdc_send("ERR unknown\n");
    }
}

static void cdc_rx_cb(int /*itf*/, cdcacm_event_t* /*evt*/) {
    uint8_t buf[64];
    size_t  got = 0;
    while (tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0,
                               buf, sizeof(buf), &got) == ESP_OK && got > 0) {
        for (size_t i = 0; i < got; i++) {
            char c = (char)buf[i];
            if (c == '\r') continue;
            if (c == '\n') {
                cdc_rx_buf[cdc_rx_len < sizeof(cdc_rx_buf) ? cdc_rx_len : sizeof(cdc_rx_buf)-1] = 0;
                cdc_handle_cmd(cdc_rx_buf);
                cdc_rx_len = 0;
            } else if (cdc_rx_len < sizeof(cdc_rx_buf) - 1) {
                cdc_rx_buf[cdc_rx_len++] = c;
            }
        }
        got = 0;
    }
}

// "1200-baud touch" — Arduino-style convention: the host (esptool, PIO, etc.)
// opens the CDC port at 1200 baud to ask the firmware to reboot into ROM
// download mode. Avoids needing to physically hold the BOOT button between
// flashes once TinyUSB has taken over USB.
static void cdc_line_coding_cb(int /*itf*/, cdcacm_event_t* evt) {
    if (!evt || evt->type != CDC_EVENT_LINE_CODING_CHANGED) return;
    const cdc_line_coding_t* lc = evt->line_coding_changed_data.p_line_coding;
    if (lc && lc->bit_rate == 1200) {
        ESP_LOGW("dl", "1200-baud touch detected — rebooting to ROM download");
        // Set the persistent flag the ROM bootloader checks on next reset.
        SET_PERI_REG_MASK(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
        vTaskDelay(pdMS_TO_TICKS(80));      // let host close the port cleanly
        esp_restart();
    }
}

static esp_err_t usb_composite_init(void) {
    dbg("usb: install");
    tinyusb_config_t cfg = {};
    esp_err_t r = tinyusb_driver_install(&cfg);
    if (r != ESP_OK) { char b[24]; snprintf(b,sizeof(b),"  FAIL 0x%X",r); dbg(b, RED); return r; }
    dbg("  ok",  GREEN);

    dbg("usb: cdc");
    tinyusb_config_cdcacm_t acm = {};
    acm.usb_dev  = TINYUSB_USBDEV_0;
    acm.cdc_port = TINYUSB_CDC_ACM_0;
    r = tusb_cdc_acm_init(&acm);
    if (r != ESP_OK) { char b[24]; snprintf(b,sizeof(b),"  FAIL 0x%X",r); dbg(b, RED); return r; }
    dbg("  ok",  GREEN);

    // Hook 1200-baud-touch so future flashes don't need a physical button.
    tinyusb_cdcacm_register_callback(TINYUSB_CDC_ACM_0,
                                     CDC_EVENT_LINE_CODING_CHANGED,
                                     cdc_line_coding_cb);
    // Hook line-buffered RX command parser (SET_ENABLED / GET_BRANDS).
    tinyusb_cdcacm_register_callback(TINYUSB_CDC_ACM_0,
                                     CDC_EVENT_RX,
                                     cdc_rx_cb);

    // NOTE: deliberately NOT calling esp_tusb_init_console() — that wrapper
    // installs its own RX handler on CDC_ACM_0 to feed stdin, which steals
    // RX events from our SET_ENABLED / 1200-baud-touch callbacks. Console
    // output goes to /dev/null in this build; ESP_LOGI calls still emit but
    // don't reach the host. Use the LCD `dbg()` strip for boot diagnostics.

    dbg("usb: msc");
    msc_disk_init();
    dbg("  ok",  GREEN);
    return ESP_OK;
}

extern "C" void app_main(void) {
    // NVS first (no display deps).
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        r = nvs_flash_init();
    }
    ESP_ERROR_CHECK(r);

    // Bring up the StickS3 hardware early so the LCD can serve as a visual
    // log surface for boot diagnostics (TinyUSB / MSC init).
    auto cfg = M5.config();
    cfg.internal_spk = false;
    cfg.internal_mic = true;
    M5.begin(cfg);
    M5.Display.setRotation(0);
    M5.Display.setBrightness(180);
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextSize(1);
    dbg("OmniRemote v0.4");
    dbg("nvs ok", GREEN);

    usb_composite_init();
    dbg("starting app...");
    delay(1500);   // let user see boot status on LCD

    setup();
    for (;;) loop();
}
