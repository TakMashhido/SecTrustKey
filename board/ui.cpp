#include "ui.h"

#include <cstring>

#include "M5Unified.h"
#include "board.h"
#include "backup.h"
#include "clock.h"
#include "credential_service.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif
extern "C" {
#include "file.h"
#include "flash.h"
}
#include "esp_system.h"
#include "presence_bridge.h"
#include "settings.h"
#include "usb_link.h"
#include "dialog.h"
#include "stk_oath_screen.h"
#include "stk_totp.h"
#include <ctime>
#include "esp_heap_caps.h"
#include "tusb.h"
#ifdef CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
#include "esp_core_dump.h"
extern "C" {
extern uint32_t stk_dbg_marker, stk_dbg_free_heap, stk_dbg_largest_block, stk_dbg_progress;
}
#endif

extern "C" {
extern char* rp_id;
extern std::size_t rp_id_len;
extern char* user_name;
extern bool is_busy(void);
extern volatile bool cancel_button;

cf_credential_backend cardputer_credentials_backend(void) __attribute__((weak));

/* Clock (stk_time.c) and the read-only OATH view (oath.c). Weak so a build without the
 * OATH application still links; the Codes screen then reports an error. */
bool has_set_rtc(void);
time_t get_rtc_time(void);
stk_oath_screen_status stk_oath_screen_list(stk_oath_entry_t* out, size_t cap, size_t* count)
    __attribute__((weak));
/* Read-only health checks (apps); weak so the dashboard degrades if an app is absent. */
int stk_health_openpgp_defaults(uint8_t* pw1_default, uint8_t* pw3_default) __attribute__((weak));
int stk_health_piv_defaults(uint8_t* pin_default, uint8_t* puk_default) __attribute__((weak));
int stk_health_fido_pin_set(void) __attribute__((weak));
int cardputer_key_store_sealed(void);
stk_oath_screen_status stk_oath_screen_code(const char* name_raw, size_t name_raw_len,
                                            uint64_t unix_s, uint32_t* code) __attribute__((weak));
}

