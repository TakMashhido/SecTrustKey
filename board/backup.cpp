#include "backup.h"
#include "dialog.h"
#include "backup_codec.h"
#include "backup_password.h"
#include "backup_store.h"
#include "flash_layout.h"
#include "key_store_esp32.h"
#include "lock_policy.h"
#include "esp_timer.h"
#include "mbedtls/pkcs5.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"
extern "C" {
#include "serial.h"
}

namespace cardputer {
namespace {
constexpr char kMount[] = "/sdcard";
// Everything SecTrustKey writes lives in one folder, created on first use.
constexpr char kDir[] = "/sdcard/SecTrustKey";
constexpr char kFile[] = "/sdcard/SecTrustKey/backup.stk";
constexpr char kPrevious[] = "/sdcard/SecTrustKey/backup-previous.stk";
constexpr char kTemporary[] = "/sdcard/SecTrustKey/backup-unfinished.tmp";
// Where earlier versions kept them (8.3 names in the card root). Moved into the folder.
constexpr char kLegacyFile[] = "/sdcard/FIDOBAK.CFB";
constexpr char kLegacyPrevious[] = "/sdcard/FIDOBAK.OLD";
constexpr char kLegacyTemporary[] = "/sdcard/FIDOBAK.TMP";
constexpr char kNamespace[] = "fido_backup";
constexpr auto kHost = SPI2_HOST;
bool mounted = false;
sdmmc_card_t* mounted_card = nullptr;
bool sd_teardown_failed = false;

bool state_read(const char* key, std::uint8_t& out) {
    out = 0;
    if (nvs_flash_init() != ESP_OK) { return false; }
    nvs_handle_t handle;
    esp_err_t result = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) { return true; }
    if (result != ESP_OK) { return false; }
    result = nvs_get_u8(handle, key, &out);
    nvs_close(handle);
    return result == ESP_OK || result == ESP_ERR_NVS_NOT_FOUND;
}

bool state_write(const char* key, std::uint8_t value) {
    if (nvs_flash_init() != ESP_OK) { return false; }
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) { return false; }
    esp_err_t result = nvs_set_u8(handle, key, value);
    if (result == ESP_OK) { result = nvs_commit(handle); }
    nvs_close(handle);
    std::uint8_t check = 255;
    return result == ESP_OK && state_read(key, check) && check == value;
}

bool mount_sd() {
    if (mounted) { return true; }
    // Cardputer ADV official pinmap: CS12, MOSI14, CLK40, MISO39.
    // The LCD uses SPI3, so this dedicated SPI2 bus is not shared with it.
    spi_bus_config_t bus = {};
    bus.mosi_io_num = GPIO_NUM_14;
    bus.miso_io_num = GPIO_NUM_39;
    bus.sclk_io_num = GPIO_NUM_40;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = CF_BACKUP_CHUNK + 16;
    if (spi_bus_initialize(kHost, &bus, SPI_DMA_CH_AUTO) != ESP_OK) { return false; }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = kHost;
    host.max_freq_khz = 10000;
    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id = kHost;
    device.gpio_cs = GPIO_NUM_12;
    esp_vfs_fat_sdmmc_mount_config_t config = {};
    config.format_if_mount_failed = false;
    config.max_files = 3;
    config.allocation_unit_size = 4096;
    if (esp_vfs_fat_sdspi_mount(kMount, &host, &device, &config, &mounted_card) != ESP_OK) {
        spi_bus_free(kHost);
        return false;
    }
    mounted = true;
    // Create the SecTrustKey folder and move backups made by earlier versions into it.
    // Never overwrite: a file already in the folder wins over an old one in the root.
    struct stat st = {};
    if (stat(kDir, &st) != 0 && mkdir(kDir, 0775) != 0) {
        return true;   // the card is readable; writes will report the problem
    }
    const char* from[3] = {kLegacyFile, kLegacyPrevious, kLegacyTemporary};
    const char* to[3] = {kFile, kPrevious, kTemporary};
    for (int i = 0; i < 3; ++i) {
        if (stat(from[i], &st) == 0 && stat(to[i], &st) != 0) {
            std::rename(from[i], to[i]);
        }
    }
    return true;
}

struct SDSession {
    ~SDSession() {
        if (!mounted) { return; }
        // IDF can free the card/context even when VFS unregistration fails.
        // Never reuse that handle. A failed teardown requires a clean boot
        // before another attempt; any restore guard remains persistent.
        sdmmc_card_t* card = mounted_card;
        mounted = false;
        mounted_card = nullptr;
        const bool unmounted = esp_vfs_fat_sdcard_unmount(kMount, card) == ESP_OK;
        const bool freed = spi_bus_free(kHost) == ESP_OK;
        sd_teardown_failed = sd_teardown_failed || !unmounted || !freed;
    }
};

