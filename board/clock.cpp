// Copyright (c) 2026 Koustav Kumar Mondal. Part of SecTrustKey, licensed under AGPL-3.0.
//
// Settings > Clock. The Cardputer ADV has no battery-backed clock, so the time is gone after
// every power-up. The user sets it here over Wi-Fi (SNTP) or by typing it; afterwards the
// ESP32-S3 RTC keeps it across restarts until the power is cut.
//
// Wi-Fi only ever starts in the pre-USB maintenance boot, and the device restarts once the
// clock work is done, so the radio is never on while the key answers USB requests.
#include "clock.h"

#include <algorithm>
#include <memory>
#include <new>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/time.h>

#include "clock_logic.h"
#include "dialog.h"
#include "maintenance_ui.h"

#include "esp_attr.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "mbedtls/sha1.h"

extern "C" {
bool has_set_rtc(void);
void set_rtc_time(time_t t);

/* The Wi-Fi supplicant runs on its own crypto (see config/sdkconfig.defaults), and in
 * ESP-IDF 5.5 that build leaves out SHA-1, which WPA2 needs to derive the network key.
 * This supplies it from Mbed TLS, on the SHA accelerator. */
int sha1_vector(std::size_t num_elem, const std::uint8_t* addr[], const std::size_t* len,
                std::uint8_t* mac) {
    mbedtls_sha1_context ctx;
    mbedtls_sha1_init(&ctx);
    int ret = mbedtls_sha1_starts(&ctx);
    for (std::size_t i = 0; ret == 0 && i < num_elem; ++i) {
        ret = mbedtls_sha1_update(&ctx, addr[i], len[i]);
    }
    if (ret == 0) {
        ret = mbedtls_sha1_finish(&ctx, mac);
    }
    mbedtls_sha1_free(&ctx);
    return ret == 0 ? 0 : -1;
}
}

namespace cardputer {
namespace {

using maint::confirm;
using maint::fresh_keys;
using maint::notice;
using maint::pause;

constexpr char kNamespace[] = "stk_clock";
constexpr char kHeader[] = "Clock";

struct ClockPrefs {
    char ssid[33] = {};
    char pass[65] = {};
    bool auto_sync = false;
    bool tz_set = false;
    int tz = 0;
};

bool load_prefs(ClockPrefs& p) {
    p = ClockPrefs{};
    if (nvs_flash_init() != ESP_OK) { return false; }
    nvs_handle_t h;
    const esp_err_t opened = nvs_open(kNamespace, NVS_READONLY, &h);
    if (opened == ESP_ERR_NVS_NOT_FOUND) { return true; }
    if (opened != ESP_OK) { return false; }
    std::size_t len = sizeof(p.ssid);
    if (nvs_get_str(h, "ssid", p.ssid, &len) != ESP_OK) { p.ssid[0] = '\0'; }
    len = sizeof(p.pass);
    if (nvs_get_str(h, "pass", p.pass, &len) != ESP_OK) { p.pass[0] = '\0'; }
    std::uint8_t a = 0;
    if (nvs_get_u8(h, "auto", &a) == ESP_OK) { p.auto_sync = a == 1; }
    std::int16_t tz = 0;
    if (nvs_get_i16(h, "tz", &tz) == ESP_OK) {
        p.tz = clocklogic::clamp_offset(tz);
        p.tz_set = true;
    }
    nvs_close(h);
    return true;
}

bool save_prefs(const ClockPrefs& p) {
    if (nvs_flash_init() != ESP_OK) { return false; }
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) { return false; }
    esp_err_t r = ESP_OK;
    if (p.ssid[0] != '\0') {
        r = nvs_set_str(h, "ssid", p.ssid);
        if (r == ESP_OK) { r = nvs_set_str(h, "pass", p.pass); }
    } else {
        // Forgetting a network removes the password too, not just the name.
        const esp_err_t a = nvs_erase_key(h, "ssid");
        const esp_err_t b = nvs_erase_key(h, "pass");
        r = (a == ESP_OK || a == ESP_ERR_NVS_NOT_FOUND) && (b == ESP_OK || b == ESP_ERR_NVS_NOT_FOUND)
                ? ESP_OK : ESP_FAIL;
    }
    if (r == ESP_OK) { r = nvs_set_u8(h, "auto", p.auto_sync ? 1 : 0); }
    if (r == ESP_OK && p.tz_set) { r = nvs_set_i16(h, "tz", static_cast<std::int16_t>(p.tz)); }
    if (r == ESP_OK) { r = nvs_commit(h); }
    nvs_close(h);
    return r == ESP_OK;
}

void wipe(ClockPrefs& p) {
    mbedtls_platform_zeroize(&p, sizeof(p));
}

// ---- Wi-Fi --------------------------------------------------------------------------------

constexpr EventBits_t kGotIp = BIT0;
constexpr EventBits_t kDropped = BIT1;
EventGroupHandle_t wifi_events = nullptr;
bool wifi_ready = false;
volatile std::uint8_t last_reason = 0;

void on_wifi_event(void*, esp_event_base_t base, std::int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        last_reason = static_cast<wifi_event_sta_disconnected_t*>(data)->reason;
        xEventGroupSetBits(wifi_events, kDropped);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_events, kGotIp);
    }
}