namespace cardputer {
namespace {

constexpr std::uint8_t kOpMakeCredential = 1;
constexpr std::uint8_t kOpGetAssertion = 2;

static_assert(static_cast<int>(RequestKind::Exclusion) ==
                  CF_PRESENCE_KIND_EXCLUSION,
              "exclusion prompt kind must match the C protocol core");
static_assert(static_cast<int>(RequestKind::Reset) == CF_PRESENCE_KIND_RESET,
              "reset prompt kind must match the C protocol core");
static_assert(static_cast<int>(RequestKind::Selection) ==
                  CF_PRESENCE_KIND_SELECTION,
              "selection prompt kind must match the C protocol core");
static_assert(static_cast<int>(RequestKind::OpenPgpSign) ==
                  CF_PRESENCE_KIND_OPENPGP_SIGN,
              "OpenPGP sign prompt kind must match the C protocol core");
static_assert(static_cast<int>(RequestKind::OpenPgpDecrypt) ==
                  CF_PRESENCE_KIND_OPENPGP_DECRYPT,
              "OpenPGP decrypt prompt kind must match the C protocol core");
static_assert(static_cast<int>(RequestKind::OpenPgpAuth) ==
                  CF_PRESENCE_KIND_OPENPGP_AUTH,
              "OpenPGP authenticate prompt kind must match the C protocol core");

Board board;
PresenceBridge bridge;
cf_credential_service credentials;
Settings applied_settings;
Settings saved_settings;
bool usb_mounted = false;
int presence_kind_override = -1;
volatile bool power_off_started = false;
volatile bool power_off_failed = false;
volatile bool backup_started = false;
volatile bool backup_failed = false;
BackupMode requested_backup = BackupMode::None;
volatile uint32_t usb_sof_ms = 0;
volatile bool usb_sof_seen = false;
volatile bool usb_suspended = false;

#ifdef ESP_PLATFORM
void backup_reboot_task(void*) {
    // Stop accepting host traffic before the final flush. The owner loop
    // continues pumping the existing request and the flash task while waiting.
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(250));
    const std::uint64_t started = Board::now_ms();
    while (is_busy() && Board::now_ms() - started < 35000) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (is_busy() || !flash_commit_sync(5000) || !request_backup_mode(requested_backup)) {
        // A failed flush must not discard dirty RAM by rebooting. Keep the
        // running authenticator, reconnect and report failure on its UI owner.
        tud_connect();
        backup_failed = true;
        vTaskDelete(nullptr);
        return;
    }
    esp_restart();
}

void power_off_task(void*) {
    if (!flash_commit_sync(5000)) {
        power_off_failed = true;
        vTaskDelete(NULL);
        return;
    }
    board.power_off();
    vTaskDelete(NULL);
}
#endif

#ifdef ESP_PLATFORM
SemaphoreHandle_t ui_mutex = nullptr;

void ui_lock() {
    if (ui_mutex != nullptr) {
        xSemaphoreTake(ui_mutex, portMAX_DELAY);
    }
}

void ui_unlock() {
    if (ui_mutex != nullptr) {
        xSemaphoreGive(ui_mutex);
    }
}
#else
void ui_lock() {}
void ui_unlock() {}
#endif

void apply_settings(const Settings& settings) {
    board.set_brightness(settings.brightness);
    board.set_volume(settings.volume);
    bridge.model().set_screen_timeout(screen_timeouts_ms[settings.timeout_index]);
    applied_settings = settings;
}

void refresh_saved_keys() {
    size_t count = 0;
    int truncated = 0;
    const cf_credential_item* items =
        cf_credential_items(&credentials, &count, &truncated);
    SavedKey keys[Model::kMaxSavedKeys];
    int used = 0;
    for (size_t i = 0; i < count && used < Model::kMaxSavedKeys; ++i) {
        SavedKey& key = keys[used];
        std::memset(&key, 0, sizeof(key));
        std::memcpy(key.id, items[i].id, sizeof(key.id));
        key.id_len = items[i].id_len;
        std::memcpy(key.rp_id, items[i].rp_id, sizeof(key.rp_id) - 1);
        std::memcpy(key.account, items[i].account, sizeof(key.account) - 1);
        key.protected_key = items[i].protected_key != 0;
        ++used;
    }
    bridge.model().set_saved_keys(keys, used);
    if (truncated) {
        bridge.model().set_status("Key list limited", Board::now_ms());
    }
}

void handle_action(const Action& action) {
    Model& model = bridge.model();
    if (backup_started || power_off_started) {
        return;
    }
    switch (action.type) {
        case ActionType::BackupToSD:
        case ActionType::RestoreFromSD:
            if (is_busy()) {
                model.set_status("Request in progress", Board::now_ms());
                break;
            }
            requested_backup = action.type == ActionType::BackupToSD ? BackupMode::Save : BackupMode::Restore;
            backup_started = true;
            model.set_status("Restarting for SD...", Board::now_ms());
            if (xTaskCreate(backup_reboot_task, "fido_backup_boot", 4096, nullptr, 1, nullptr) != pdPASS) {
                backup_started = false;
                model.set_status("Backup start failed", Board::now_ms());
            }
            break;
        case ActionType::PassphraseSetup:
        case ActionType::VerifyBackup:
        case ActionType::SdInfo:
        case ActionType::ClockSetup:
            if (is_busy()) {
                model.set_status("Request in progress", Board::now_ms());
                break;
            }
            requested_backup = action.type == ActionType::PassphraseSetup ? BackupMode::Passphrase
                               : action.type == ActionType::VerifyBackup  ? BackupMode::Verify
                               : action.type == ActionType::ClockSetup    ? BackupMode::Clock
                                                                          : BackupMode::SdInfo;
            backup_started = true;
            model.set_status(action.type == ActionType::ClockSetup ? "Restarting for clock..."
                                                                   : "Restarting...",
                             Board::now_ms());
            if (xTaskCreate(backup_reboot_task, "fido_backup_boot", 4096, nullptr, 1, nullptr) != pdPASS) {
                backup_started = false;
                model.set_status("Restart failed", Board::now_ms());
            }
            break;
        case ActionType::OpenSavedKeys:
            if (cf_credential_request_list(&credentials) != CF_CRED_OK) {
                model.set_status("Request in progress", Board::now_ms());
            }
            break;
        case ActionType::SaveSettings:
            if (save_settings(action.settings)) {
                saved_settings = action.settings;
                apply_settings(saved_settings);
                model.set_settings(saved_settings);
                board.play(Tone::Success);
            } else {
                model.set_settings(saved_settings);
                apply_settings(saved_settings);
                model.set_status("Save failed", Board::now_ms());
                board.play(Tone::Failure);
            }
            break;
        case ActionType::DeleteKey: {
            const cf_cred_status status = cf_credential_request_delete(
                &credentials, action.key.id, action.key.id_len);
            if (status == CF_CRED_BUSY) {
                model.set_status("Request in progress", Board::now_ms());
            } else if (status != CF_CRED_OK) {
                model.set_status("Delete failed", Board::now_ms());
            }
            break;
        }
        case ActionType::PowerOff:
            board.flush_input_feedback();
#ifdef ESP_PLATFORM
            if (!power_off_started) {
                power_off_started = true;
                model.set_status("Powering off...", Board::now_ms());
                if (xTaskCreate(power_off_task, "fido_poweroff", 4096, NULL, 1,
                                NULL) != pdPASS) {
                    power_off_started = false;
                    model.set_status("Power off failed", Board::now_ms());
                    board.play(Tone::Failure);
                }
            }
#else
            if (!flash_commit_sync(5000)) {
                model.set_status("Power off failed", Board::now_ms());
                board.play(Tone::Failure);
            } else {
                board.power_off();
            }
#endif
            break;
        default:
            break;
    }
}

// ---- Codes screen ---------------------------------------------------------------------
stk_oath_entry_t oath_entries[STK_OATH_SCREEN_MAX];
std::size_t oath_count = 0;
bool oath_loaded = false;
std::uint64_t oath_steps[STK_OATH_SCREEN_MAX];
CodeItem code_items[STK_OATH_SCREEN_MAX];
std::uint64_t codes_last_ms = 0;
int codes_revealed = -1;

void forget_codes() {
    // Codes are secrets for their time step: wipe them whenever the screen is not showing.
    std::memset(code_items, 0, sizeof(code_items));
    std::memset(oath_entries, 0, sizeof(oath_entries));
    oath_count = 0;
    oath_loaded = false;
    codes_revealed = -1;
}

void format_code(std::uint32_t code, std::uint8_t digits, char* out, std::size_t cap) {
    char raw[12];
    std::snprintf(raw, sizeof(raw), "%0*lu", static_cast<int>(digits),
                  static_cast<unsigned long>(code));
    const int half = digits / 2;   // "123 456", "1234 5678"
    std::snprintf(out, cap, "%.*s %s", half, raw, raw + half);
}

void pump_codes(std::uint64_t now) {
    Model& model = bridge.model();
    if (model.screen() != Screen::Codes) {
        if (oath_loaded || code_items[0].name[0] != '\0') {
            forget_codes();
        }
        return;
    }
    if (now - codes_last_ms < 200) {
        return;
    }
    codes_last_ms = now;
    if (stk_oath_screen_list == nullptr || stk_oath_screen_code == nullptr) {
        model.set_codes(CodesState::Error, nullptr, 0, 0, 30);
        return;
    }
    if (!has_set_rtc()) {
        model.set_codes(CodesState::TimeUnknown, nullptr, 0, 0, 30);
        return;
    }
    if (is_busy()) {
        return;   // never read the store while a USB command may be writing it
    }
    if (!oath_loaded) {
        const stk_oath_screen_status status =
            stk_oath_screen_list(oath_entries, STK_OATH_SCREEN_MAX, &oath_count);
        if (status == STK_OATH_SCREEN_PROTECTED) {
            model.set_codes(CodesState::Protected, nullptr, 0, 0, 30);
            return;
        }
        if (status != STK_OATH_SCREEN_OK) {
            model.set_codes(CodesState::Error, nullptr, 0, 0, 30);
            return;
        }
        oath_loaded = true;
        for (std::size_t i = 0; i < STK_OATH_SCREEN_MAX; ++i) {
            oath_steps[i] = UINT64_MAX;
            code_items[i] = CodeItem{};
        }
        for (std::size_t i = 0; i < oath_count; ++i) {
            std::snprintf(code_items[i].name, sizeof(code_items[i].name), "%.64s", oath_entries[i].name);
            code_items[i].totp = oath_entries[i].totp != 0;
            code_items[i].touch = oath_entries[i].touch != 0;
        }
    }
    if (oath_count == 0) {
        model.set_codes(CodesState::Empty, nullptr, 0, 0, 30);
        return;
    }
    const std::uint64_t unix_s = static_cast<std::uint64_t>(get_rtc_time());
    const int selected = model.code_index();
    const int revealed = model.code_revealed(selected) ? selected : -1;
    for (std::size_t i = 0; i < oath_count; ++i) {
        const stk_oath_entry_t& e = oath_entries[i];
        if (!e.totp) {
            continue;
        }
        const std::uint64_t step = stk_totp_step(unix_s, e.period);
        if (step != oath_steps[i] && static_cast<int>(i) == codes_revealed) {
            model.hide_revealed_code();     // a touch code is revealed for one step only
            codes_revealed = -1;
        }
        const bool wanted = !e.touch || static_cast<int>(i) == revealed;
        if (!wanted) {
            std::memset(code_items[i].code, 0, sizeof(code_items[i].code));
            oath_steps[i] = UINT64_MAX;
            continue;
        }
        if (step == oath_steps[i]) {
            continue;
        }
        std::uint32_t code = 0;
        if (stk_oath_screen_code(e.name_raw, e.name_raw_len, unix_s, &code) == STK_OATH_SCREEN_OK) {
            format_code(code, e.digits, code_items[i].code, sizeof(code_items[i].code));
            oath_steps[i] = step;
            if (e.touch) {
                codes_revealed = static_cast<int>(i);
            }
        } else {
            std::snprintf(code_items[i].code, sizeof(code_items[i].code), "error");
        }
    }
    const stk_oath_entry_t& current = oath_entries[selected < static_cast<int>(oath_count) ? selected : 0];
    const std::uint32_t period = current.totp ? (current.period ? current.period : 30u) : 30u;
    model.set_codes(CodesState::Ready, code_items, static_cast<int>(oath_count),
                    static_cast<int>(stk_totp_seconds_left(unix_s, period)),
                    static_cast<int>(period));
}

// ---- Health screen ---------------------------------------------------------------------
std::uint64_t health_last_ms = 0;

void health_item(HealthItem* items, int& n, const char* label, const char* value,
                 HealthLevel level, const char* advice) {
    if (n >= Model::kMaxHealth) {
        return;
    }
    HealthItem& item = items[n++];
    std::snprintf(item.label, sizeof(item.label), "%s", label);
    std::snprintf(item.value, sizeof(item.value), "%s", value);
    std::snprintf(item.advice, sizeof(item.advice), "%s", advice);
    item.level = level;
}

void pump_health(std::uint64_t now) {
    Model& model = bridge.model();
    if (model.screen() != Screen::Health && model.screen() != Screen::HealthDetail) {
        health_last_ms = 0;
        return;
    }
    if ((health_last_ms != 0 && now - health_last_ms < 2000) || is_busy()) {
        return;   // read the store only while no USB command runs
    }
    health_last_ms = now;
    HealthItem items[Model::kMaxHealth];
    int n = 0;
    const bool sealed = cardputer_key_store_sealed() != 0;
    health_item(items, n, "Passphrase", sealed ? "On" : "Off",
                sealed ? HealthLevel::Good : HealthLevel::Warn,
                sealed ? "Your keys are encrypted in flash" : "Settings > Security & Backup > Passphrase");
    if (stk_health_fido_pin_set != nullptr) {
        const bool pin = stk_health_fido_pin_set() != 0;
        health_item(items, n, "FIDO PIN", pin ? "Set" : "Not set",
                    pin ? HealthLevel::Good : HealthLevel::Warn,
                    pin ? "Sites ask for it when you sign in" : "Add one in your browser's security-key settings");
    }
    std::uint8_t a = 1, b = 1;
    if (stk_health_openpgp_defaults != nullptr && stk_health_openpgp_defaults(&a, &b) == 0) {
        // Say which one is still at its public factory value: a user who changed only the
        // user PIN must see that the admin PIN (8+ characters) is what remains.
        health_item(items, n, "OpenPGP PINs", !a && !b ? "Changed" : (a && b ? "Default" : (a ? "User" : "Admin")),
                    !a && !b ? HealthLevel::Good : HealthLevel::Warn,
                    !a && !b ? "No factory PINs left"
                    : (a && b ? "gpg --card-edit, then admin, then passwd"
                              : (a ? "User PIN is still 123456: passwd, option 1" : "Admin PIN is still 12345678: passwd, option 3")));
    }
    if (stk_health_piv_defaults != nullptr && stk_health_piv_defaults(&a, &b) == 0) {
        health_item(items, n, "PIV PINs", !a && !b ? "Changed" : (a && b ? "Default" : (a ? "PIN" : "PUK")),
                    !a && !b ? HealthLevel::Good : HealthLevel::Warn,
                    !a && !b ? "No factory PIN or PUK left"
                    : (a && b ? "ykman piv access change-pin and change-puk"
                              : (a ? "PIN is still 123456: ykman piv access change-pin" : "PUK is still 12345678: ykman piv access change-puk")));
    }
    const bool backup = backup_verified_once();
    health_item(items, n, "SD backup", backup ? "Verified" : "None",
                backup ? HealthLevel::Good : HealthLevel::Warn,
                backup ? "A backup was saved and checked" : "Settings > Security & Backup > Backup to SD");
    health_item(items, n, "Clock", has_set_rtc() ? "Set" : "Unknown", HealthLevel::Info,
                has_set_rtc() ? "One-time codes can be shown" : "Settings > Clock: Wi-Fi or type it");
    char value[16];
    char advice[64];
    std::snprintf(value, sizeof(value), "%u KB",
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024));
    std::snprintf(advice, sizeof(advice), "Largest block %u KB, UI stack %u KB left",
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) / 1024));
    health_item(items, n, "Memory", value, HealthLevel::Info, advice);
    const std::uint64_t minutes = now / 60000;
    std::snprintf(value, sizeof(value), "%uh %02um", static_cast<unsigned>(minutes / 60),
                  static_cast<unsigned>(minutes % 60));
    health_item(items, n, "Uptime", value, HealthLevel::Info, "Time since the device was switched on");