void pause() {
    vTaskDelay(pdMS_TO_TICKS(10));
}

Keys fresh_keys(Board& board) {
    // Require a full release between screens, including modifier keys.
    do {
        board.poll_input();
        pause();
    } while (board.keys().any);
    for (;;) {
        board.poll_input();
        if (board.keys().any) { return board.keys(); }
        pause();
    }
}

void notice(Board& board, const char* title, const char* line, Mood mood = Mood::Info,
            const char* header = "SD card") {
    board.render(make_dialog(header, title, line, "ENT:OK", mood));
    while (!fresh_keys(board).enter) {}
}

bool confirm(Board& board, const char* title, const char* line, Mood mood = Mood::Warn,
             const char* header = "SD card") {
    board.render(make_dialog(header, title, line, "ENT:YES DEL:NO", mood));
    for (;;) {
        const Keys keys = fresh_keys(board);
        if (keys.del) { return false; }
        if (keys.enter) { return true; }
    }
}

bool enter_password(Board& board, BackupPassword& password, const char* title,
                    std::size_t minimum, bool cancellable = true) {
    password.clear();
    // Swallow the key used to enter this screen, including held printable keys.
    do { board.poll_input(); pause(); } while (board.keys().any);
    Keys previous;
    std::uint64_t last_activity = Board::now_ms();
    for (;;) {
        char hint[40];
        std::snprintf(hint, sizeof(hint), "%u+ characters; longer is safer",
                      static_cast<unsigned>(minimum));
        board.render(make_entry(title, reinterpret_cast<const char*>(password.data()),
                                password.size(), minimum, 64, minimum > 1 ? hint : "",
                                "ENT:OK DEL:ERASE FN+DEL:CANCEL"));
        board.poll_input();
        const Keys keys = board.keys();
        if (keys.any) { last_activity = Board::now_ms(); }
        if (cancellable &&
            ((board.fn_held() && keys.del) || Board::now_ms() - last_activity > 120000)) {
            password.clear();
            return false;
        }
        if (keys.del && !previous.del) {
            password.backspace();
        } else {
            password.append(board.typed());
            if (keys.enter && !previous.enter) {
                if (password.size() >= minimum) { return true; }
                board.play(Tone::Failure);
            }
        }
        previous = keys;
        pause();
    }
}

int read_store(void* ctx, std::uint32_t offset, std::uint8_t* out, std::size_t len) {
    return esp_partition_read(static_cast<const esp_partition_t*>(ctx), offset, out, len) == ESP_OK ? 0 : -1;
}
int program_store(void* ctx, std::uint32_t offset, const std::uint8_t* data, std::size_t len) {
    return esp_partition_write(static_cast<const esp_partition_t*>(ctx), offset, data, len) == ESP_OK ? 0 : -1;
}
int erase_store(void* ctx, std::uint32_t offset, std::size_t len) {
    return esp_partition_erase_range(static_cast<const esp_partition_t*>(ctx), offset, len) == ESP_OK ? 0 : -1;
}
int random_fill(void*, std::uint8_t* out, std::size_t len) {
    esp_fill_random(out, len);
    return 0;
}
int database_not_erased(void*) { return 0; }

bool verify_path(const char* path, const BackupPassword& password) {
    FILE* file = std::fopen(path, "rb");
    if (!file) { return false; }
    const int result = cf_backup_verify(file, password.data(), password.size(), stk_serial_hash);
    const int closed = std::fclose(file);
    return result == CF_BACKUP_OK && closed == 0;
}

// ---- Progress ---------------------------------------------------------------------------
// Key derivation cannot report progress, so its share of the bar is estimated from a rate
// measured on this chip; writing and checking report real bytes.
std::uint32_t pbkdf2_us_per_1000 = 0;

std::uint32_t measure_pbkdf2() {
    if (pbkdf2_us_per_1000 == 0) {
        static const unsigned char kPass[] = "calibration";
        static const unsigned char kSalt[16] = {};
        unsigned char out[32];
        const std::int64_t start = esp_timer_get_time();
        mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, kPass, sizeof(kPass) - 1, kSalt,
                                      sizeof(kSalt), 2000, sizeof(out), out);
        const std::int64_t elapsed = esp_timer_get_time() - start;
        mbedtls_platform_zeroize(out, sizeof(out));
        pbkdf2_us_per_1000 = static_cast<std::uint32_t>(std::max<std::int64_t>(elapsed / 2, 1));
    }
    return pbkdf2_us_per_1000;
}