bool wifi_up() {
    if (wifi_ready) { return true; }
    wifi_events = wifi_events ? wifi_events : xEventGroupCreate();
    if (wifi_events == nullptr || esp_netif_init() != ESP_OK) { return false; }
    const esp_err_t loop = esp_event_loop_create_default();
    if (loop != ESP_OK && loop != ESP_ERR_INVALID_STATE) { return false; }
    if (esp_netif_create_default_wifi_sta() == nullptr) { return false; }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) { return false; }
    // Keep the network only in our own NVS namespace; the driver must not copy it.
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, nullptr) != ESP_OK ||
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, nullptr) != ESP_OK ||
        esp_wifi_start() != ESP_OK) {
        return false;
    }
    wifi_ready = true;
    return true;
}

void wifi_down() {
    if (!wifi_ready) { return; }
    esp_wifi_disconnect();
    esp_wifi_stop();
}

struct Network {
    char ssid[33] = {};
    int rssi = 0;
    bool secure = false;
};

enum class JobKind { Scan, Sync };

struct WifiJob {
    JobKind kind = JobKind::Sync;
    char ssid[33] = {};
    char pass[65] = {};
    volatile int stage = 0;          // 0 starting Wi-Fi, 1 joining, 2 asking the time
    volatile bool cancel = false;
    volatile bool done = false;
    bool ok = false;
    const char* error = "";
    Network found[12];
    int found_count = 0;
};

const char* join_error(std::uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_NO_AP_FOUND:
            return "Network not found";
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_MIC_FAILURE:
        case WIFI_REASON_CONNECTION_FAIL:
            return "Wrong password?";
        default:
            return "Could not join the network";
    }
}

void run_scan(WifiJob& job) {
    wifi_scan_config_t scan = {};
    if (esp_wifi_scan_start(&scan, true) != ESP_OK) {
        job.error = "Wi-Fi scan failed";
        return;
    }
    std::uint16_t count = 20;
    auto* records = new (std::nothrow) wifi_ap_record_t[20];
    if (records == nullptr || esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) {
        delete[] records;
        esp_wifi_clear_ap_list();
        job.error = records == nullptr ? "Not enough memory" : "Wi-Fi scan failed";
        return;
    }
    for (int i = 0; i < count && job.found_count < 12; ++i) {
        const char* name = reinterpret_cast<const char*>(records[i].ssid);
        if (name[0] == '\0') { continue; }   // hidden network: use "Type a name"
        bool seen = false;
        for (int j = 0; j < job.found_count; ++j) {
            seen = seen || std::strcmp(job.found[j].ssid, name) == 0;
        }
        if (seen) { continue; }   // the strongest copy came first
        Network& n = job.found[job.found_count++];
        std::snprintf(n.ssid, sizeof(n.ssid), "%s", name);
        n.rssi = records[i].rssi;
        n.secure = records[i].authmode != WIFI_AUTH_OPEN;
    }
    delete[] records;
    job.ok = true;
}

