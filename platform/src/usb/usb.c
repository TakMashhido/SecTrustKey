/*
 * This file is part of the Pico Keys SDK distribution (https://github.com/polhenarejos/pico-keys-sdk).
 * Copyright (c) 2022 Pol Henarejos.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "stk.h"
#include "usb.h"
#include "led/led.h"
#include "button.h"
#include "stk_time.h"
#include "apdu.h"
#ifndef ENABLE_EMULATION
#include "tusb.h"
#else
#include "emulation.h"
#endif

// Device specific functions
static uint32_t *timeout_counter = NULL;
static uint8_t card_locked_itf = 0; // no locked
static void *(*card_locked_func)(void *) = NULL;
uint8_t enabled_usb_itf = PHY_USB_ITF_ALL;
#ifndef ENABLE_EMULATION
extern volatile uint32_t stk_progress;   /* defined in apdu.c */
#if defined(ESP_PLATFORM) && !defined(ENABLE_EMULATION)
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_system.h"
/* Survive a software reset so the next boot can show why the last run ended. */
RTC_NOINIT_ATTR uint32_t stk_dbg_marker;          /* 0xC0DE0001 launch failed, 0xC0DE0002 command stalled */
RTC_NOINIT_ATTR uint32_t stk_dbg_free_heap;
RTC_NOINIT_ATTR uint32_t stk_dbg_largest_block;
RTC_NOINIT_ATTR uint32_t stk_dbg_progress;
static volatile uint32_t cmd_start_ms = 0;

/* core1's command stack (4096*ITF_TOTAL*2, 32 KiB with four interfaces) must be one
 * contiguous block. By the time the first command arrives, the display canvas, USB and
 * Wi-Fi's static data have split the heap: 70 KiB free was seen with no 32 KiB block.
 * A block is therefore set aside during boot and handed over the first time core1 cannot
 * be created otherwise. When core1 is replaced its stack returns to the same place. */
#define STK_CORE1_RESERVE (34 * 1024)
static void *core1_reserve = NULL;

