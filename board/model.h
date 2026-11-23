#pragma once
#include <cstdint>
#include "settings.h"
#include "ui_icons.h"
#include "ui_theme.h"

namespace cardputer {

enum class Decision { None, Confirm, Cancel, TimedOut };
enum class RequestKind {
    Generic, MakeCredential, GetAssertion, Exclusion, Reset, Selection,
    /* OpenPGP touch confirmation (UIF). Never shows FIDO relying-party text. */
    OpenPgpSign, OpenPgpDecrypt, OpenPgpAuth
};
enum class Screen { MainMenu, SavedKeys, KeyDetails, Settings, SettingsEdit, ConfirmDeleteKey, Backup, Codes, Health, HealthDetail, About };
enum class ActionType { None, OpenSavedKeys, OpenSettings, PowerOff, DeleteKey, SaveSettings, BackupToSD, RestoreFromSD, OpenCodes, PassphraseSetup, OpenHealth, VerifyBackup, SdInfo, ClockSetup };
// What the Codes screen can show. Codes appear only in Ready; every other state explains why.
enum class CodesState { Loading, TimeUnknown, Protected, Empty, Ready, Error };
enum class Layout { List, Home, Details, Prompt, Editor, Entry, Progress, About, Splash };
using Icon = lcd::Glyph;
using Accent = lcd::Accent;
using Clip = lcd::Clip;

struct Keys {
    bool any = false;
    bool enter = false;
    bool del = false;
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool modifier = false;
};

struct RequestInfo {
    RequestKind kind = RequestKind::Generic;
    char rp_id[256] = {};
    char account[65] = {};
    std::uint8_t stage = 0;
};

struct SavedKey {
    std::uint8_t id[32] = {};
    std::uint8_t id_len = 0;
    char rp_id[256] = {};
    char account[65] = {};
    bool protected_key = false;
};

// One account on the Codes screen. `code` is the formatted code, or empty when not computed.
struct CodeItem {
    char name[65] = {};
    char code[12] = {};
    bool totp = true;     // HOTP is listed but never computed on screen (it would advance the counter)
    bool touch = false;   // the owner asked for a touch: hidden until Enter on that row
};

// One line of the security health check.
enum class HealthLevel : std::uint8_t { Good, Warn, Info };
struct HealthItem {
    char label[32] = {};
    char value[16] = {};
    char advice[64] = {};
    HealthLevel level = HealthLevel::Info;
};

struct Line {
    // Preserve complete RP/account UTF-8; the renderer clips to the available width.
    char text[256] = {};
    char sub[65] = {};      // secondary line, shown for the selected row
    char value[24] = {};    // right-aligned value, e.g. a setting's current level
    bool selected = false;
    Icon icon = Icon::Key;
    Accent accent = Accent::Teal;
    Clip clip = Clip::Tail;   // how `text` is shortened when it does not fit
    bool mono = false;        // draw `value` in the monospace code font (one-time codes)
};

// One entry of the home grid.
struct Tile {
    char label[24] = {};
    char hint[32] = {};
    Icon glyph = Icon::Key;
    Accent accent = Accent::Teal;
    bool enabled = true;
};

// What the status bar shows. Everything is optional: -1 / false means "unknown / absent".
struct StatusBar {
    bool usb = false;
    bool sd = false;
    bool locked = false;
    bool time_known = false;
    bool charging = false;
    int battery = -1;
    std::uint8_t hour = 0;
    std::uint8_t minute = 0;
    bool operator==(const StatusBar& o) const {
        return usb == o.usb && sd == o.sd && locked == o.locked && time_known == o.time_known &&
               charging == o.charging && battery == o.battery && hour == o.hour && minute == o.minute;
    }
};

struct Snapshot {
    /* Prompt header bar. nullptr keeps the FIDO default so existing prompts are unchanged. */
    const char* header = nullptr;
    const char* title = "";
    const char* subtitle = "";   // a note under the main content (warnings, hints)
    Line rows[4] = {};
    int row_count = 0;
    Tile tiles[8] = {};          // Layout::Home
    int tile_count = 0;
    int tile_selected = 0;
    const char* footer = "";
    const char* status = "";     // transient message shown as a toast
    bool screen_on = true;
    Layout layout = Layout::List;
    Icon icon = Icon::Key;
    Accent accent = Accent::Teal;
    StatusBar bar;
    char value[32] = {};
    int meter = -1;
    int progress = -1;           // 0..100 time left on a confirmation, -1 when not applicable
    int phase = 0;               // 0..99 animation phase (progress sheen); redraws when it moves
};

struct Action {
    ActionType type = ActionType::None;
    SavedKey key = {};
    Settings settings = {};
};

class Model {
public:
    static constexpr int kMaxSavedKeys = 32;
    static constexpr int kMaxCodes = 16;
    static constexpr int kMaxHealth = 12;