#ifdef STK_BUILD_NUMBER
    std::snprintf(value, sizeof(value), "#%d", STK_BUILD_NUMBER);
#else
    std::snprintf(value, sizeof(value), "dev");
#endif
    health_item(items, n, "Firmware", value, HealthLevel::Info, "SecTrustKey for Cardputer ADV");
    model.set_health(items, n);
}

void pump_credentials() {
    Model& model = bridge.model();
    const cf_cred_result result = cf_credential_service_poll(&credentials);
    if (result == CF_CRED_RESULT_LIST) {
        refresh_saved_keys();
    } else if (result == CF_CRED_RESULT_DELETED) {
        board.play(Tone::Success);
        refresh_saved_keys();
    } else if (result == CF_CRED_RESULT_NOT_FOUND) {
        model.set_status("Key not found", Board::now_ms());
        board.play(Tone::Failure);
    } else if (result == CF_CRED_RESULT_FORBIDDEN) {
        model.set_status("Protected key", Board::now_ms());
        board.play(Tone::Failure);
    } else if (result == CF_CRED_RESULT_ERROR) {
        model.set_status("Key list failed", Board::now_ms());
        board.play(Tone::Failure);
    }
}

}  // namespace
}  // namespace cardputer

using namespace cardputer;

extern "C" void cardputer_ui_init(void) {
#ifdef ESP_PLATFORM
    if (ui_mutex == nullptr) {
        ui_mutex = xSemaphoreCreateMutex();
    }
#endif
    ui_lock();
    if (!board.begin()) {
        // Keep the fatal keyboard message visible; do not start a menu or
        // USB authenticator that cannot receive presence confirmations.
        ui_unlock();
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
#ifdef CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    // Debug builds only: show where the previous run crashed, then clear the dump.
    if (esp_core_dump_image_check() == ESP_OK) {
        esp_core_dump_summary_t summary = {};
        if (esp_core_dump_get_summary(&summary) == ESP_OK) {
            M5.Display.fillScreen(0x0000);
            M5.Display.setTextColor(0xFFFF, 0x0000);
            M5.Display.setTextSize(1);
            M5.Display.setCursor(0, 0);
            M5.Display.printf("CRASH task=%.12s\ncause=%u pc=%08x\nvaddr=%08x\n", summary.exc_task,
                              static_cast<unsigned>(summary.ex_info.exc_cause),
                              static_cast<unsigned>(summary.exc_pc),
                              static_cast<unsigned>(summary.ex_info.exc_vaddr));
            for (int i = 0; i < 10 && i < static_cast<int>(summary.exc_bt_info.depth); ++i) {
                M5.Display.printf("%08x ", static_cast<unsigned>(summary.exc_bt_info.bt[i]));
                if (i % 3 == 2) M5.Display.printf("\n");
            }
        }
        if (stk_dbg_marker == 0xC0DE0001u || stk_dbg_marker == 0xC0DE0002u) {
            M5.Display.printf("\n%s free=%u big=%u prog=%u", stk_dbg_marker == 0xC0DE0001u ? "LAUNCH FAILED" : "STALLED",
                              static_cast<unsigned>(stk_dbg_free_heap), static_cast<unsigned>(stk_dbg_largest_block),
                              static_cast<unsigned>(stk_dbg_progress));
            stk_dbg_marker = 0;
        }
        // The dump is kept so it can be read back with esptool; a new crash overwrites it.
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
#endif
    // Boot reveal (~0.7 s), once per power-up.
    for (int percent = 0; percent <= 100; percent += 5) {
        board.render(make_splash(percent));
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    vTaskDelay(pdMS_TO_TICKS(250));
    bridge.start(Board::now_ms());
    if (load_settings(&saved_settings)) {
        // Loaded values are already validated by the NVS adapter.
    } else {
        saved_settings = Settings{};
    }
    apply_settings(saved_settings);
    bridge.model().set_settings(saved_settings);
    {
        char build[16];
#ifdef STK_BUILD_NUMBER
        std::snprintf(build, sizeof(build), "#%d", STK_BUILD_NUMBER);
#else
        std::snprintf(build, sizeof(build), "dev");
#endif
        bridge.model().set_firmware_label(build);
    }

    cf_credential_backend backend = {};
    if (cardputer_credentials_backend != nullptr) {
        backend = cardputer_credentials_backend();
    }
    cf_credential_service_init(&credentials, backend);
    ui_unlock();
}

extern "C" void cardputer_ui_pump(void) {
    ui_lock();
    const std::uint64_t now = Board::now_ms();
    board.poll_input();
    bridge.key_snapshot(board.keys(), now);
    bridge.tick(now);

    Model& model = bridge.model();
    static bool sof_enabled = false;
    bool mounted = tud_mounted() && !usb_suspended;
    if (mounted && !sof_enabled) {
        tud_sof_cb_enable(true);
        sof_enabled = true;
    }
    if (mounted && usb_sof_seen) {
        const uint32_t now32 = static_cast<uint32_t>(now);
        // usb_sof_ms is written by the USB task on the other core and can be newer than
        // `now`. Unsigned subtraction then wrapped to ~4e9 and looked like an unplugged
        // cable, cancelling any open prompt within ~0.1 s (seen on hardware as an instant
        // OPERATION_DENIED). Use a signed age, and allow brief USB task stalls (flash
        // writes) without declaring the cable gone.
        if (usb_link_lost(now32, usb_sof_ms)) {
            // On the Cardputer ADV there is no VBUS monitor. TinyUSB only
            // reports umount when the host drives a disconnect; poll SOF so
            // pulling the cable also clears the connected state.
            mounted = false;
        }
    }
    if (mounted != usb_mounted) {
        if (!mounted) {
            bridge.cancel(bridge.active_generation(), now);
        }
        usb_mounted = mounted;
        model.set_usb_mounted(mounted);
    }

    {
        // Status-bar clock in the user's time zone, once the time is known.
        StatusBar bar = model.status_bar();
        int hour = 0, minute = 0;
        bar.time_known = clock_local_time(&hour, &minute);
        bar.hour = static_cast<std::uint8_t>(hour);
        bar.minute = static_cast<std::uint8_t>(minute);
        model.set_status_bar(bar);
    }

    Action action;
    if (model.take_action(&action)) {
        handle_action(action);
    }

    if (model.editing_settings()) {
        const Settings& draft = model.draft_settings();
        if (draft.brightness != board.brightness()) {
            board.set_brightness(draft.brightness);
        }
        if (draft.volume != board.volume()) {
            board.set_volume(draft.volume);
        }
    } else if (model.settings().volume != board.volume() ||
               model.settings().brightness != board.brightness() ||
               model.settings().timeout_index != applied_settings.timeout_index) {
        apply_settings(model.settings());
    }
    board.flush_input_feedback();

    if (!is_busy()) {
        pump_credentials();
    }
    pump_codes(now);
    pump_health(now);

    if (power_off_failed) {
        power_off_failed = false;
        power_off_started = false;
        model.set_status("Power off failed", Board::now_ms());
        board.play(Tone::Failure);
    }

    if (backup_failed) {
        backup_failed = false;
        backup_started = false;
        model.set_status("Backup start failed", Board::now_ms());
        board.play(Tone::Failure);
    }

    board.render(model.view());
    ui_unlock();
}

extern "C" std::uint32_t cardputer_presence_begin(std::uint32_t timeout_ms,
                                                   std::uint8_t operation) {
    ui_lock();
    RequestKind kind = RequestKind::Generic;
    if (presence_kind_override >= 0) {
        kind = static_cast<RequestKind>(presence_kind_override);
        presence_kind_override = -1;
    } else if (operation == kOpMakeCredential) {
        kind = RequestKind::MakeCredential;
    } else if (operation == kOpGetAssertion) {
        kind = RequestKind::GetAssertion;
    }
    const char* account = user_name != nullptr ? user_name : "";
    const std::size_t account_len = user_name != nullptr ? std::strlen(user_name) : 0;
    const char* rp = (kind == RequestKind::Reset) ? nullptr : rp_id;
    const std::size_t rp_len = rp != nullptr ? rp_id_len : 0;
    const std::uint32_t generation =
        bridge.begin(kind, rp, rp_len, account, account_len, Board::now_ms(),
                     timeout_ms != 0 ? timeout_ms : 30000);
    board.play(Tone::Request);
    ui_unlock();
    return generation;
}

extern "C" int cardputer_presence_take(std::uint32_t generation) {
    ui_lock();
    const PresenceResult result = bridge.take(generation);
    ui_unlock();
    switch (result) {
        case PresenceResult::Confirmed:
            return 0;
        case PresenceResult::TimedOut:
            return 1;
        case PresenceResult::Cancelled:
        case PresenceResult::Stale:
            return 2;
        default:
            return -1;
    }
}

extern "C" void cardputer_presence_cancel(std::uint32_t generation) {
    ui_lock();
    bridge.cancel(generation, Board::now_ms());
    ui_unlock();
}

extern "C" std::uint32_t cardputer_presence_active(void) {
    ui_lock();
    const std::uint32_t generation = bridge.active_generation();
    ui_unlock();
    return generation;
}

extern "C" int cardputer_presence_wait_openpgp(int kind, std::uint32_t timeout_ms) {
    const char* label = nullptr;
    switch (kind) {
        case CF_PRESENCE_KIND_OPENPGP_SIGN:
            label = "Signature key";
            break;
        case CF_PRESENCE_KIND_OPENPGP_DECRYPT:
            label = "Decryption key";
            break;
        case CF_PRESENCE_KIND_OPENPGP_AUTH:
            label = "Authentication key";
            break;
        default:
            return 2;  // unknown request: refuse rather than ask a misleading question
    }
    static const char kCard[] = "OpenPGP card";
    ui_lock();
    const std::uint32_t generation =
        bridge.begin(static_cast<RequestKind>(kind), kCard, sizeof(kCard) - 1, label,
                     std::strlen(label), Board::now_ms(),
                     timeout_ms != 0 ? timeout_ms : 30000);
    board.play(Tone::Request);
    ui_unlock();
    for (;;) {
        if (cancel_button) {
            cardputer_presence_cancel(generation);
            cancel_button = false;
        }
        const int result = cardputer_presence_take(generation);
        if (result >= 0) {
            return result;
        }
        cardputer_ui_pump();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

extern "C" void cardputer_presence_set_kind(int kind) {
    ui_lock();
    presence_kind_override = kind;
    ui_unlock();
}

extern "C" void cardputer_storage_failure(int status) {
    (void)status;
    backup_recovery(board);
}

extern "C" int cardputer_unlock_device(const cf_key_flash* flash, cf_device_keys* out) {
    unlock_store_at_boot(board, flash, out);
    return 0;
}

extern "C" void cardputer_backup_startup(void) {
    backup_startup(board);
}

extern "C" int stk_board_init(void) {
    cardputer_ui_init();
    return 0;
}

extern "C" void stk_ui_task(void) {
    cardputer_ui_pump();
}

extern "C" void stk_storage_failure(int status) {
    cardputer_storage_failure(status);
}

extern "C" void tud_mount_cb(void) {
    usb_suspended = false;
}

extern "C" void tud_resume_cb(void) {
    usb_suspended = false;
}

extern "C" void tud_umount_cb(void) {
    usb_suspended = true;
    usb_sof_seen = false;
    cardputer_presence_cancel(cardputer_presence_active());
}

extern "C" void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
    usb_suspended = true;
    usb_sof_seen = false;
    cardputer_presence_cancel(cardputer_presence_active());
}

extern "C" void tud_sof_cb(uint32_t frame_count) {
    (void)frame_count;
    usb_sof_ms = static_cast<uint32_t>(Board::now_ms());
    usb_sof_seen = true;
}