std::uint32_t pbkdf2_ms(std::uint32_t iterations) {
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(measure_pbkdf2()) * iterations / 1000000u);
}

struct Segment {
    int step = 1;
    const char* detail = "";
    int base = 0;      // where this segment starts on the overall bar
    int weight = 0;    // how much of the bar it covers
};

struct ProgressPlan {
    Board* board = nullptr;
    const char* title = "";
    Accent accent = Accent::Green;
    int steps = 1;
    Segment kdf, data, data_again;   // data_again: a second pass over the file (restore)
    std::uint32_t kdf_ms = 1;
    std::uint32_t last_done = 0;
    bool second_pass = false;
    std::uint64_t last_render = 0;
};
ProgressPlan* active_plan = nullptr;

void plan_show(ProgressPlan& p, const Segment& seg, int percent_of_segment, bool force = false) {
    const std::uint64_t now = Board::now_ms();
    if (!force && now - p.last_render < 60) { return; }
    p.last_render = now;
    const int clamped = std::clamp(percent_of_segment, 0, 100);
    const int overall = seg.base + seg.weight * clamped / 100;
    p.board->render(make_progress(p.title, seg.detail, seg.step, p.steps, overall, p.accent,
                                  static_cast<int>((now / 12) % 100)));
}

void codec_progress(void*, int stage, std::uint32_t done, std::uint32_t total) {
    if (!active_plan) { return; }
    ProgressPlan& p = *active_plan;
    if (stage == CF_BACKUP_STAGE_KDF) {
        plan_show(p, p.kdf, static_cast<int>(std::min<std::uint64_t>(95, done * 100ull / std::max<std::uint32_t>(p.kdf_ms, 1))));
        return;
    }
    if (done < p.last_done) { p.second_pass = true; }
    p.last_done = done;
    const Segment& seg = p.second_pass && p.data_again.weight > 0 ? p.data_again : p.data;
    plan_show(p, seg, total ? static_cast<int>(done * 100ull / total) : 0, done == total);
}

// Runs the codec with `plan` showing progress; the derived key is cached for the plan.
struct PlanScope {
    explicit PlanScope(ProgressPlan& plan) {
        active_plan = &plan;
        plan.kdf_ms = pbkdf2_ms(CF_BACKUP_ITERATIONS);
        cf_backup_set_progress(codec_progress, nullptr);
        cf_backup_key_cache(1);
        plan_show(plan, plan.kdf, 0, true);
    }
    ~PlanScope() {
        cf_backup_key_cache(0);          // wipe the derived key
        cf_backup_set_progress(nullptr, nullptr);
        active_plan = nullptr;
    }
};

// Runs fn(arg) in a worker task while this task animates a time-estimated bar.
struct WorkerJob {
    void (*fn)(void*);
    void* arg;
    SemaphoreHandle_t done;
};

void worker_entry(void* raw) {
    auto* job = static_cast<WorkerJob*>(raw);
    job->fn(job->arg);
    xSemaphoreGive(job->done);
    vTaskDelete(nullptr);
}

void run_with_progress(Board& board, const char* title, const char* detail, Accent accent,
                       std::uint32_t expected_ms, void (*fn)(void*), void* arg) {
    WorkerJob job{fn, arg, xSemaphoreCreateBinary()};
    if (!job.done || xTaskCreate(worker_entry, "stk_work", 8192, &job, 1, nullptr) != pdPASS) {
        if (job.done) { vSemaphoreDelete(job.done); }
        board.render(make_progress(title, detail, 1, 1, -1, accent, 0, "Keep the device powered", "Security"));
        fn(arg);   // fall back to running here, without animation
        return;
    }
    const std::uint64_t start = Board::now_ms();
    while (xSemaphoreTake(job.done, pdMS_TO_TICKS(50)) != pdTRUE) {
        const std::uint64_t elapsed = Board::now_ms() - start;
        const int percent = static_cast<int>(std::min<std::uint64_t>(95, elapsed * 100 / std::max<std::uint32_t>(expected_ms, 1)));
        board.render(make_progress(title, detail, 1, 1, percent, accent,
                                   static_cast<int>((Board::now_ms() / 12) % 100),
                                   "Keep the device powered", "Security"));
    }
    vSemaphoreDelete(job.done);
    board.render(make_progress(title, detail, 1, 1, 100, accent, 0, "Keep the device powered", "Security"));
}

bool save_backup_inner(const BackupPassword& password);

bool save_backup(Board& board, const BackupPassword& password) {
    ProgressPlan plan;
    plan.board = &board;
    plan.title = "Saving backup";
    plan.accent = Accent::Green;
    plan.steps = 4;
    plan.kdf = {1, "Applying your password", 0, 55};
    plan.data = {2, "Writing to the SD card", 55, 25};
    PlanScope scope(plan);
    return save_backup_inner(password);
}