    void start(std::uint64_t now);
    void set_screen_timeout(std::uint64_t milliseconds);
    std::uint64_t screen_timeout() const { return screen_timeout_; }
    void tick(std::uint64_t now);
    void key_snapshot(Keys keys, std::uint64_t now);

    std::uint32_t begin_request(std::uint64_t now, std::uint64_t timeout_ms);
    std::uint32_t begin_request(std::uint64_t now, std::uint64_t timeout_ms,
                                const RequestInfo& info);
    void cancel_request(std::uint64_t now);
    Decision take_decision();
    bool decision_pending() const { return decision_ != Decision::None; }
    std::uint32_t decision_generation() const { return decision_generation_; }
    std::uint32_t active_generation() const { return pending_ ? active_generation_ : 0; }
    const RequestInfo* active_request() const { return pending_ ? &request_ : nullptr; }
    RequestInfo request_info() const { return request_; }

    bool screen_on() const { return screen_on_; }
    bool pending() const { return pending_; }
    Screen screen() const { return screen_; }
    const Settings& settings() const { return settings_; }
    const Settings& draft_settings() const { return draft_; }
    bool editing_settings() const { return screen_ == Screen::SettingsEdit; }

    void set_settings(const Settings& settings);
    void set_saved_keys(const SavedKey* keys, int count);
    int saved_key_count() const { return saved_key_count_; }
    const SavedKey* saved_key(int index) const;
    // Codes screen: the board supplies state, accounts and the countdown of the current step.
    void set_codes(CodesState state, const CodeItem* items, int count, int seconds_left, int period);
    CodesState codes_state() const { return codes_state_; }
    int code_count() const { return code_count_; }
    int code_index() const { return code_index_; }
    // A touch-protected code is revealed for the step in which Enter was pressed on it.
    bool code_revealed(int index) const { return index == revealed_index_ && index >= 0; }
    void hide_revealed_code() { revealed_index_ = -1; }
    // Health screen: checks the board computed (read-only; no PIN retries are spent).
    void set_health(const HealthItem* items, int count);
    int health_count() const { return health_count_; }
    // Firmware build for the About page, e.g. "#1190".
    void set_firmware_label(const char* label);
    void set_usb_mounted(bool mounted);
    bool usb_mounted() const { return usb_mounted_; }
    // Status-bar facts that the board samples; the model only carries them to the view.
    void set_status_bar(const StatusBar& bar) { bar_ = bar; }
    const StatusBar& status_bar() const { return bar_; }
    void set_status(const char* text, std::uint64_t now);

    bool take_action(Action* out);
    Snapshot view() const;

private:
    void finish(Decision decision, std::uint64_t now);
    void navigate(Keys keys);
    void adjust_setting(int direction);
    void note_activity(std::uint64_t now);
    void set_line(Line* line, const char* text, bool selected) const;

    std::uint64_t last_activity_ = 0;
    std::uint64_t screen_timeout_ = 60000;
    std::uint64_t request_start_ = 0;
    std::uint64_t request_timeout_ = 30000;
    std::uint64_t status_since_ = 0;
    Keys previous_{};
    bool screen_on_ = true;
    bool blocked_until_release_ = false;
    bool pending_ = false;
    bool usb_mounted_ = false;
    Decision decision_ = Decision::None;
    std::uint32_t next_generation_ = 1;
    std::uint32_t active_generation_ = 0;
    std::uint32_t decision_generation_ = 0;
    RequestInfo request_{};
    Screen screen_ = Screen::MainMenu;
    int main_index_ = 0;
    int key_index_ = 0;
    int settings_index_ = 0;
    int edit_field_ = 0;
    int backup_index_ = 0;
    Settings settings_{};
    Settings draft_{};
    SavedKey saved_keys_[kMaxSavedKeys] = {};
    int saved_key_count_ = 0;
    CodeItem codes_[kMaxCodes] = {};
    int code_count_ = 0;
    int code_index_ = 0;
    int revealed_index_ = -1;
    int code_seconds_left_ = 0;
    int code_period_ = 30;
    CodesState codes_state_ = CodesState::Loading;
    HealthItem health_[kMaxHealth] = {};
    int health_count_ = 0;
    int health_index_ = 0;
    char firmware_[32] = {};
    Action action_{};
    char status_[64] = {};
    StatusBar bar_{};
    std::uint64_t now_ = 0;      // last time seen, for the confirmation countdown
};

}  // namespace cardputer