void stk_reserve_core1_stack(void) {
    if (core1_reserve == NULL) {
        core1_reserve = heap_caps_malloc(STK_CORE1_RESERVE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
}

static bool release_core1_reserve(void) {
    if (core1_reserve == NULL) {
        return false;
    }
    heap_caps_free(core1_reserve);
    core1_reserve = NULL;
    return true;
}

__attribute__((noinline)) static void stk_core1_launch_failed(void) {
    stk_dbg_free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    stk_dbg_largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    stk_dbg_marker = 0xC0DE0001u;
#ifdef CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    abort();
#else
    esp_restart();      /* a clean reboot is better than a frozen device */
#endif
}

__attribute__((noinline)) static void stk_command_stalled(void) {
    stk_dbg_free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    stk_dbg_largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    stk_dbg_progress = stk_progress;
    stk_dbg_marker = 0xC0DE0002u;
    abort();
}
#endif
static mutex_t mutex;
#if defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) && !defined(ENABLE_EMULATION)
/* Debug builds only: a mutex wait that lasts 20 s is a deadlock; abort so the crash dump shows it. */
#define STK_MUTEX_ENTER(m) do { if (xSemaphoreTake(*(m), pdMS_TO_TICKS(20000)) != pdTRUE) { abort(); } } while (0)
#else
#define STK_MUTEX_ENTER(m) mutex_enter_blocking(m)
#endif
#endif
#if !defined(ENABLE_EMULATION) && !defined(ESP_PLATFORM)
#ifdef _MSC_VER
#include "compat/pthread_win32.h"
#endif
pthread_t hcore0, hcore1;
#endif

#ifdef USB_ITF_HID
    uint8_t ITF_HID_CTAP = ITF_INVALID, ITF_HID_KB = ITF_INVALID;
    uint8_t ITF_HID = ITF_INVALID, ITF_KEYBOARD = ITF_INVALID;
    uint8_t ITF_HID_TOTAL = 0;
    extern void hid_init(void);
#endif

#ifdef USB_ITF_CCID
    uint8_t ITF_SC_CCID = ITF_INVALID, ITF_SC_WCID = ITF_INVALID;
    uint8_t ITF_CCID = ITF_INVALID, ITF_WCID = ITF_INVALID;
    uint8_t ITF_SC_TOTAL = 0;
    extern void ccid_init(void);
#endif

uint8_t ITF_TOTAL = 0;

void usb_set_timeout_counter(uint8_t itf, uint32_t v) {
    timeout_counter[itf] = v;
}

queue_t usb_to_card_q = {0};
queue_t card_to_usb_q = {0};

#ifndef ENABLE_EMULATION
extern tusb_desc_device_t desc_device;
extern char *string_desc_itf[5], *string_desc_arr[];
#endif
void usb_init(void)
{
#ifndef ENABLE_EMULATION
    if (phy_data.vidpid_present) {
        desc_device.idVendor = phy_data.vid;
        desc_device.idProduct = phy_data.pid;
    }
    else {
        phy_data.vid = desc_device.idVendor;
        phy_data.pid = desc_device.idProduct;
        phy_data.vidpid_present = true;
    }
    mutex_init(&mutex);
#endif
    queue_init(&card_to_usb_q, sizeof(uint32_t), 64);
    queue_init(&usb_to_card_q, sizeof(uint32_t), 64);

    enabled_usb_itf = PHY_USB_ITF_ALL;
#ifndef ENABLE_EMULATION
    if (phy_data.enabled_usb_itf_present) {
        enabled_usb_itf = phy_data.enabled_usb_itf;
    }
#endif

#ifdef USB_ITF_HID
    ITF_HID_TOTAL = 0;
#endif
#ifdef USB_ITF_CCID
    ITF_SC_TOTAL = 0;
#endif
    ITF_TOTAL = 0;
#ifdef USB_ITF_HID
    if (enabled_usb_itf & PHY_USB_ITF_HID) {
        ITF_HID_CTAP = ITF_HID_TOTAL++;
        ITF_HID = ITF_TOTAL++;
#ifndef ENABLE_EMULATION
        string_desc_itf[ITF_TOTAL - 1] = string_desc_arr[6];
#endif
    }
    if (enabled_usb_itf & PHY_USB_ITF_KB) {
        ITF_HID_KB = ITF_HID_TOTAL++;
        ITF_KEYBOARD = ITF_TOTAL++;
#ifndef ENABLE_EMULATION
        string_desc_itf[ITF_TOTAL - 1] = string_desc_arr[7];
#endif
    }
#endif
#ifdef USB_ITF_CCID
    if (enabled_usb_itf & PHY_USB_ITF_CCID) {
        ITF_SC_CCID = ITF_SC_TOTAL++;
        ITF_CCID = ITF_TOTAL++;
#ifndef ENABLE_EMULATION
        string_desc_itf[ITF_TOTAL - 1] = string_desc_arr[8];
#endif
    }
    if (enabled_usb_itf & PHY_USB_ITF_WCID) {
        ITF_SC_WCID = ITF_SC_TOTAL++;
        ITF_WCID = ITF_TOTAL++;
#ifndef ENABLE_EMULATION
        string_desc_itf[ITF_TOTAL - 1] = string_desc_arr[9];
#endif
    }
#endif
    card_locked_itf = ITF_TOTAL;
    if (timeout_counter == NULL) {
        timeout_counter = (uint32_t *)calloc(ITF_TOTAL, sizeof(uint32_t));
    }
#ifdef USB_ITF_HID
    if (ITF_HID_TOTAL > 0) {
        hid_init();
    }
#endif
#ifdef USB_ITF_CCID
    if (ITF_SC_TOTAL > 0) {
        ccid_init();
    }
#endif
#ifdef ESP_PLATFORM
    usb_desc_setup();
#endif
    set_atr();
}


uint32_t timeout = 0;
void timeout_stop(void) {
    timeout = 0;
}

void timeout_start(void) {
    timeout = board_millis();
#if defined(ESP_PLATFORM) && !defined(ENABLE_EMULATION)
    cmd_start_ms = (uint32_t)timeout;
#endif
}

bool is_busy(void) {
    return timeout > 0;
}

/* While a non-command event (the exit request when the interface changes) is in flight, this
 * thread is the only legitimate reader of card_to_usb_q: it waits for the acknowledgement
 * (flag + 1). card_status() runs from the main loop task, a different task from the TinyUSB
 * task that sends the event, and discards every message that is not "execution finished":
 * without this flag it could swallow the acknowledgement and leave the sender waiting for
 * ever, which froze both USB interfaces after a HID request followed by a smart-card one. */
static volatile bool event_ack_pending = false;

void usb_send_event(uint32_t flag) {
#ifndef ENABLE_EMULATION
    STK_MUTEX_ENTER(&mutex);
#endif
    if (flag != EV_CMD_AVAILABLE) {
        event_ack_pending = true;
    }
    queue_add_blocking(&usb_to_card_q, &flag);
    if (flag == EV_CMD_AVAILABLE) {
        timeout_start();
    }
#ifndef ENABLE_EMULATION
    mutex_exit(&mutex);
#endif

    if (flag != EV_CMD_AVAILABLE) {
        uint32_t m;
        /* No time limit: the command thread reads the exit request only after its current
         * command finishes, and some commands legitimately run for tens of seconds (on-card
         * key generation, waiting for a key press). Abandoning the wait would leave two
         * command threads sharing the same state. */
#if defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) && !defined(ENABLE_EMULATION)
        /* Debug builds only: turn a silent hang into a crash dump that holds every task's stack. */
        {
            const uint32_t started = board_millis();
            for (;;) {
                if (xQueueReceive(card_to_usb_q, &m, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (m == flag + 1) {
                        break;
                    }
                    continue;
                }
                if (board_millis() - started > 300000) {
                    abort();
                }
            }
        }
#else
        do {
            queue_remove_blocking(&card_to_usb_q, &m);
        } while (m != flag + 1);
#endif
        event_ack_pending = false;
    }
}

void card_init_core1(void) {
    low_flash_init_core1();
}

volatile uint16_t finished_data_size = 0;

void card_start(uint8_t itf, void *(*func)(void *)) {
    timeout_start();
    if (card_locked_itf != itf || card_locked_func != func) {
        if (card_locked_itf != ITF_TOTAL || card_locked_func != NULL) {
            card_exit();
        }
        if (func) {
            multicore_reset_core1();
#if defined(ESP_PLATFORM) && !defined(ENABLE_EMULATION)
            /* The dead thread's stack is freed by the idle task, so creation can fail for a
             * moment right after an interface switch. Retry instead of leaving no thread. */
            stk_progress = 0;
            BaseType_t created = multicore_launch_func_core1(func);
            if (created != pdPASS && release_core1_reserve()) {
                created = multicore_launch_func_core1(func);
            }
            for (int attempt = 0; created != pdPASS && attempt < 100; ++attempt) {
                vTaskDelay(1);
                created = multicore_launch_func_core1(func);
            }
            if (created != pdPASS) {
                stk_core1_launch_failed();
            }
#else
            multicore_launch_func_core1(func);
#endif
        }
        led_set_mode(MODE_MOUNTED);
        card_locked_itf = itf;
        card_locked_func = func;
    }
}

void card_exit(void) {
    if (card_locked_itf != ITF_TOTAL || card_locked_func != NULL) {
        usb_send_event(EV_EXIT);
        uint32_t m;
        while (queue_is_empty(&usb_to_card_q) == false) {
            if (queue_try_remove(&usb_to_card_q, &m) == false) {
                break;
            }
        }
        while (queue_is_empty(&card_to_usb_q) == false) {
#ifndef ENABLE_EMULATION
            STK_MUTEX_ENTER(&mutex);
#endif
            if (queue_try_remove(&card_to_usb_q, &m) == false) {
#ifndef ENABLE_EMULATION
                mutex_exit(&mutex);      /* was leaked here: every later wait on it hung for ever */
#endif
                break;
            }
#ifndef ENABLE_EMULATION
            mutex_exit(&mutex);
#endif
        }
        led_set_mode(MODE_SUSPENDED);
#ifdef ESP_PLATFORM
        hcore1 = NULL;
#endif
    }
    card_locked_itf = ITF_TOTAL;
    card_locked_func = NULL;
}
extern void hid_task(void);
extern void ccid_task(void);
void usb_task(void) {
#ifdef USB_ITF_HID
    hid_task();
#endif
#ifdef ENABLE_EMULATION
    emul_task();
#else
#ifdef USB_ITF_CCID
    ccid_task();
#endif
#endif
}

int card_status(uint8_t itf) {
    if (card_locked_itf == itf) {
        if (timeout == 0) {
            return STK_ERR_FILE_NOT_FOUND;
        }
        uint32_t m = 0x0;
        if (event_ack_pending) {
            return STK_ERR_FILE_NOT_FOUND;   // do not consume the exit acknowledgement
        }
#ifndef ENABLE_EMULATION
        STK_MUTEX_ENTER(&mutex);
#endif
        bool has_m = !event_ack_pending && queue_try_remove(&card_to_usb_q, &m);
#ifndef ENABLE_EMULATION
        mutex_exit(&mutex);
#endif
        //if (m != 0)
        //    printf("\n ------ M = %lu\n",m);
        if (has_m) {
            if (m == EV_EXEC_FINISHED) {
                timeout_stop();
                if (led_get_mode() == MODE_PROCESSING) {
                    led_set_mode(MODE_MOUNTED);
                }
                return STK_OK;
            }
#ifndef ENABLE_EMULATION
            else if ((m & 0xffu) == EV_PRESS_BUTTON) {
                button_wait_start_timeout(EV_PRESS_BUTTON_GET_TIMEOUT(m));
            }
#endif
            return STK_ERR_FILE_NOT_FOUND;
        }
        else {
            if (timeout > 0) {
#if defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) && !defined(ENABLE_EMULATION)
                if (board_millis() - cmd_start_ms > 300000) {
                    stk_command_stalled();
                }
#endif
                if (timeout + timeout_counter[itf] < board_millis()) {
                    timeout = board_millis();
                    return STK_ERR_BLOCKED;
                }
            }
        }
    }
    return STK_ERR_FILE_NOT_FOUND;
}

#ifndef USB_ITF_CCID
#include "device/usbd_pvt.h"
usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 0;
    return NULL;
}
#endif