bool save_backup_inner(const BackupPassword& password) {
    // Never replace a good file until the complete new file has been closed,
    // synced, reopened and authenticated. FAT is not power-loss atomic: retain
    // the previous generation and allow its explicit selection on restore.
    FILE* file = std::fopen(kTemporary, "wb");
    if (!file) { return false; }
    int result = cf_backup_export(file, password.data(), password.size(), stk_serial_hash,
                  read_store, const_cast<esp_partition_t*>(cardputer_fido_partition()), random_fill, nullptr);
    if (result == CF_BACKUP_OK && fsync(fileno(file)) != 0) { result = CF_BACKUP_IO; }
    if (std::fclose(file) != 0) { result = CF_BACKUP_IO; }
    if (active_plan) {
        active_plan->data = {3, "Checking the new file", 80, 10};
        active_plan->last_done = 0;
    }
    if (result != CF_BACKUP_OK || !verify_path(kTemporary, password)) { return false; }
    struct stat info;
    if (stat(kFile, &info) == 0) {
        if (std::remove(kPrevious) != 0 && errno != ENOENT) { return false; }
        if (std::rename(kFile, kPrevious) != 0) { return false; }
    } else if (errno != ENOENT) {
        return false;
    }
    if (active_plan) {
        active_plan->data = {4, "Checking it once more", 90, 10};
        active_plan->last_done = 0;
    }
    return std::rename(kTemporary, kFile) == 0 && verify_path(kFile, password);
}

const char* choose_file(Board& board, const char* title = "Restore from SD") {
    int selected = 0;
    for (;;) {
        Snapshot view;
        view.title = title;
        view.accent = Accent::Green;
        view.row_count = 3;
        const char* names[] = {"Latest backup", "Previous backup", "Temporary file"};
        const char* notes[] = {"Most recent verified copy", "The copy before the latest",
                               "Unfinished; use only if needed"};
        for (int i = 0; i < 3; ++i) {
            std::snprintf(view.rows[i].text, sizeof(view.rows[i].text), "%s", names[i]);
            std::snprintf(view.rows[i].sub, sizeof(view.rows[i].sub), "%s", notes[i]);
            view.rows[i].selected = selected == i;
            view.rows[i].icon = Icon::Sd;
            view.rows[i].accent = i == 2 ? Accent::Amber : Accent::Green;
        }
        view.footer = "ENT:OK DEL:BACK";
        board.render(view);
        const Keys keys = fresh_keys(board);
        if (keys.del) { return nullptr; }
        if (keys.up) { selected = (selected + 2) % 3; }
        else if (keys.down) { selected = (selected + 1) % 3; }
        else if (keys.enter) {
            const char* paths[] = {kFile, kPrevious, kTemporary};
            return paths[selected];
        }
    }
}

bool restore_backup(Board& board, const char* path, const BackupPassword& password) {
    FILE* file = std::fopen(path, "rb");
    if (!file) { notice(board, "Restore failed", "File not found", Mood::Bad); return false; }
    ProgressPlan plan;
    plan.board = &board;
    plan.title = "Restoring backup";
    plan.accent = Accent::Amber;
    plan.steps = 4;
    plan.kdf = {1, "Checking your password", 0, 45};
    plan.data = {2, "Checking the backup", 45, 20};
    PlanScope scope(plan);
    int result = cf_backup_verify(file, password.data(), password.size(), stk_serial_hash);
    if (result != CF_BACKUP_OK) {
        std::fclose(file);
        notice(board, "Backup not accepted", "Wrong password, damaged file or other device", Mood::Bad);
        return false;
    }
    if (!confirm(board, "Replace all keys?", "The device returns to this backup", Mood::Bad)) {
        std::fclose(file);
        return false;
    }
    plan.data = {3, "Checking it again", 65, 15};
    plan.data_again = {4, "Writing your keys", 80, 20};
    plan.last_done = 0;
    plan.second_pass = false;
    auto* store = static_cast<cf_backup_store*>(std::calloc(1, sizeof(cf_backup_store)));
    if (!store) { std::fclose(file); return false; }
    store->ctx = const_cast<esp_partition_t*>(cardputer_fido_partition());
    store->read = read_store;
    store->program = program_store;
    store->erase = erase_store;
    // Persist AND read back before any destructive operation. It survives
    // partial key commits, including a power cut during the final key pages.
    if (!state_write("pending", 1)) {
        std::fclose(file);
        std::free(store);
        notice(board, "Restore refused", "Cannot set guard", Mood::Bad);
        return false;
    }
    result = cf_backup_restore(file, password.data(), password.size(), stk_serial_hash,
                               cf_backup_store_write, store);
    if (std::fclose(file) != 0) { result = CF_BACKUP_IO; }
    if (result == CF_BACKUP_OK && cf_backup_store_finish(store) != 0) { result = CF_BACKUP_IO; }
    mbedtls_platform_zeroize(store, sizeof(*store));
    std::free(store);
    if (result == CF_BACKUP_OK && state_write("pending", 0)) {
        notice(board, "Keys restored", "Press Enter to reboot", Mood::Good);
        esp_restart();
    }
    notice(board, "Restore incomplete", "Retry; USB stays off", Mood::Bad);
    return false;
}

