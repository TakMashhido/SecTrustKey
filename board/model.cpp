#include "model.h"
#include "about.h"
#include <cstdio>
#include <cstring>

namespace cardputer {
namespace {
constexpr const char* kOpenPgpHeader = "OPENPGP CONFIRM";
const char* const kMainItems[5] = {"Saved Keys", "Codes", "Health", "Settings", "Power Off"};
const char* const kMainFooter = kCredit;   // the home screen carries the credit
const char* const kRequestFooter = "ENT:YES DEL:NO";
const char* const kEditFooter = "ENT:SAVE DEL:BACK";
constexpr int kMainCount = 5;
constexpr int kSettingsCount = 5;   // volume, brightness, timeout, security, about

// Settings > Security & Backup. Every item reboots into the pre-USB maintenance mode.
struct SecurityItem {
    const char* text;
    const char* sub;
    Icon icon;
    Accent accent;
    ActionType action;
};
const SecurityItem kSecurityItems[] = {
    {"Backup to SD", "Writes an encrypted file", Icon::Backup, Accent::Green, ActionType::BackupToSD},
    {"Restore from SD", "Replaces this device's keys", Icon::Sd, Accent::Green, ActionType::RestoreFromSD},
    {"Verify backup", "Checks a backup, changes nothing", Icon::Check, Accent::Green, ActionType::VerifyBackup},
    {"SD card info", "Free space and backup files", Icon::Info, Accent::Blue, ActionType::SdInfo},
    {"Passphrase", "Set, change or remove the startup lock", Icon::Lock, Accent::Violet, ActionType::PassphraseSetup},
};
constexpr int kSecurityCount = static_cast<int>(sizeof(kSecurityItems) / sizeof(kSecurityItems[0]));

bool pressed(bool now, bool before) {
    return now && !before;
}

void copy_text(char* out, std::size_t cap, const char* text) {
    if (cap == 0) {
        return;
    }
    std::size_t i = 0;
    if (text != nullptr) {
        for (; text[i] != '\0' && i + 1 < cap; ++i) {
            out[i] = text[i];
        }
    }
    out[i] = '\0';
}

int clamp_index(int value, int count) {
    if (count <= 0) {
        return 0;
    }
    if (value < 0) {
        return 0;
    }
    if (value >= count) {
        return count - 1;
    }
    return value;
}
}  // namespace

void Model::start(std::uint64_t now) {
    last_activity_ = now;
    request_start_ = now;
    previous_ = {};
    screen_on_ = true;
    blocked_until_release_ = false;
    pending_ = false;
    decision_ = Decision::None;
    decision_generation_ = 0;
    active_generation_ = 0;
    screen_ = Screen::MainMenu;
    main_index_ = 0;
    key_index_ = 0;
    settings_index_ = 0;
    edit_field_ = 0;
    backup_index_ = 0;
    saved_key_count_ = 0;
    action_ = {};
    status_[0] = '\0';
    status_since_ = 0;
}

void Model::set_screen_timeout(std::uint64_t milliseconds) {
    screen_timeout_ = milliseconds;
}

void Model::note_activity(std::uint64_t now) {
    last_activity_ = now;
}

void Model::tick(std::uint64_t now) {
    now_ = now;
    if (pending_ && now - request_start_ >= request_timeout_) {
        finish(Decision::TimedOut, now);
    }
    if (status_[0] != '\0' && now - status_since_ >= 3000) {
        status_[0] = '\0';
    }
    if (!pending_ && !decision_pending() && screen_timeout_ != 0 &&
        now - last_activity_ >= screen_timeout_) {
        screen_on_ = false;
    }
}

void Model::set_settings(const Settings& settings) {
    settings_ = settings;
    draft_ = settings;
    if (screen_timeout_ != screen_timeouts_ms[settings.timeout_index]) {
        screen_timeout_ = screen_timeouts_ms[settings.timeout_index];
    }
}

void Model::set_saved_keys(const SavedKey* keys, int count) {
    if (count < 0) {
        count = 0;
    }
    if (count > kMaxSavedKeys) {
        count = kMaxSavedKeys;
    }
    if (screen_ == Screen::KeyDetails || screen_ == Screen::ConfirmDeleteKey) {
        // A refresh can reorder or remove entries. Never retarget an open
        // detail/confirmation screen merely because its row index was reused.
        const SavedKey* selected = saved_key(key_index_);
        int retained = -1;
        if (selected != nullptr && keys != nullptr &&
            selected->id_len <= sizeof(selected->id)) {
            for (int i = 0; i < count; ++i) {
                if (selected->id_len == keys[i].id_len &&
                    std::memcmp(selected->id, keys[i].id, selected->id_len) == 0) {
                    retained = i;
                    break;
                }
            }
        }
        if (retained < 0) {
            screen_ = Screen::SavedKeys;
        } else {
            key_index_ = retained;
        }
    }
    saved_key_count_ = count;
    if (keys != nullptr) {
        for (int i = 0; i < saved_key_count_; ++i) {
            saved_keys_[i] = keys[i];
        }
    } else {
        saved_key_count_ = 0;
    }
    key_index_ = clamp_index(key_index_, saved_key_count_);
}

const SavedKey* Model::saved_key(int index) const {
    if (index < 0 || index >= saved_key_count_) {
        return nullptr;
    }
    return &saved_keys_[index];
}

void Model::set_usb_mounted(bool mounted) {
    usb_mounted_ = mounted;
}

void Model::set_status(const char* text, std::uint64_t now) {
    copy_text(status_, sizeof(status_), text);
    status_since_ = now;
}

bool Model::take_action(Action* out) {
    if (action_.type == ActionType::None || out == nullptr) {
        return false;
    }
    *out = action_;
    action_ = {};
    return true;
}

void Model::key_snapshot(Keys keys, std::uint64_t now) {
    now_ = now;
    // Callers must supply the complete physical state, including modifiers.
    keys.any = keys.any || keys.enter || keys.del || keys.up || keys.down ||
               keys.left || keys.right || keys.modifier;
    if (keys.any) {
        note_activity(now);
    }
    if (!screen_on_ && keys.any) {
        screen_on_ = true;
        blocked_until_release_ = true;
    }
    if (blocked_until_release_) {
        previous_ = keys;
        if (!keys.any) {
            blocked_until_release_ = false;
        }
        return;
    }
    if (!pending_ && decision_pending()) {
        // Keep the overlay and input ownership until the protocol consumes
        // this decision, including the handoff between the two reset stages.
        previous_ = keys;
        return;
    }
    if (pending_) {
        // Cancellation wins when Enter and Del arrive in the same snapshot.
        if (pressed(keys.del, previous_.del)) {
            finish(Decision::Cancel, now);
        } else if (pressed(keys.enter, previous_.enter)) {
            finish(Decision::Confirm, now);
        }
        previous_ = keys;
        return;
    }
    navigate(keys);
    previous_ = keys;
}

void Model::navigate(Keys keys) {
    const bool up = pressed(keys.up, previous_.up);
    const bool down = pressed(keys.down, previous_.down);
    const bool left = pressed(keys.left, previous_.left);
    const bool right = pressed(keys.right, previous_.right);
    const bool enter = pressed(keys.enter, previous_.enter);
    const bool del = pressed(keys.del, previous_.del);

    switch (screen_) {
        case Screen::MainMenu:
            if (up || left) {
                main_index_ = (main_index_ + kMainCount - 1) % kMainCount;
            } else if (down || right) {
                main_index_ = (main_index_ + 1) % kMainCount;
            } else if (enter) {
                switch (main_index_) {
                    case 0:
                        screen_ = Screen::SavedKeys;
                        action_.type = ActionType::OpenSavedKeys;
                        break;
                    case 1:
                        screen_ = Screen::Codes;
                        codes_state_ = CodesState::Loading;
                        code_count_ = 0;
                        code_index_ = 0;
                        revealed_index_ = -1;
                        action_.type = ActionType::OpenCodes;
                        break;
                    case 2:
                        screen_ = Screen::Health;
                        health_index_ = 0;
                        health_count_ = 0;
                        action_.type = ActionType::OpenHealth;
                        break;
                    case 3:
                        screen_ = Screen::Settings;
                        break;
                    default:
                        action_.type = ActionType::PowerOff;
                        break;
                }
            }
            break;

        case Screen::Health:
            if (health_count_ > 0) {
                if (up) {
                    health_index_ = (health_index_ + health_count_ - 1) % health_count_;
                } else if (down) {
                    health_index_ = (health_index_ + 1) % health_count_;
                } else if (enter) {
                    screen_ = Screen::HealthDetail;
                }
            }
            if (del) {
                screen_ = Screen::MainMenu;
            }
            break;

        case Screen::HealthDetail:
            if (del || enter) {
                screen_ = Screen::Health;
            }
            break;

        case Screen::About:
            if (del || enter) {
                screen_ = Screen::Settings;
            }
            break;

        case Screen::Codes:
            if (code_count_ > 0) {
                if (up) {
                    code_index_ = (code_index_ + code_count_ - 1) % code_count_;
                } else if (down) {
                    code_index_ = (code_index_ + 1) % code_count_;
                } else if (enter && codes_[code_index_].touch) {
                    revealed_index_ = code_index_;
                }
            }
            if (del) {
                screen_ = Screen::MainMenu;
                revealed_index_ = -1;
                code_count_ = 0;      // forget the codes as soon as the screen closes
            }
            break;

        case Screen::SavedKeys:
            if (saved_key_count_ > 0) {
                if (up) {
                    key_index_ = (key_index_ + saved_key_count_ - 1) % saved_key_count_;
                } else if (down) {
                    key_index_ = (key_index_ + 1) % saved_key_count_;
                } else if (enter) {
                    screen_ = Screen::KeyDetails;
                }
            }
            if (del) {
                screen_ = Screen::MainMenu;
            }
            break;

        case Screen::KeyDetails:
            if (enter) {
                screen_ = Screen::ConfirmDeleteKey;
            } else if (del) {
                screen_ = Screen::SavedKeys;
            }
            break;

        case Screen::ConfirmDeleteKey: {
            const SavedKey* key = saved_key(key_index_);
            if (del) {
                screen_ = Screen::KeyDetails;
            } else if (enter && key != nullptr) {
                action_.type = ActionType::DeleteKey;
                action_.key = *key;
                screen_ = Screen::KeyDetails;
            }
            break;
        }

        case Screen::Settings:
            if (up) {
                settings_index_ = (settings_index_ + kSettingsCount - 1) % kSettingsCount;
            } else if (down) {
                settings_index_ = (settings_index_ + 1) % kSettingsCount;
            } else if (enter) {
                if (settings_index_ == 3) {
                    screen_ = Screen::Backup;
                } else if (settings_index_ == 4) {
                    screen_ = Screen::About;
                } else {
                    draft_ = settings_;
                    edit_field_ = settings_index_;
                    screen_ = Screen::SettingsEdit;
                }
            } else if (del) {
                screen_ = Screen::MainMenu;
            }
            break;

        case Screen::Backup:
            if (del) {
                screen_ = Screen::Settings;
            } else if (up) {
                backup_index_ = (backup_index_ + kSecurityCount - 1) % kSecurityCount;
            } else if (down) {
                backup_index_ = (backup_index_ + 1) % kSecurityCount;
            } else if (enter) {
                action_.type = kSecurityItems[backup_index_].action;
            }
            break;

        case Screen::SettingsEdit:
            if (left) {
                adjust_setting(-1);
            } else if (right) {
                adjust_setting(1);
            } else if (enter) {
                settings_ = draft_;
                set_screen_timeout(screen_timeouts_ms[settings_.timeout_index]);
                action_.type = ActionType::SaveSettings;
                action_.settings = settings_;
                screen_ = Screen::Settings;
            } else if (del) {
                draft_ = settings_;
                screen_ = Screen::Settings;
            }
            break;
    }
}

void Model::adjust_setting(int direction) {
    switch (edit_field_) {
        case 0:
            if (direction < 0 && draft_.volume > 0) {
                draft_.volume = static_cast<std::uint8_t>(draft_.volume - 10);
            } else if (direction > 0 && draft_.volume < 100) {
                draft_.volume = static_cast<std::uint8_t>(draft_.volume + 10);
            }
            break;
        case 1:
            if (direction < 0 && draft_.brightness > 10) {
                draft_.brightness = static_cast<std::uint8_t>(draft_.brightness - 10);
            } else if (direction > 0 && draft_.brightness < 100) {
                draft_.brightness = static_cast<std::uint8_t>(draft_.brightness + 10);
            }
            break;
        default:
            if (direction < 0 && draft_.timeout_index > 0) {
                draft_.timeout_index--;
            } else if (direction > 0 && static_cast<std::size_t>(draft_.timeout_index) + 1 <
                                          screen_timeouts_ms.size()) {
                draft_.timeout_index++;
            }
            break;
    }
}

void Model::set_line(Line* line, const char* text, bool selected) const {
    copy_text(line->text, sizeof(line->text), text);
    line->selected = selected;
}

Snapshot Model::view() const {
    Snapshot out;
    out.screen_on = screen_on_;
    out.status = status_;
    out.bar = bar_;
    out.bar.usb = usb_mounted_;
    if (!screen_on_) {
        return out;
    }
    if (pending_ || decision_pending()) {
        out.layout = Layout::Prompt;
        out.icon = request_.kind == RequestKind::Reset ? Icon::Trash : Icon::Touch;
        out.accent = request_.kind == RequestKind::Reset ? Accent::Red : Accent::Teal;
        out.header = request_.kind == RequestKind::Reset ? "FIDO RESET" : "FIDO CONFIRM";
        if (request_timeout_ > 0) {
            // Once the request has ended (answered or timed out) the bar is simply empty.
            const std::uint64_t elapsed = now_ > request_start_ ? now_ - request_start_ : 0;
            out.progress = !pending_ || elapsed >= request_timeout_
                               ? 0
                               : static_cast<int>(100 - elapsed * 100 / request_timeout_);
        }
        switch (request_.kind) {
            case RequestKind::MakeCredential:
                out.title = "Create passkey?";
                break;
            case RequestKind::GetAssertion:
                out.title = "Sign in?";
                break;
            case RequestKind::Exclusion:
                out.title = "Check existing key?";
                break;
            case RequestKind::Reset:
                out.title = request_.stage >= 2 ? "Confirm deletion" : "Delete all keys?";
                break;
            case RequestKind::Selection:
                out.title = "Select an account";
                break;
            case RequestKind::OpenPgpSign:
                out.title = "Sign data?";
                out.header = kOpenPgpHeader;
                out.accent = Accent::Violet;
                break;
            case RequestKind::OpenPgpDecrypt:
                out.title = "Decrypt data?";
                out.header = kOpenPgpHeader;
                out.accent = Accent::Violet;
                break;
            case RequestKind::OpenPgpAuth:
                out.title = "Authenticate?";
                out.header = kOpenPgpHeader;
                out.accent = Accent::Violet;
                break;
            default:
                out.title = "Confirm security key?";
                break;
        }
        out.row_count = 0;
        if (request_.rp_id[0] != '\0') {
            set_line(&out.rows[out.row_count++], request_.rp_id, false);
            out.rows[out.row_count - 1].clip = Clip::Head;
        }
        if (request_.account[0] != '\0') {
            set_line(&out.rows[out.row_count++], request_.account, false);
            out.rows[out.row_count - 1].clip = Clip::Middle;
        }
        if (request_.kind == RequestKind::Reset && request_.stage >= 2) {
            out.subtitle = "This cannot be undone";
        }
        out.footer = kRequestFooter;
        return out;
    }

    switch (screen_) {
        case Screen::MainMenu: {
            out.layout = Layout::Home;
            out.title = "SecTrustKey";
            out.tile_count = kMainCount;
            out.tile_selected = main_index_;
            static const Icon glyphs[kMainCount] = {Icon::Key, Icon::Codes, Icon::Shield,
                                                    Icon::Settings, Icon::Power};
            static const Accent accents[kMainCount] = {Accent::Teal, Accent::Rose, Accent::Green,
                                                       Accent::Blue, Accent::Red};
            for (int i = 0; i < kMainCount; ++i) {
                copy_text(out.tiles[i].label, sizeof(out.tiles[i].label), kMainItems[i]);
                out.tiles[i].glyph = glyphs[i];
                out.tiles[i].accent = accents[i];
            }
            std::snprintf(out.tiles[0].hint, sizeof(out.tiles[0].hint), "%d on device",
                          saved_key_count_);
            copy_text(out.tiles[1].hint, sizeof(out.tiles[1].hint), "TOTP on screen");
            copy_text(out.tiles[2].hint, sizeof(out.tiles[2].hint), "security check");
            copy_text(out.tiles[3].hint, sizeof(out.tiles[3].hint), "sound, screen");
            copy_text(out.tiles[4].hint, sizeof(out.tiles[4].hint), "turn off");
            out.footer = kMainFooter;
            break;
        }
        case Screen::Health: {
            static char title[24];
            int good = 0;
            int scored = 0;
            for (int i = 0; i < health_count_; ++i) {
                if (health_[i].level != HealthLevel::Info) {
                    ++scored;
                    good += health_[i].level == HealthLevel::Good ? 1 : 0;
                }
            }
            if (health_count_ == 0) {
                std::snprintf(title, sizeof(title), "Health");
            } else {
                std::snprintf(title, sizeof(title), "Health %d/%d", good, scored);
            }
            out.title = title;
            out.accent = good == scored ? Accent::Green : Accent::Amber;
            out.footer = "ENT:WHY DEL:BACK";
            if (health_count_ == 0) {
                set_line(&out.rows[0], "Checking", true);
                out.rows[0].icon = Icon::Shield;
                out.row_count = 1;
                break;
            }
            const int visible = 4;
            int first = health_index_ - visible + 1;
            if (first < 0) {
                first = 0;
            }
            for (int i = 0; i < visible && first + i < health_count_; ++i) {
                const HealthItem& item = health_[first + i];
                Line& line = out.rows[out.row_count++];
                set_line(&line, item.label, first + i == health_index_);
                copy_text(line.value, sizeof(line.value), item.value);
                line.icon = item.level == HealthLevel::Good ? Icon::Check
                            : item.level == HealthLevel::Warn ? Icon::Warn : Icon::Info;
                line.accent = item.level == HealthLevel::Good ? Accent::Green
                              : item.level == HealthLevel::Warn ? Accent::Amber : Accent::Blue;
            }
            break;
        }
        case Screen::About:
            out.layout = Layout::About;
            out.title = "About";
            out.accent = Accent::Blue;
            set_line(&out.rows[out.row_count++], kAboutWebsite, false);
            set_line(&out.rows[out.row_count++], kAboutEmail, false);
            set_line(&out.rows[out.row_count++], kAboutRepo[0] != '\0' ? kAboutRepo : "GitHub: coming soon", false);
            copy_text(out.value, sizeof(out.value), firmware_[0] ? firmware_ : "SecTrustKey");
            out.footer = "DEL:BACK";
            break;
        case Screen::HealthDetail: {
            out.layout = Layout::Details;
            const HealthItem* item = health_index_ < health_count_ ? &health_[health_index_] : nullptr;
            out.title = item ? item->label : "Health";
            out.accent = item && item->level == HealthLevel::Warn ? Accent::Amber : Accent::Green;
            if (item) {
                set_line(&out.rows[out.row_count++], item->value, false);
                out.subtitle = item->advice;   // full width: advice is a command or a menu path
                out.icon = item->level == HealthLevel::Good ? Icon::Check
                           : item->level == HealthLevel::Warn ? Icon::Warn : Icon::Info;
            }
            out.footer = "DEL:BACK";
            break;
        }
        case Screen::Codes: {
            out.title = "Codes";
            out.accent = Accent::Rose;
            out.footer = "ENT:SHOW DEL:BACK";
            const char* message = nullptr;
            const char* detail = "";
            switch (codes_state_) {
                case CodesState::Loading:
                    message = "Loading codes"; break;
                case CodesState::TimeUnknown:
                    message = "Time not set";
                    detail = "Run stk-time on your computer";
                    break;
                case CodesState::Protected:
                    message = "Codes are password-protected";
                    detail = "Use your authenticator app";
                    break;
                case CodesState::Empty:
                    message = "No TOTP accounts yet";
                    detail = "Add them with ykman or Yubico Authenticator";
                    break;
                case CodesState::Error:
                    message = "Could not read codes"; break;
                case CodesState::Ready:
                    break;
            }
            if (message != nullptr) {
                set_line(&out.rows[0], message, false);
                out.rows[0].icon = codes_state_ == CodesState::TimeUnknown ? Icon::Clock
                                   : codes_state_ == CodesState::Protected ? Icon::Lock
                                                                          : Icon::Info;
                out.rows[0].accent = Accent::Rose;
                copy_text(out.rows[0].sub, sizeof(out.rows[0].sub), detail);
                out.rows[0].selected = true;
                out.row_count = 1;
                out.footer = "DEL:BACK";
                break;
            }
            const int visible = 4;
            int first = code_index_ - visible + 1;
            if (first < 0) {
                first = 0;
            }
            for (int i = 0; i < visible && first + i < code_count_; ++i) {
                const CodeItem& item = codes_[first + i];
                Line& line = out.rows[out.row_count++];
                // "Issuer:account": the issuer is what people scan for, so it leads the row
                // and the account becomes the detail line.
                const char* colon = std::strchr(item.name, ':');
                char issuer[65] = {};
                if (colon != nullptr && colon != item.name && colon[1] != '\0') {
                    const std::size_t n = static_cast<std::size_t>(colon - item.name);
                    std::memcpy(issuer, item.name, n < sizeof(issuer) - 1 ? n : sizeof(issuer) - 1);
                    copy_text(line.sub, sizeof(line.sub), colon + 1);
                } else {
                    copy_text(issuer, sizeof(issuer), item.name[0] != '\0' ? item.name : "Unnamed");
                }
                set_line(&line, issuer, first + i == code_index_);
                line.clip = Clip::Tail;
                line.icon = item.touch ? Icon::Touch : Icon::Codes;
                line.accent = Accent::Rose;
                line.mono = true;
                if (!item.totp) {
                    copy_text(line.value, sizeof(line.value), "HOTP");
                    line.mono = false;
                } else if (item.touch && !code_revealed(first + i)) {
                    copy_text(line.value, sizeof(line.value), "*** ***");
                } else {
                    copy_text(line.value, sizeof(line.value), item.code);
                }
            }
            if (code_period_ > 0 && code_seconds_left_ > 0) {
                out.progress = code_seconds_left_ * 100 / code_period_;
            }
            break;
        }
        case Screen::SavedKeys:
            out.title = "Saved Keys";
            if (saved_key_count_ == 0) {
                set_line(&out.rows[0], "No saved keys", false);
                out.row_count = 1;
            } else {
                const int visible = 3;
                int first = key_index_ - visible + 1;
                if (first < 0) {
                    first = 0;
                }
                if (first > saved_key_count_ - visible && saved_key_count_ > visible) {
                    first = saved_key_count_ - visible;
                }
                for (int i = 0; i < visible && first + i < saved_key_count_; ++i) {
                    const SavedKey& key = saved_keys_[first + i];
                    const char* site = key.protected_key ? "Protected key" : key.rp_id;
                    const char* who = key.protected_key
                                          ? "Details are hidden"
                                          : (key.account[0] != '\0' ? key.account
                                                                   : "Unnamed account");
                    set_line(&out.rows[out.row_count++], site[0] != '\0' ? site : who,
                             first + i == key_index_);
                    Line& line = out.rows[out.row_count - 1];
                    line.clip = key.protected_key ? Clip::Tail : Clip::Head;
                    copy_text(line.sub, sizeof(line.sub), who);
                    line.icon = key.protected_key ? Icon::Lock : Icon::Key;
                }
            }
            out.footer = "ENT:OPEN DEL:BACK";
            break;
        case Screen::KeyDetails: {
            out.layout = Layout::Details;
            out.title = "Saved Key";
            const SavedKey* key = saved_key(key_index_);
            if (key != nullptr) {
                set_line(&out.rows[out.row_count++], key->rp_id, false);
                out.rows[out.row_count - 1].clip = Clip::Head;
                set_line(&out.rows[out.row_count++],
                         key->protected_key
                             ? "Protected key"
                             : (key->account[0] != '\0' ? key->account : "Unnamed account"),
                         false);
                out.rows[out.row_count - 1].clip = Clip::Middle;
                out.subtitle = "Press Enter to delete this key";
                out.icon = key->protected_key ? Icon::Lock : Icon::Key;
            }
            out.footer = "ENT:DEL DEL:BACK";
            break;
        }
        case Screen::ConfirmDeleteKey: {
            out.layout = Layout::Prompt;
            out.icon = Icon::Trash;
            out.accent = Accent::Red;
            out.header = "DELETE KEY";
            out.title = "Delete key?";
            out.subtitle = "This cannot be undone";
            const SavedKey* key = saved_key(key_index_);
            if (key != nullptr) {
                set_line(&out.rows[out.row_count++], key->rp_id, false);
                out.rows[out.row_count - 1].clip = Clip::Head;
                set_line(&out.rows[out.row_count++],
                         key->account[0] != '\0' ? key->account : "Unnamed account", false);
                out.rows[out.row_count - 1].clip = Clip::Middle;
            }
            out.footer = kRequestFooter;
            break;
        }
        case Screen::Settings: {
            out.title = "Settings";
            out.accent = Accent::Blue;
            Line all[kSettingsCount];
            char buffer[24];
            set_line(&all[0], "Volume", false);
            std::snprintf(buffer, sizeof(buffer), "%u", settings_.volume);
            copy_text(all[0].value, sizeof(all[0].value), buffer);
            all[0].icon = Icon::Volume;
            set_line(&all[1], "Brightness", false);
            std::snprintf(buffer, sizeof(buffer), "%u", settings_.brightness);
            copy_text(all[1].value, sizeof(all[1].value), buffer);
            all[1].icon = Icon::Sun;
            set_line(&all[2], "Screen timeout", false);
            copy_text(all[2].value, sizeof(all[2].value), screen_timeout_labels[settings_.timeout_index]);
            all[2].icon = Icon::Clock;
            set_line(&all[3], "Security & Backup", false);
            copy_text(all[3].sub, sizeof(all[3].sub), "Passphrase, SD backup and SD tools");
            all[3].icon = Icon::Shield;
            set_line(&all[4], "About", false);
            copy_text(all[4].sub, sizeof(all[4].sub), "Developer, website and contact");
            all[4].icon = Icon::Info;
            for (int i = 0; i < kSettingsCount; ++i) {
                all[i].accent = Accent::Blue;
            }
            all[3].accent = Accent::Green;
            const int visible = 4;
            int first = settings_index_ - visible + 1;
            if (first < 0) {
                first = 0;
            }
            for (int i = 0; i < visible && first + i < kSettingsCount; ++i) {
                out.rows[out.row_count] = all[first + i];
                out.rows[out.row_count].selected = first + i == settings_index_;
                ++out.row_count;
            }
            out.footer = "ENT:OK DEL:BACK";
            break;
        }
        case Screen::Backup: {
            out.title = "Security & Backup";
            out.accent = Accent::Green;
            // Three rows at a time leave room for the selected item's explanation.
            const int visible = 3;
            int first = backup_index_ - visible + 1;
            if (first < 0) {
                first = 0;
            }
            for (int i = 0; i < visible && first + i < kSecurityCount; ++i) {
                const SecurityItem& item = kSecurityItems[first + i];
                Line& line = out.rows[out.row_count++];
                set_line(&line, item.text, first + i == backup_index_);
                line.icon = item.icon;
                line.accent = item.accent;
                copy_text(line.sub, sizeof(line.sub), item.sub);
            }
            out.footer = "ENT:OK DEL:BACK";
            break;
        }
        case Screen::SettingsEdit: {
            out.layout = Layout::Editor;
            static const Icon icons[3] = {Icon::Volume, Icon::Sun, Icon::Clock};
            out.icon = icons[edit_field_];
            out.accent = Accent::Blue;
            static const char* const labels[3] = {"Volume", "Brightness", "Screen Timeout"};
            out.title = "Settings";
            out.row_count = 1;
            set_line(&out.rows[0], labels[edit_field_], true);
            if (edit_field_ < 2) {
                const unsigned value = edit_field_ == 0 ? draft_.volume : draft_.brightness;
                std::snprintf(out.value, sizeof(out.value), "%u%%", value);
                out.meter = static_cast<int>(value);
            } else {
                copy_text(out.value, sizeof(out.value), screen_timeout_labels[draft_.timeout_index]);
            }
            out.footer = kEditFooter;
            break;
        }
    }
    return out;
}

std::uint32_t Model::begin_request(std::uint64_t now, std::uint64_t timeout_ms) {
    RequestInfo info;
    return begin_request(now, timeout_ms, info);
}

std::uint32_t Model::begin_request(std::uint64_t now, std::uint64_t timeout_ms,
                                   const RequestInfo& info) {
    if (pending_) {
        finish(Decision::Cancel, now);
    }
    request_ = info;
    request_.rp_id[sizeof(request_.rp_id) - 1] = '\0';
    request_.account[sizeof(request_.account) - 1] = '\0';
    active_generation_ = next_generation_++;
    if (next_generation_ == 0) {
        next_generation_ = 1;
    }
    pending_ = true;
    screen_on_ = true;
    decision_ = Decision::None;
    decision_generation_ = 0;
    request_start_ = now;
    now_ = now;
    request_timeout_ = timeout_ms;
    last_activity_ = now;
    blocked_until_release_ = blocked_until_release_ || previous_.any;
    return active_generation_;
}

void Model::finish(Decision decision, std::uint64_t now) {
    pending_ = false;
    decision_ = decision;
    decision_generation_ = active_generation_;
    last_activity_ = now;
}

void Model::cancel_request(std::uint64_t now) {
    // A key press is not committed until the protocol consumes its decision.
    if (pending_ || decision_ == Decision::Confirm) {
        finish(Decision::Cancel, now);
    }
}

void Model::set_firmware_label(const char* label) {
    copy_text(firmware_, sizeof(firmware_), label);
}

void Model::set_health(const HealthItem* items, int count) {
    if (items == nullptr || count < 0) {
        count = 0;
    }
    health_count_ = count > kMaxHealth ? kMaxHealth : count;
    for (int i = 0; i < health_count_; ++i) {
        health_[i] = items[i];
    }
    if (health_index_ >= health_count_) {
        health_index_ = health_count_ > 0 ? health_count_ - 1 : 0;
    }
}

void Model::set_codes(CodesState state, const CodeItem* items, int count, int seconds_left,
                      int period) {
    codes_state_ = state;
    if (items == nullptr || count < 0) {
        count = 0;
    }
    code_count_ = count > kMaxCodes ? kMaxCodes : count;
    for (int i = 0; i < code_count_; ++i) {
        codes_[i] = items[i];
    }
    if (code_index_ >= code_count_) {
        code_index_ = code_count_ > 0 ? code_count_ - 1 : 0;
    }
    if (revealed_index_ >= code_count_) {
        revealed_index_ = -1;
    }
    code_seconds_left_ = seconds_left;
    code_period_ = period > 0 ? period : 30;
}

Decision Model::take_decision() {
    const auto result = decision_;
    decision_ = Decision::None;
    return result;
}

}  // namespace cardputer