void run_sync(WifiJob& job) {
    wifi_config_t wc = {};
    std::memcpy(wc.sta.ssid, job.ssid, std::min(sizeof(wc.sta.ssid), std::strlen(job.ssid)));
    std::memcpy(wc.sta.password, job.pass, std::min(sizeof(wc.sta.password), std::strlen(job.pass)));
    wc.sta.threshold.authmode = WIFI_AUTH_OPEN;   // accept whatever security the network uses
    wc.sta.pmf_cfg.capable = true;
    wc.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    job.stage = 1;
    esp_wifi_disconnect();
    xEventGroupClearBits(wifi_events, kGotIp | kDropped);
    last_reason = 0;
    if (esp_wifi_set_config(WIFI_IF_STA, &wc) != ESP_OK) {
        mbedtls_platform_zeroize(&wc, sizeof(wc));
        job.error = "Network settings rejected";
        return;
    }
    mbedtls_platform_zeroize(&wc, sizeof(wc));
    bool joined = false;
    for (int attempt = 0; attempt < 3 && !joined && !job.cancel; ++attempt) {
        xEventGroupClearBits(wifi_events, kGotIp | kDropped);
        esp_wifi_connect();
        const TickType_t start = xTaskGetTickCount();
        while (!job.cancel && xTaskGetTickCount() - start < pdMS_TO_TICKS(12000)) {
            const EventBits_t bits = xEventGroupWaitBits(wifi_events, kGotIp | kDropped, pdTRUE, pdFALSE,
                                                         pdMS_TO_TICKS(250));
            if (bits & kGotIp) { joined = true; break; }
            if (bits & kDropped) { break; }
        }
        if (last_reason == WIFI_REASON_AUTH_FAIL) { break; }   // retrying will not help
    }
    if (job.cancel) { job.error = "Cancelled"; return; }
    if (!joined) {
        job.error = join_error(last_reason);
        return;
    }
    job.stage = 2;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        2, ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.cloudflare.com"));
    if (esp_netif_sntp_init(&config) != ESP_OK) {
        job.error = "Time service failed";
        return;
    }
    bool synced = false;
    for (int i = 0; i < 30 && !job.cancel && !synced; ++i) {
        synced = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(500)) == ESP_OK;
    }
    esp_netif_sntp_deinit();
    esp_wifi_disconnect();
    if (job.cancel) { job.error = "Cancelled"; return; }
    if (!synced || !has_set_rtc()) {
        job.error = "No answer from time server";
        return;
    }
    struct timeval tv = {};
    gettimeofday(&tv, nullptr);
    set_rtc_time(tv.tv_sec);   // marks the clock as set for this power-up
    job.ok = true;
}

void wifi_worker(void* arg) {
    auto* job = static_cast<WifiJob*>(arg);
    job->stage = 0;
    if (!wifi_up()) {
        job->error = "Wi-Fi did not start";
    } else if (job->kind == JobKind::Scan) {
        run_scan(*job);
    } else {
        run_sync(*job);
    }
    job->done = true;
    vTaskDelete(nullptr);
}