void run_mode(Board& board, BackupMode mode) {
    if (cardputer_key_store_prepare() != 0) {
        notice(board, "Storage layout error", "See install guide", Mood::Bad);
        return;
    }
    // Export only a valid store; never provision a new identity in maintenance.
    if (mode == BackupMode::Save) {
        cf_key_flash flash = {};
        flash.ctx = const_cast<esp_partition_t*>(cardputer_fido_partition());
        flash.read = read_store;
        flash.program = program_store;
        flash.erase = erase_store;
        flash.database_erased = database_not_erased;
        cf_device_keys keys;
        const auto status = cf_key_store_load(&flash, &keys);
        mbedtls_platform_zeroize(&keys, sizeof(keys));
        // A passphrase-sealed store is healthy: its backup keeps the sealed record, so a
        // restore asks for the passphrase that was set when the backup was taken.
        if (status != CF_KEY_READY && status != CF_KEY_LOCKED) {
            notice(board, "Backup refused", "Store is not healthy", Mood::Bad);
            return;
        }
    }
    if (!mount_sd()) {
        notice(board, "SD unavailable", "Insert FAT32 microSD", Mood::Warn);
        return;
    }
    SDSession session;
    const char* path = mode == BackupMode::Restore ? choose_file(board) : nullptr;
    if (mode == BackupMode::Restore && !path) { return; }
    BackupPassword password;
    if (!enter_password(board, password, "Backup password", mode == BackupMode::Save ? CF_BACKUP_PASSWORD_MIN : 1)) {
        return;
    }
    if (mode == BackupMode::Save) {
        BackupPassword repeated;
        if (!enter_password(board, repeated, "Repeat password", CF_BACKUP_PASSWORD_MIN)) { return; }
        if (!password.equals(repeated)) { notice(board, "Passwords differ", "Backup not changed", Mood::Warn); return; }
        repeated.clear();
        if (!confirm(board, "Save backup to SD?", "Keep power and SD on", Mood::Info)) { return; }
        const bool ok = save_backup(board, password);
        password.clear();
        if (ok) {
            state_write("lastok", 1);   // the dashboard reports that a verified backup exists
        }
        notice(board, ok ? "Backup verified" : "Backup failed", ok ? "Remove SD after reboot" : "Keep previous backup", ok ? Mood::Good : Mood::Bad);
    } else {
        restore_backup(board, path, password);
    }
}
// ---- SD tools -------------------------------------------------------------------------
// Check a backup with its password without changing anything on the device.
void run_verify(Board& board) {
    if (!mount_sd()) {
        notice(board, "SD unavailable", "Insert FAT32 microSD", Mood::Warn);
        return;
    }
    SDSession session;
    const char* path = choose_file(board, "Verify a backup");
    if (!path) { return; }
    struct stat st = {};
    if (stat(path, &st) != 0) {
        notice(board, "No such backup", "Nothing to check", Mood::Warn);
        return;
    }
    BackupPassword password;
    if (!enter_password(board, password, "Backup password", 1)) { return; }
    ProgressPlan plan;
    plan.board = &board;
    plan.title = "Verifying backup";
    plan.accent = Accent::Green;
    plan.steps = 2;
    plan.kdf = {1, "Checking your password", 0, 80};
    plan.data = {2, "Checking every part", 80, 20};
    bool ok;
    {
        PlanScope scope(plan);
        ok = verify_path(path, password);
    }
    password.clear();
    notice(board, ok ? "Backup is good" : "Backup not readable",
           ok ? "Decrypts and checks out" : "Wrong password or damaged", ok ? Mood::Good : Mood::Bad);
}

// Card capacity, free space and which backup files exist. Reads only.
void run_sd_info(Board& board) {
    if (!mount_sd()) {
        notice(board, "SD unavailable", "Insert FAT32 microSD", Mood::Warn);
        return;
    }
    SDSession session;
    std::uint64_t total = 0, free_bytes = 0;
    esp_vfs_fat_info(kMount, &total, &free_bytes);
    const char* paths[3] = {kFile, kPrevious, kTemporary};
    const char* names[3] = {"Latest backup", "Previous backup", "Temporary file"};
    Snapshot view;
    view.title = "SD card";
    view.accent = Accent::Green;
    view.footer = "DEL:BACK";
    std::snprintf(view.rows[0].text, sizeof(view.rows[0].text), "Free space");
    std::snprintf(view.rows[0].value, sizeof(view.rows[0].value), "%llu MB",
                  static_cast<unsigned long long>(free_bytes / (1024 * 1024)));
    std::snprintf(view.rows[0].sub, sizeof(view.rows[0].sub), "of %llu MB",
                  static_cast<unsigned long long>(total / (1024 * 1024)));
    view.rows[0].icon = Icon::Sd;
    view.rows[0].accent = Accent::Green;
    view.row_count = 1;
    for (int i = 0; i < 3; ++i) {
        struct stat st = {};
        const bool present = stat(paths[i], &st) == 0;
        Line& line = view.rows[view.row_count++];
        std::snprintf(line.text, sizeof(line.text), "%s", names[i]);
        if (present) {
            std::snprintf(line.value, sizeof(line.value), "%ld KB", static_cast<long>(st.st_size / 1024));
        } else {
            std::snprintf(line.value, sizeof(line.value), "none");
        }
        line.icon = Icon::Backup;
        line.accent = present ? Accent::Green : Accent::Amber;
    }
    view.rows[0].selected = true;
    board.render(view);
    while (!fresh_keys(board).del) {}
}

// ---- Startup passphrase ---------------------------------------------------------------
constexpr const char* kSecurity = "Security";

bool fail_count(std::uint8_t& out) { return state_read("lockfail", out); }

// Wait out the lockout, showing the remaining time; keys are ignored meanwhile.
void lockout_wait(Board& board, std::uint8_t failures) {
    const std::uint32_t wait = lockout_seconds(failures);
    if (wait == 0) { return; }
    const std::uint64_t start = Board::now_ms();
    const std::uint64_t until = start + static_cast<std::uint64_t>(wait) * 1000u;
    while (Board::now_ms() < until) {
        char line[40];
        std::snprintf(line, sizeof(line), "You can try again in %u s",
                      static_cast<unsigned>((until - Board::now_ms() + 999) / 1000));
        const int percent = static_cast<int>((Board::now_ms() - start) * 100 / (until - start));
        board.render(make_progress("Too many wrong tries", line, 1, 1, percent, Accent::Red,
                                   static_cast<int>((Board::now_ms() / 12) % 100), "Nothing is erased",
                                   "Security"));
        board.poll_input();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// One counted attempt. Returns the store status; the counter is raised before checking.
cf_key_status counted_unlock(Board& board, const cf_key_flash* flash, const BackupPassword& pass,
                             cf_device_keys* out) {
    std::uint8_t failures = 0;
    if (!fail_count(failures) || !state_write("lockfail", failures < 255 ? failures + 1 : 255)) {
        return CF_KEY_IO_ERROR;   // without a durable counter, refuse rather than allow free tries
    }
    struct Job {
        const cf_key_flash* flash;
        const BackupPassword* pass;
        cf_device_keys* out;
        cf_key_status status;
    } job{flash, &pass, out, CF_KEY_IO_ERROR};
    run_with_progress(board, "Unlocking", "Checking your passphrase", Accent::Violet,
                      std::max<std::uint32_t>(pbkdf2_ms(20000), 400),
                      [](void* raw) {
                          auto* j = static_cast<Job*>(raw);
                          j->status = cf_key_store_unlock(j->flash, j->pass->data(), j->pass->size(), j->out);
                      },
                      &job);
    const cf_key_status status = job.status;
    if (status == CF_KEY_READY) {
        state_write("lockfail", 0);
    }
    return status;
}

// PBKDF2 cost that takes about one second on this chip (measured, then clamped).
std::uint32_t calibrate_iterations() {
    static const unsigned char kPass[] = "calibration";
    static const unsigned char kSalt[16] = {};
    unsigned char out[32];
    const std::uint32_t probe = 2000;
    const std::int64_t start = esp_timer_get_time();
    mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, kPass, sizeof(kPass) - 1, kSalt, sizeof(kSalt),
                                  probe, sizeof(out), out);
    const std::int64_t elapsed_us = esp_timer_get_time() - start;
    mbedtls_platform_zeroize(out, sizeof(out));
    std::uint64_t iterations = elapsed_us > 0 ? probe * 1000000ull / static_cast<std::uint64_t>(elapsed_us)
                                              : 200000;
    return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(iterations, 20000, 2000000));
}