// Runs the job on a worker while this task animates progress. DEL asks it to stop; the
// worker always finishes (bounded waits) before this returns, so `job` stays valid.
void run_job(Board& board, WifiJob& job, const char* title) {
    if (xTaskCreate(wifi_worker, "stk_wifi", 8192, &job, 2, nullptr) != pdPASS) {
        job.error = "Not enough memory";
        return;
    }
    const std::uint64_t start = Board::now_ms();
    while (!job.done) {
        board.poll_input();
        if (board.keys().del) { job.cancel = true; }
        char detail[64];
        const int steps = job.kind == JobKind::Scan ? 1 : 3;
        const int step = job.kind == JobKind::Scan ? 1 : job.stage + 1;
        if (job.cancel) {
            std::snprintf(detail, sizeof(detail), "Stopping...");
        } else if (job.kind == JobKind::Scan) {
            std::snprintf(detail, sizeof(detail), "Looking for networks nearby");
        } else if (job.stage == 0) {
            std::snprintf(detail, sizeof(detail), "Starting Wi-Fi");
        } else if (job.stage == 1) {
            std::snprintf(detail, sizeof(detail), "Joining %s", job.ssid);
        } else {
            std::snprintf(detail, sizeof(detail), "Asking a time server");
        }
        board.render(make_progress(title, detail, step, steps, -1, Accent::Blue,
                                   static_cast<int>(((Board::now_ms() - start) / 12) % 100),
                                   "DEL:CANCEL", kHeader));
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

// ---- Screens -----------------------------------------------------------------------------

// Plain visible text entry into `buf` (already holding any starting text).
bool enter_text(Board& board, char* buf, std::size_t cap, const char* title, const char* hint,
                Icon icon) {
    do { board.poll_input(); pause(); } while (board.keys().any);
    Keys previous;
    std::uint64_t last_activity = Board::now_ms();
    for (;;) {
        board.render(make_text_entry(title, buf, cap - 1, hint, "ENT:OK DEL:ERASE FN+DEL:CANCEL",
                                     icon, Accent::Blue));
        board.poll_input();
        const Keys keys = board.keys();
        if (keys.any) { last_activity = Board::now_ms(); }
        if ((board.fn_held() && keys.del) || Board::now_ms() - last_activity > 120000) {
            return false;
        }
        std::size_t len = std::strlen(buf);
        if (keys.del && !previous.del) {
            if (len > 0) { buf[len - 1] = '\0'; }
        } else {
            for (const char* t = board.typed(); *t != '\0' && len + 1 < cap; ++t) {
                buf[len++] = *t;
                buf[len] = '\0';
            }
            if (keys.enter && !previous.enter) { return true; }
        }
        previous = keys;
        pause();
    }
}

void show_local_time_line(char* out, std::size_t cap, int tz) {
    struct timeval tv = {};
    gettimeofday(&tv, nullptr);
    char when[24], zone[16];
    clocklogic::format_local(tv.tv_sec, tz, when, sizeof(when));
    clocklogic::format_offset(tz, zone, sizeof(zone));
    std::snprintf(out, cap, "%s %s", when + 11, zone);   // "15:42 UTC+05:30"
}

// Joins `ssid` and sets the clock. On success the network is saved for next time.
bool sync_now(Board& board, ClockPrefs& prefs, const char* ssid, const char* pass) {
    // Heap, not static: this RAM is needed only here, and the key needs it the rest of the time.
    std::unique_ptr<WifiJob> owned(new (std::nothrow) WifiJob());
    if (!owned) {
        notice(board, "Clock not set", "Not enough memory", Mood::Bad, kHeader);
        return false;
    }
    WifiJob& job = *owned;
    job.kind = JobKind::Sync;
    std::snprintf(job.ssid, sizeof(job.ssid), "%s", ssid);
    std::snprintf(job.pass, sizeof(job.pass), "%s", pass);
    run_job(board, job, "Setting the clock");
    mbedtls_platform_zeroize(job.pass, sizeof(job.pass));
    if (!job.ok) {
        notice(board, "Clock not set", job.error, Mood::Bad, kHeader);
        return false;
    }
    if (std::strcmp(prefs.ssid, ssid) != 0 || std::strcmp(prefs.pass, pass) != 0) {
        std::snprintf(prefs.ssid, sizeof(prefs.ssid), "%s", ssid);
        std::snprintf(prefs.pass, sizeof(prefs.pass), "%s", pass);
        save_prefs(prefs);
    }
    char line[48];
    show_local_time_line(line, sizeof(line), prefs.tz);
    notice(board, "Clock set", line, Mood::Good, kHeader);
    return true;
}

void edit_time_zone(Board& board, ClockPrefs& prefs) {
    int tz = prefs.tz;
    for (;;) {
        Snapshot view;
        view.layout = Layout::Editor;
        view.header = kHeader;
        view.title = "Clock";
        view.icon = Icon::Clock;
        view.accent = Accent::Blue;
        std::snprintf(view.rows[0].text, sizeof(view.rows[0].text), "Time zone (up/down: 1 hour)");
        view.rows[0].selected = true;
        view.row_count = 1;
        clocklogic::format_offset(tz, view.value, sizeof(view.value));
        view.footer = "ENT:SAVE DEL:BACK";
        board.render(view);
        const Keys keys = fresh_keys(board);
        if (keys.del) { return; }
        if (keys.enter) {
            prefs.tz = tz;
            prefs.tz_set = true;
            save_prefs(prefs);
            return;
        }
        if (keys.left) { tz -= clocklogic::kOffsetStep; }
        if (keys.right) { tz += clocklogic::kOffsetStep; }
        if (keys.up) { tz += 60; }
        if (keys.down) { tz -= 60; }
        tz = clocklogic::clamp_offset(tz);
    }
}

void type_the_time(Board& board, ClockPrefs& prefs) {
    if (!prefs.tz_set) {
        notice(board, "Choose your time zone", "Typed times are local time", Mood::Info, kHeader);
        edit_time_zone(board, prefs);
        if (!prefs.tz_set) { return; }
    }
    char text[24] = {};
    if (has_set_rtc()) {
        struct timeval tv = {};
        gettimeofday(&tv, nullptr);
        clocklogic::format_local(tv.tv_sec, prefs.tz, text, sizeof(text));
    }
    for (;;) {
        if (!enter_text(board, text, sizeof(text), "Local date and time",
                        "YYYY-MM-DD HH:MM, Enter at :00 s", Icon::Clock)) {
            return;
        }
        std::int64_t utc = 0;
        if (clocklogic::local_to_utc(text, prefs.tz, &utc)) {
            set_rtc_time(static_cast<time_t>(utc));
            char line[48];
            show_local_time_line(line, sizeof(line), prefs.tz);
            notice(board, "Clock set", line, Mood::Good, kHeader);
            return;
        }
        notice(board, "Not a valid time", "Example: 2026-11-23 15:40", Mood::Warn, kHeader);
    }
}

// Picks a network: from a scan, by typing its name, or forgets the saved one. Then joins it,
// which also checks the password, and sets the clock.
void choose_network(Board& board, ClockPrefs& prefs) {
    std::unique_ptr<WifiJob> owned(new (std::nothrow) WifiJob());
    if (!owned) {
        notice(board, "No networks", "Not enough memory", Mood::Bad, kHeader);
        return;
    }
    WifiJob& scan = *owned;
    scan.kind = JobKind::Scan;
    run_job(board, scan, "Wi-Fi networks");
    if (!scan.ok && !scan.cancel) {
        notice(board, "No networks", scan.error, Mood::Warn, kHeader);
    }
    const int found = scan.ok ? scan.found_count : 0;
    const bool saved = prefs.ssid[0] != '\0';
    const int count = found + 1 + (saved ? 1 : 0);   // networks, "Type a name", "Forget"
    int selected = 0;
    for (;;) {
        Snapshot view;
        view.header = kHeader;
        view.title = found > 0 ? "Choose a network" : "Wi-Fi network";
        view.accent = Accent::Blue;
        const int visible = 3;
        int first = selected - visible + 1;
        if (first < 0) { first = 0; }
        for (int i = 0; i < visible && first + i < count; ++i) {
            const int item = first + i;
            Line& line = view.rows[view.row_count++];
            line.selected = item == selected;
            line.accent = Accent::Blue;
            if (item < found) {
                const Network& n = scan.found[item];
                std::snprintf(line.text, sizeof(line.text), "%s", n.ssid);
                std::snprintf(line.sub, sizeof(line.sub), "%s signal%s",
                              n.rssi > -60 ? "Strong" : (n.rssi > -75 ? "Good" : "Weak"),
                              n.secure ? ", password" : ", open");
                std::snprintf(line.value, sizeof(line.value), "%s",
                              std::strcmp(n.ssid, prefs.ssid) == 0 ? "saved" : "");
                line.icon = Icon::Wifi;
            } else if (item == found) {
                std::snprintf(line.text, sizeof(line.text), "Type a network name");
                std::snprintf(line.sub, sizeof(line.sub), "For hidden networks");
                line.icon = Icon::Wifi;
            } else {
                std::snprintf(line.text, sizeof(line.text), "Forget %s", prefs.ssid);
                std::snprintf(line.sub, sizeof(line.sub), "Removes the name and password");
                line.icon = Icon::Trash;
                line.accent = Accent::Red;
            }
        }
        view.footer = "ENT:OK DEL:BACK";
        board.render(view);
        const Keys keys = fresh_keys(board);
        if (keys.del) { return; }
        if (keys.up) { selected = (selected + count - 1) % count; continue; }
        if (keys.down) { selected = (selected + 1) % count; continue; }
        if (!keys.enter) { continue; }

        if (saved && selected == count - 1) {
            if (confirm(board, "Forget this network?", prefs.ssid, Mood::Warn, kHeader)) {
                mbedtls_platform_zeroize(prefs.ssid, sizeof(prefs.ssid));
                mbedtls_platform_zeroize(prefs.pass, sizeof(prefs.pass));
                prefs.auto_sync = false;
                save_prefs(prefs);
                notice(board, "Network forgotten", "Start-up sync is off", Mood::Good, kHeader);
            }
            return;
        }
        char ssid[33] = {};
        bool secure = true;
        if (selected < found) {
            std::snprintf(ssid, sizeof(ssid), "%s", scan.found[selected].ssid);
            secure = scan.found[selected].secure;
        } else if (!enter_text(board, ssid, sizeof(ssid), "Network name", "Exactly as on the router",
                               Icon::Wifi) || ssid[0] == '\0') {
            continue;
        }
        char pass[65] = {};
        if (std::strcmp(ssid, prefs.ssid) == 0) {
            std::snprintf(pass, sizeof(pass), "%s", prefs.pass);
        }
        if (secure && !enter_text(board, pass, sizeof(pass), "Wi-Fi password",
                                  "Shown as you type", Icon::Lock)) {
            mbedtls_platform_zeroize(pass, sizeof(pass));
            continue;
        }
        const bool ok = sync_now(board, prefs, ssid, pass);
        mbedtls_platform_zeroize(pass, sizeof(pass));
        if (ok) { return; }
    }
}

constexpr std::uint32_t kSkipMagic = 0x5354434bu;   // "STCK"
RTC_NOINIT_ATTR std::uint32_t skip_start_sync;

}  // namespace

int clock_utc_offset_minutes() {
    static bool loaded = false;
    static int offset = 0;
    if (!loaded) {
        ClockPrefs prefs;
        if (load_prefs(prefs)) { offset = prefs.tz; }
        wipe(prefs);
        loaded = true;
    }
    return offset;
}

bool clock_local_time(int* hour, int* minute) {
    if (!has_set_rtc()) { return false; }
    struct timeval tv = {};
    gettimeofday(&tv, nullptr);
    const auto c = clocklogic::civil_from_unix(
        tv.tv_sec + static_cast<std::int64_t>(clock_utc_offset_minutes()) * 60);
    if (hour) { *hour = c.hour; }
    if (minute) { *minute = c.minute; }
    return true;
}

void clock_label(char* out, unsigned cap) {
    int h = 0, m = 0;
    if (clock_local_time(&h, &m)) {
        std::snprintf(out, cap, "%02d:%02d", h, m);
    } else {
        std::snprintf(out, cap, "Not set");
    }
}

void clock_menu(Board& board) {
    ClockPrefs prefs;
    load_prefs(prefs);
    int selected = 0;
    constexpr int kCount = 5;
    for (;;) {
        Snapshot view;
        view.header = kHeader;
        char title[64];
        char now[48];
        if (has_set_rtc()) {
            show_local_time_line(now, sizeof(now), prefs.tz);
            std::snprintf(title, sizeof(title), "Clock %s", now);
        } else {
            std::snprintf(title, sizeof(title), "Clock not set");
        }
        view.title = title;
        view.accent = Accent::Blue;
        Line all[kCount];
        std::snprintf(all[0].text, sizeof(all[0].text), "Sync now");
        if (prefs.ssid[0] != '\0') {
            std::snprintf(all[0].sub, sizeof(all[0].sub), "Over %s", prefs.ssid);
        } else {
            std::snprintf(all[0].sub, sizeof(all[0].sub), "Choose a Wi-Fi network first");
        }
        all[0].icon = Icon::Wifi;
        std::snprintf(all[1].text, sizeof(all[1].text), "Wi-Fi network");
        std::snprintf(all[1].value, sizeof(all[1].value), "%.23s", prefs.ssid[0] ? prefs.ssid : "None");
        std::snprintf(all[1].sub, sizeof(all[1].sub), "Choose, type or forget a network");
        all[1].icon = Icon::Wifi;
        std::snprintf(all[2].text, sizeof(all[2].text), "Sync at start");
        std::snprintf(all[2].value, sizeof(all[2].value), "%s", prefs.auto_sync ? "On" : "Off");
        std::snprintf(all[2].sub, sizeof(all[2].sub), "After power-up, Wi-Fi only for this");
        all[2].icon = Icon::Power;
        std::snprintf(all[3].text, sizeof(all[3].text), "Type the time");
        std::snprintf(all[3].sub, sizeof(all[3].sub), "No Wi-Fi needed");
        all[3].icon = Icon::Clock;
        std::snprintf(all[4].text, sizeof(all[4].text), "Time zone");
        clocklogic::format_offset(prefs.tz, all[4].value, sizeof(all[4].value));
        std::snprintf(all[4].sub, sizeof(all[4].sub), "For the status bar and typed times");
        all[4].icon = Icon::Settings;
        const int visible = 3;
        int first = selected - visible + 1;
        if (first < 0) { first = 0; }
        for (int i = 0; i < visible && first + i < kCount; ++i) {
            view.rows[view.row_count] = all[first + i];
            view.rows[view.row_count].accent = Accent::Blue;
            view.rows[view.row_count].selected = first + i == selected;
            ++view.row_count;
        }
        view.footer = "ENT:OK DEL:DONE";
        board.render(view);
        const Keys keys = fresh_keys(board);
        if (keys.del) { break; }
        if (keys.up) { selected = (selected + kCount - 1) % kCount; continue; }
        if (keys.down) { selected = (selected + 1) % kCount; continue; }
        if (!keys.enter) { continue; }
        switch (selected) {
            case 0:
                if (prefs.ssid[0] == '\0') {
                    choose_network(board, prefs);
                } else {
                    sync_now(board, prefs, prefs.ssid, prefs.pass);
                }
                break;
            case 1:
                choose_network(board, prefs);
                break;
            case 2:
                if (!prefs.auto_sync && prefs.ssid[0] == '\0') {
                    notice(board, "No network saved", "Choose a Wi-Fi network first", Mood::Warn, kHeader);
                    break;
                }
                prefs.auto_sync = !prefs.auto_sync;
                save_prefs(prefs);
                break;
            case 3:
                type_the_time(board, prefs);
                break;
            default:
                edit_time_zone(board, prefs);
                break;
        }
    }
    wipe(prefs);
    wifi_down();
}

void clock_sync_at_start(Board& board) {
    if (skip_start_sync == kSkipMagic) {   // set just before the restart that brought us here
        skip_start_sync = 0;
        return;
    }
    if (has_set_rtc()) { return; }
    ClockPrefs prefs;
    if (!load_prefs(prefs) || !prefs.auto_sync || prefs.ssid[0] == '\0') {
        wipe(prefs);
        return;
    }
    std::unique_ptr<WifiJob> owned(new (std::nothrow) WifiJob());
    if (!owned) {
        wipe(prefs);
        return;   // start without a time rather than fail the boot
    }
    WifiJob& job = *owned;
    job.kind = JobKind::Sync;
    std::snprintf(job.ssid, sizeof(job.ssid), "%s", prefs.ssid);
    std::snprintf(job.pass, sizeof(job.pass), "%s", prefs.pass);
    run_job(board, job, "Setting the clock");
    mbedtls_platform_zeroize(job.pass, sizeof(job.pass));
    if (job.ok) {
        char line[48];
        show_local_time_line(line, sizeof(line), prefs.tz);
        board.render(make_dialog(kHeader, "Clock set", line, "", Mood::Good));
        vTaskDelay(pdMS_TO_TICKS(1200));
    } else if (!job.cancel) {
        board.render(make_dialog(kHeader, "Clock not set", job.error, "Starting without it", Mood::Warn));
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
    wipe(prefs);
    wifi_down();
    // Restart so the key starts with Wi-Fi fully off; the RTC keeps the time. Without a
    // time, skip the sync once so a missing network cannot loop the boot.
    if (!has_set_rtc()) { skip_start_sync = kSkipMagic; }
    esp_restart();
}

}  // namespace cardputer