bool choose_new_passphrase(Board& board, BackupPassword& pass) {
    if (!enter_password(board, pass, "New passphrase", kPassphraseMin)) { return false; }
    BackupPassword again;
    if (!enter_password(board, again, "Repeat passphrase", kPassphraseMin)) { return false; }
    if (!pass.equals(again)) {
        notice(board, "Passphrases differ", "Nothing was changed", Mood::Warn, kSecurity);
        return false;
    }
    return confirm(board, "Forget it = lose keys", "Keep an SD backup. Continue?", Mood::Warn, kSecurity);
}

void passphrase_setup(Board& board) {
    if (cardputer_key_store_prepare() != 0) {
        notice(board, "Storage layout error", "See install guide", Mood::Bad, kSecurity);
        return;
    }
    cf_key_flash flash = {};
    flash.ctx = const_cast<esp_partition_t*>(cardputer_fido_partition());
    flash.read = read_store;
    flash.program = program_store;
    flash.erase = erase_store;
    flash.database_erased = database_not_erased;
    flash.random = random_fill;
    cf_device_keys keys;
    const cf_key_status current = cf_key_store_load(&flash, &keys);
    mbedtls_platform_zeroize(&keys, sizeof(keys));
    BackupPassword old_pass, new_pass;
    if (current == CF_KEY_READY) {
        if (!confirm(board, "Set a passphrase?", "Asked at every power-up", Mood::Info, kSecurity) ||
            !choose_new_passphrase(board, new_pass)) {
            return;
        }
    } else if (current == CF_KEY_LOCKED) {
        const bool change = confirm(board, "Change passphrase?", "DEL to remove it instead", Mood::Info, kSecurity);
        if (!change && !confirm(board, "Remove passphrase?", "Keys stay, unencrypted", Mood::Warn, kSecurity)) {
            return;
        }
        std::uint8_t failures = 0;
        if (fail_count(failures)) { lockout_wait(board, failures); }
        if (!enter_password(board, old_pass, "Current passphrase", 1)) { return; }
        cf_device_keys check;
        const cf_key_status opened = counted_unlock(board, &flash, old_pass, &check);
        mbedtls_platform_zeroize(&check, sizeof(check));
        if (opened != CF_KEY_READY) {
            notice(board, opened == CF_KEY_WRONG_PASSPHRASE ? "Wrong passphrase" : "Could not open keys",
                   "Nothing was changed", Mood::Bad, kSecurity);
            return;
        }
        if (change && !choose_new_passphrase(board, new_pass)) { return; }
    } else {
        notice(board, "Keys are not healthy", "Nothing was changed", Mood::Bad, kSecurity);
        return;
    }
    const std::uint32_t iterations = new_pass.size() > 0 ? calibrate_iterations() : 0;
    struct Job {
        cf_key_flash* flash;
        BackupPassword* old_pass;
        BackupPassword* new_pass;
        std::uint32_t iterations;
        cf_key_status result;
    } job{&flash, &old_pass, &new_pass, iterations, CF_KEY_IO_ERROR};
    run_with_progress(board, new_pass.size() ? "Saving passphrase" : "Removing passphrase",
                      new_pass.size() ? "Encrypting your keys" : "Decrypting your keys", Accent::Violet,
                      pbkdf2_ms(iterations + 20000) + 300,
                      [](void* raw) {
                          auto* j = static_cast<Job*>(raw);
                          j->result = cf_key_store_change_passphrase(
                              j->flash, j->old_pass->size() ? j->old_pass->data() : nullptr, j->old_pass->size(),
                              j->new_pass->size() ? j->new_pass->data() : nullptr, j->new_pass->size(),
                              j->iterations);
                      },
                      &job);
    const cf_key_status result = job.result;
    const bool removed = new_pass.size() == 0;
    old_pass.clear();
    new_pass.clear();
    if (result == CF_KEY_READY) {
        notice(board, removed ? "Passphrase removed" : "Passphrase saved",
               removed ? "Keys no longer encrypted" : "Asked at next power-up", Mood::Good, kSecurity);
    } else {
        notice(board, "Not changed", "Old passphrase still works", Mood::Bad, kSecurity);
    }
}
}  // namespace

void unlock_store_at_boot(Board& board, const cf_key_flash* flash, cf_device_keys* out) {
    for (;;) {
        std::uint8_t failures = 0;
        if (!fail_count(failures)) {
            notice(board, "Settings unreadable", "Cannot count tries", Mood::Bad, kSecurity);
            continue;
        }
        lockout_wait(board, failures);
        BackupPassword pass;
        enter_password(board, pass, "Unlock SecTrustKey", 1, false);
        const cf_key_status status = counted_unlock(board, flash, pass, out);
        pass.clear();
        if (status == CF_KEY_READY) {
            board.play(Tone::Success);
            return;
        }
        board.play(Tone::Failure);
        notice(board, status == CF_KEY_WRONG_PASSPHRASE ? "Wrong passphrase" : "Could not open keys",
               status == CF_KEY_WRONG_PASSPHRASE ? "Nothing is erased" : "Try again", Mood::Bad, kSecurity);
    }
}

bool backup_verified_once() {
    std::uint8_t value = 0;
    return state_read("lastok", value) && value == 1;
}

bool request_backup_mode(BackupMode mode) {
    return (mode == BackupMode::Save || mode == BackupMode::Restore ||
            mode == BackupMode::Passphrase || mode == BackupMode::Verify ||
            mode == BackupMode::SdInfo) &&
           state_write("mode", static_cast<std::uint8_t>(mode));
}

void backup_startup(Board& board) {
    std::uint8_t mode = 0, pending = 0;
    if (!state_read("mode", mode) || !state_read("pending", pending)) {
        backup_recovery(board);
    }
    if (pending != 0) { backup_recovery(board); }
    if (mode == 0) { return; }
    if (!state_write("mode", 0)) { backup_recovery(board); }
    if (mode == static_cast<unsigned>(BackupMode::Save) || mode == static_cast<unsigned>(BackupMode::Restore)) {
        run_mode(board, static_cast<BackupMode>(mode));
    } else if (mode == static_cast<unsigned>(BackupMode::Passphrase)) {
        passphrase_setup(board);
    } else if (mode == static_cast<unsigned>(BackupMode::Verify)) {
        run_verify(board);
    } else if (mode == static_cast<unsigned>(BackupMode::SdInfo)) {
        run_sd_info(board);
    }
    if (sd_teardown_failed) { esp_restart(); }
    if (!state_read("pending", pending) || pending != 0) { backup_recovery(board); }
    esp_restart();
}

struct EraseJob {
    bool ok = false;
};

void erase_store_job(void* arg) {
    auto* job = static_cast<EraseJob*>(arg);
    const esp_partition_t* store = cardputer_fido_partition();
    job->ok = store != nullptr && esp_partition_erase_range(store, 0, CF_FIDO_STORE_SIZE) == ESP_OK;
}

// Erases the key pages and filesystem so the next start creates a new device identity,
// exactly as on a freshly flashed device. Offered only for a valid partition that holds no
// usable store, and never without the user's confirmation: once for an area that has never
// held SecTrustKey data, twice for one that has.
bool set_up_new_store(Board& board, bool never_used) {
    if (never_used) {
        if (!confirm(board, "Set up storage?", "No SecTrustKey keys here yet", Mood::Info, "Storage")) {
            return false;
        }
    } else {
        if (!confirm(board, "Erase and start over?", "Keys stored here are lost", Mood::Bad, "Storage") ||
            !confirm(board, "Really erase?", "This cannot be undone", Mood::Bad, "Storage")) {
            return false;
        }
    }
    EraseJob job;
    run_with_progress(board, "Preparing storage", "Erasing the key area", Accent::Amber, 6000,
                      erase_store_job, &job);
    if (!job.ok) {
        notice(board, "Erase failed", "USB stays off", Mood::Bad, "Storage");
        return false;
    }
    notice(board, "Storage ready", "Press Enter to restart", Mood::Good, "Storage");
    esp_restart();
    return true;  // not reached on the device; host stubs of esp_restart() do return
}

[[noreturn]] void backup_recovery(Board& board) {
    // Status -3 means the partition is valid but its contents are not a usable store. Right
    // after an installer such as M5 Launcher creates the partition, it still holds another
    // app's old data and no SecTrustKey record at all.
    const bool can_set_up = cardputer_key_store_status() == -3 && cardputer_fido_partition() != nullptr;
    const bool never_used = can_set_up && cardputer_key_store_foreign() == 1;
    for (;;) {
        if (never_used) { set_up_new_store(board, true); }
        if (confirm(board, "Storage recovery", "Restore backup from SD?")) {
            run_mode(board, BackupMode::Restore);
            if (sd_teardown_failed) { esp_restart(); }
        } else if (can_set_up && !never_used) {
            if (!set_up_new_store(board, false)) {
                notice(board, "USB disabled", "Restore required", Mood::Bad);
            }
        } else {
            notice(board, "USB disabled", "Restore required", Mood::Bad);
        }
    }
}
}  // namespace cardputer
