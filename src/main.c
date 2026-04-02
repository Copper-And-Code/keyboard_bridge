#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "btstack.h"
#include "btstack_run_loop.h"
#include "btstack_tlv.h"
#include "ble/sm.h"
#include "ble/gatt_client.h"

#include "hid_keyboard.h"

// HCI event callback registration
static btstack_packet_callback_registration_t hci_event_callback_registration;
// SM event callback registration
static btstack_packet_callback_registration_t sm_event_callback_registration;

// LED blink intervals
#define LED_BLINK_MS_RECONNECTING   200
#define LED_BLINK_MS_SCANNING       500
#define LED_BLINK_MS_CONNECTED      0

// Connection state
static bool is_connected = false;
static bool is_connecting = false;

// LED state
static btstack_timer_source_t led_timer;
static bool led_state = false;
static uint32_t led_blink_interval_ms = LED_BLINK_MS_SCANNING;

// Scan/inquiry state
static bool inquiry_active = false;

// BLE HID connection state
static hci_con_handle_t ble_con_handle = HCI_CON_HANDLE_INVALID;
static bd_addr_t ble_keyboard_addr;
static bd_addr_type_t ble_keyboard_addr_type;

// GATT discovery state
typedef enum {
    GATT_STATE_IDLE,
    GATT_STATE_W4_SERVICES,
    GATT_STATE_W4_CHARACTERISTICS,
    GATT_STATE_W4_CCCD_DISCOVERY,
    GATT_STATE_W4_CCCD_WRITE,
    GATT_STATE_W4_READ,
    GATT_STATE_CONNECTED
} gatt_state_t;

static gatt_state_t gatt_state = GATT_STATE_IDLE;

// HID service handles
static gatt_client_service_t hid_service;
static bool hid_service_found = false;

// Report characteristics we discover
#define MAX_REPORTS 8
typedef struct {
    gatt_client_characteristic_t characteristic;
    uint16_t cccd_handle;
    bool is_input;  // true if properties include NOTIFY
} report_char_t;

static report_char_t report_chars[MAX_REPORTS];
static int num_report_chars = 0;
static int current_cccd_index = 0;

// Notification listener
static gatt_client_notification_t notification_listener;

// Polling timer for reading reports (fallback if notifications don't work)
static btstack_timer_source_t poll_timer;
static bool polling_active = false;
static uint16_t boot_kb_input_handle = 0;
static uint8_t last_report[8];  // Track last report to detect changes

// Track whether notifications are actually arriving
static bool notifications_working = false;
static btstack_timer_source_t notification_watchdog;

// --- Stored keyboard (persisted in flash via TLV) ---
#define TLV_TAG_KEYBOARD_ADDR      0x4B42
typedef struct {
    bd_addr_t addr;
    uint8_t   addr_type;
    uint8_t   is_ble;
} stored_keyboard_t;

static stored_keyboard_t stored_kb;
static bool has_stored_kb = false;

// Reconnect timeout timer
static btstack_timer_source_t reconnect_timer;
static bool reconnect_attempted = false;

// ---- Flash storage helpers ----

static void load_stored_keyboard(void) {
    const btstack_tlv_t *tlv_impl;
    void *tlv_context;
    btstack_tlv_get_instance(&tlv_impl, &tlv_context);

    int size = tlv_impl->get_tag(tlv_context, TLV_TAG_KEYBOARD_ADDR,
                                  (uint8_t *)&stored_kb, sizeof(stored_kb));
    if (size == sizeof(stored_kb)) {
        has_stored_kb = true;
        printf("[STORE] Loaded saved keyboard: %s (type:%d, ble:%d)\n",
               bd_addr_to_str(stored_kb.addr), stored_kb.addr_type, stored_kb.is_ble);
    } else {
        has_stored_kb = false;
        printf("[STORE] No saved keyboard found.\n");
    }
}

static void save_keyboard(const bd_addr_t addr, uint8_t addr_type, bool is_ble) {
    const btstack_tlv_t *tlv_impl;
    void *tlv_context;
    btstack_tlv_get_instance(&tlv_impl, &tlv_context);

    memcpy(stored_kb.addr, addr, sizeof(bd_addr_t));
    stored_kb.addr_type = addr_type;
    stored_kb.is_ble = is_ble ? 1 : 0;
    has_stored_kb = true;

    tlv_impl->store_tag(tlv_context, TLV_TAG_KEYBOARD_ADDR,
                         (uint8_t *)&stored_kb, sizeof(stored_kb));
    printf("[STORE] Saved keyboard: %s\n", bd_addr_to_str(addr));
}

// ---- LED ----

static void led_timer_handler(btstack_timer_source_t *ts) {
    if (led_blink_interval_ms == 0) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    } else {
        led_state = !led_state;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
    }
    btstack_run_loop_set_timer(ts, led_blink_interval_ms ? led_blink_interval_ms : 1000);
    btstack_run_loop_add_timer(ts);
}

static void led_set_blink(uint32_t interval_ms) {
    led_blink_interval_ms = interval_ms;
}

// ---- Scanning / Discovery ----

static void start_scan(void) {
    if (inquiry_active || is_connected || is_connecting) return;
    printf("[BT] Scanning for keyboards (BLE)...\n");
    inquiry_active = true;
    led_set_blink(LED_BLINK_MS_SCANNING);
    gap_set_scan_parameters(1, 0x0060, 0x0030);
    gap_start_scan();
}

static void stop_scan(void) {
    if (!inquiry_active) return;
    gap_stop_scan();
    inquiry_active = false;
}

static void try_reconnect_stored(void) {
    if (!has_stored_kb || is_connected) return;

    printf("[BT] Trying to reconnect to saved keyboard %s...\n",
           bd_addr_to_str(stored_kb.addr));
    led_set_blink(LED_BLINK_MS_RECONNECTING);
    reconnect_attempted = true;

    gap_set_scan_parameters(1, 0x0060, 0x0030);
    gap_start_scan();
}

static void reconnect_timeout_handler(btstack_timer_source_t *ts) {
    (void)ts;
    if (is_connected) return;
    printf("[BT] Reconnect timeout, scanning for all keyboards...\n");
    stop_scan();
    reconnect_attempted = false;
    start_scan();
}

// ---- BLE advertisement handler ----

static bool is_ble_keyboard(const uint8_t *adv_data, uint8_t adv_len,
                             const char **out_name) {
    ad_context_t context;
    uint16_t appearance = 0;
    bool has_hid = false;
    *out_name = NULL;

    for (ad_iterator_init(&context, adv_len, adv_data);
         ad_iterator_has_more(&context);
         ad_iterator_next(&context)) {
        uint8_t type = ad_iterator_get_data_type(&context);
        uint8_t len = ad_iterator_get_data_len(&context);
        const uint8_t *data = ad_iterator_get_data(&context);

        if (type == BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME ||
            type == BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME) {
            *out_name = (const char *)data;
        }
        if (type == BLUETOOTH_DATA_TYPE_APPEARANCE && len >= 2) {
            appearance = little_endian_read_16(data, 0);
        }
        if (type == BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS ||
            type == BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS) {
            for (int j = 0; j + 1 < len; j += 2) {
                if (little_endian_read_16(data, j) == 0x1812) {
                    has_hid = true;
                }
            }
        }
    }
    return has_hid || appearance == 0x03C1 || appearance == 0x03C0;
}

static void handle_ble_advertisement(uint8_t *packet) {
    bd_addr_t addr;
    gap_event_advertising_report_get_address(packet, addr);
    uint8_t addr_type = gap_event_advertising_report_get_address_type(packet);
    uint8_t adv_len = gap_event_advertising_report_get_data_length(packet);
    const uint8_t *adv_data = gap_event_advertising_report_get_data(packet);

    const char *name = NULL;
    bool keyboard = is_ble_keyboard(adv_data, adv_len, &name);

    // During reconnect: only connect to stored device
    if (reconnect_attempted && has_stored_kb && stored_kb.is_ble) {
        if (memcmp(addr, stored_kb.addr, sizeof(bd_addr_t)) == 0) {
            printf("[BLE] Found saved keyboard %s, reconnecting...\n",
                   bd_addr_to_str(addr));
            gap_stop_scan();
            is_connecting = true;
            memcpy(ble_keyboard_addr, addr, sizeof(bd_addr_t));
            ble_keyboard_addr_type = (bd_addr_type_t)addr_type;
            gap_connect(addr, ble_keyboard_addr_type);
        }
        return;
    }

    // Normal scan: connect to any HID keyboard
    if (keyboard) {
        printf("[BLE] Found keyboard: %s name:'%s'\n",
               bd_addr_to_str(addr), name ? name : "");
        if (!is_connected && !is_connecting) {
            printf("[BLE] Connecting to %s...\n", bd_addr_to_str(addr));
            stop_scan();
            btstack_run_loop_remove_timer(&reconnect_timer);
            is_connecting = true;
            memcpy(ble_keyboard_addr, addr, sizeof(bd_addr_t));
            ble_keyboard_addr_type = (bd_addr_type_t)addr_type;
            gap_connect(addr, ble_keyboard_addr_type);
        }
    }
}

// ---- Notification handler ----

static void notification_handler(uint8_t packet_type, uint16_t channel,
                                  uint8_t *packet, uint16_t size) {
    UNUSED(packet_type);
    UNUSED(channel);
    UNUSED(size);

    uint8_t event_type = hci_event_packet_get_type(packet);

    if (event_type == GATT_EVENT_NOTIFICATION || event_type == GATT_EVENT_INDICATION) {
        uint16_t value_handle = gatt_event_notification_get_value_handle(packet);
        uint16_t value_len = gatt_event_notification_get_value_length(packet);
        const uint8_t *value = gatt_event_notification_get_value(packet);
        printf("[HID] Notif (handle=0x%04X, %d bytes):", value_handle, value_len);
        for (int i = 0; i < value_len && i < 16; i++) {
            printf(" %02X", value[i]);
        }
        printf("\n");

        // Notifications work - stop polling fallback if running
        if (!notifications_working) {
            notifications_working = true;
            if (polling_active) {
                printf("[HID] Notifications working, stopping poll fallback.\n");
                btstack_run_loop_remove_timer(&poll_timer);
                polling_active = false;
            }
        }

        hid_keyboard_process_report(value, value_len);
    }
}

// ---- Manual GATT discovery ----

static void enable_next_cccd(void);
static void hid_connection_ready(void);

static void gatt_event_handler(uint8_t packet_type, uint16_t channel,
                                uint8_t *packet, uint16_t size) {
    UNUSED(packet_type);
    UNUSED(channel);
    UNUSED(size);

    uint8_t event_type = hci_event_packet_get_type(packet);

    switch (gatt_state) {
        case GATT_STATE_W4_SERVICES:
            if (event_type == GATT_EVENT_SERVICE_QUERY_RESULT) {
                gatt_event_service_query_result_get_service(packet, &hid_service);
                hid_service_found = true;
                printf("[GATT] Found HID service: 0x%04X-0x%04X\n",
                       hid_service.start_group_handle, hid_service.end_group_handle);
            }
            if (event_type == GATT_EVENT_QUERY_COMPLETE) {
                uint8_t status = gatt_event_query_complete_get_att_status(packet);
                printf("[GATT] Service discovery complete, status: 0x%02X\n", status);
                if (hid_service_found && status == ATT_ERROR_SUCCESS) {
                    gatt_state = GATT_STATE_W4_CHARACTERISTICS;
                    num_report_chars = 0;
                    gatt_client_discover_characteristics_for_service(
                        gatt_event_handler, ble_con_handle, &hid_service);
                } else {
                    printf("[GATT] No HID service found!\n");
                    gatt_state = GATT_STATE_IDLE;
                }
            }
            break;

        case GATT_STATE_W4_CHARACTERISTICS:
            if (event_type == GATT_EVENT_CHARACTERISTIC_QUERY_RESULT) {
                gatt_client_characteristic_t chr;
                gatt_event_characteristic_query_result_get_characteristic(packet, &chr);

                uint16_t uuid16 = 0;
                if (chr.uuid16 != 0) {
                    uuid16 = chr.uuid16;
                }

                const char *name = "?";
                switch (uuid16) {
                    case 0x2A4D: name = "Report"; break;
                    case 0x2A4E: name = "Protocol Mode"; break;
                    case 0x2A4B: name = "Report Map"; break;
                    case 0x2A4A: name = "HID Information"; break;
                    case 0x2A4C: name = "HID Control Point"; break;
                    case 0x2A22: name = "Boot KB Input"; break;
                    case 0x2A32: name = "Boot KB Output"; break;
                    default: break;
                }

                printf("[GATT]   Char UUID=0x%04X (%s) handle=0x%04X val=0x%04X props=0x%02X\n",
                       uuid16, name, chr.start_handle, chr.value_handle, chr.properties);

                // Save Boot KB Input handle
                if (uuid16 == 0x2A22) {
                    boot_kb_input_handle = chr.value_handle;
                }
                // Save characteristics that support NOTIFY (input reports)
                bool is_report = (uuid16 == 0x2A4D) || (uuid16 == 0x2A22);
                bool can_notify = (chr.properties & ATT_PROPERTY_NOTIFY) != 0;

                if (is_report && can_notify && num_report_chars < MAX_REPORTS) {
                    report_chars[num_report_chars].characteristic = chr;
                    report_chars[num_report_chars].cccd_handle = 0;
                    report_chars[num_report_chars].is_input = true;
                    num_report_chars++;
                    printf("[GATT]     -> Saved as input report #%d\n", num_report_chars);
                }
            }
            if (event_type == GATT_EVENT_QUERY_COMPLETE) {
                printf("[GATT] Characteristic discovery complete. Found %d input reports.\n",
                       num_report_chars);
                if (num_report_chars > 0) {
                    // Enable notifications by writing CCCD for each input report
                    current_cccd_index = 0;
                    enable_next_cccd();
                } else {
                    printf("[GATT] No input reports found!\n");
                    gatt_state = GATT_STATE_IDLE;
                }
            }
            break;

        case GATT_STATE_W4_CCCD_WRITE:
            if (event_type == GATT_EVENT_QUERY_COMPLETE) {
                uint8_t status = gatt_event_query_complete_get_att_status(packet);
                printf("[GATT] CCCD write #%d complete, status: 0x%02X\n",
                       current_cccd_index, status);
                current_cccd_index++;
                enable_next_cccd();
            }
            break;

        case GATT_STATE_W4_READ:
            if (event_type == GATT_EVENT_CHARACTERISTIC_VALUE_QUERY_RESULT) {
                uint16_t value_len = gatt_event_characteristic_value_query_result_get_value_length(packet);
                const uint8_t *value = gatt_event_characteristic_value_query_result_get_value(packet);
                // Only process if report changed
                if (value_len >= 8 && memcmp(value, last_report, 8) != 0) {
                    memcpy(last_report, value, 8);
                    hid_keyboard_process_report(value, value_len);
                }
            }
            if (event_type == GATT_EVENT_QUERY_COMPLETE) {
                gatt_state = GATT_STATE_CONNECTED;
                // Schedule next poll if polling fallback is active
                if (polling_active) {
                    btstack_run_loop_set_timer(&poll_timer, 1);
                    btstack_run_loop_add_timer(&poll_timer);
                }
            }
            break;

        default:
            break;
    }
}

static void poll_timer_handler(btstack_timer_source_t *ts) {
    UNUSED(ts);
    if (!polling_active) return;
    if (ble_con_handle == HCI_CON_HANDLE_INVALID || !is_connected) return;
    if (notifications_working) {
        // Notifications started working, stop polling
        polling_active = false;
        return;
    }
    if (gatt_state != GATT_STATE_CONNECTED) {
        // Busy, retry ASAP
        btstack_run_loop_set_timer(&poll_timer, 1);
        btstack_run_loop_add_timer(&poll_timer);
        return;
    }
    // Poll Boot KB Input characteristic specifically
    if (boot_kb_input_handle != 0) {
        gatt_state = GATT_STATE_W4_READ;
        gatt_client_read_value_of_characteristic_using_value_handle(
            gatt_event_handler, ble_con_handle, boot_kb_input_handle);
        // Next poll scheduled after read completes (in gatt_event_handler)
    }
}

static void start_poll_fallback(void) {
    if (polling_active || notifications_working) return;
    printf("[HID] No notifications received, starting poll fallback.\n");
    polling_active = true;
    memset(last_report, 0, sizeof(last_report));
    btstack_run_loop_set_timer_handler(&poll_timer, poll_timer_handler);
    btstack_run_loop_set_timer(&poll_timer, 10);
    btstack_run_loop_add_timer(&poll_timer);
}

static void notification_watchdog_handler(btstack_timer_source_t *ts) {
    UNUSED(ts);
    if (!is_connected) return;
    if (!notifications_working) {
        start_poll_fallback();
    }
}

static void hid_connection_ready(void) {
    is_connecting = false;
    is_connected = true;
    notifications_working = false;
    polling_active = false;
    led_set_blink(LED_BLINK_MS_CONNECTED);
    btstack_run_loop_remove_timer(&reconnect_timer);
    save_keyboard(ble_keyboard_addr, ble_keyboard_addr_type, true);

    // Request fast connection parameters for responsive input
    gap_request_connection_parameter_update(ble_con_handle, 6, 12, 0, 200);
    printf("[BLE] HID ready! Requested fast connection params (7.5-15ms, latency 0).\n");

    if (boot_kb_input_handle != 0) {
        printf("[BLE] Boot KB Input handle=0x%04X — starting poll immediately.\n",
               boot_kb_input_handle);
        // Start polling Boot KB Input immediately (don't wait for notifications
        // since this keyboard doesn't send them)
        memset(last_report, 0, sizeof(last_report));
        polling_active = true;
        btstack_run_loop_set_timer_handler(&poll_timer, poll_timer_handler);
        btstack_run_loop_set_timer(&poll_timer, 1);
        btstack_run_loop_add_timer(&poll_timer);
    } else {
        printf("[BLE] No Boot KB Input, waiting for notifications...\n");
        btstack_run_loop_set_timer_handler(&notification_watchdog, notification_watchdog_handler);
        btstack_run_loop_set_timer(&notification_watchdog, 3000);
        btstack_run_loop_add_timer(&notification_watchdog);
    }
}

static void enable_next_cccd(void) {
    while (current_cccd_index < num_report_chars) {
        gatt_state = GATT_STATE_W4_CCCD_WRITE;
        uint8_t status = gatt_client_write_client_characteristic_configuration(
            gatt_event_handler, ble_con_handle,
            &report_chars[current_cccd_index].characteristic,
            GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
        printf("[GATT] Writing CCCD for report #%d (val_handle=0x%04X), status: 0x%02X\n",
               current_cccd_index,
               report_chars[current_cccd_index].characteristic.value_handle,
               status);
        if (status == ERROR_CODE_SUCCESS) {
            return; // Wait for completion
        }
        // Skip failed ones
        current_cccd_index++;
    }

    // All CCCDs written - proceed without switching protocol mode
    // (some keyboards disconnect if forced to Boot Protocol)
    gatt_state = GATT_STATE_CONNECTED;
    hid_connection_ready();
}

static void start_hid_discovery(void) {
    printf("[GATT] Starting HID service discovery...\n");
    hid_service_found = false;
    gatt_state = GATT_STATE_W4_SERVICES;
    gatt_client_discover_primary_services_by_uuid16(
        gatt_event_handler, ble_con_handle, 0x1812);
}

// ---- Main packet handler ----

static void packet_handler(uint8_t packet_type, uint16_t channel,
                            uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;

    if (packet_type != HCI_EVENT_PACKET) return;

    uint8_t event = hci_event_packet_get_type(packet);

    switch (event) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                printf("[BT] Bluetooth stack ready.\n");
                load_stored_keyboard();
                if (has_stored_kb) {
                    try_reconnect_stored();
                    btstack_run_loop_set_timer_handler(&reconnect_timer,
                                                       reconnect_timeout_handler);
                    btstack_run_loop_set_timer(&reconnect_timer, 10000);
                    btstack_run_loop_add_timer(&reconnect_timer);
                } else {
                    start_scan();
                }
            }
            break;

        // ---- SM (BLE Security Manager) events ----
        case SM_EVENT_JUST_WORKS_REQUEST:
            printf("[BLE] Pairing: Just Works — accepting\n");
            sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
            break;

        case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
            printf("[BLE] Pairing: Numeric Comparison — accepting\n");
            sm_numeric_comparison_confirm(sm_event_numeric_comparison_request_get_handle(packet));
            break;

        case SM_EVENT_PAIRING_STARTED:
            printf("[BLE] Pairing started.\n");
            break;

        case SM_EVENT_PAIRING_COMPLETE: {
            uint8_t sm_status = sm_event_pairing_complete_get_status(packet);
            hci_con_handle_t sm_handle = sm_event_pairing_complete_get_handle(packet);
            printf("[BLE] Pairing complete, status: 0x%02X\n", sm_status);
            if (sm_status == ERROR_CODE_SUCCESS && sm_handle == ble_con_handle
                && gatt_state == GATT_STATE_IDLE) {
                start_hid_discovery();
            }
            break;
        }

        case SM_EVENT_REENCRYPTION_COMPLETE: {
            uint8_t re_status = sm_event_reencryption_complete_get_status(packet);
            hci_con_handle_t re_handle = sm_event_reencryption_complete_get_handle(packet);
            printf("[BLE] Re-encryption complete, status: 0x%02X\n", re_status);
            if (re_status == ERROR_CODE_SUCCESS && re_handle == ble_con_handle
                && gatt_state == GATT_STATE_IDLE) {
                start_hid_discovery();
            } else if (re_status != ERROR_CODE_SUCCESS && re_handle == ble_con_handle) {
                printf("[BLE] Re-encryption failed, requesting fresh pairing...\n");
                sm_request_pairing(ble_con_handle);
            }
            break;
        }

        case GAP_EVENT_ADVERTISING_REPORT:
            handle_ble_advertisement(packet);
            break;

        // ---- BLE connection events ----
        case HCI_EVENT_LE_META:
            switch (hci_event_le_meta_get_subevent_code(packet)) {
                case HCI_SUBEVENT_LE_CONNECTION_COMPLETE: {
                    uint8_t status = hci_subevent_le_connection_complete_get_status(packet);
                    if (status == ERROR_CODE_SUCCESS) {
                        ble_con_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
                        printf("[BLE] Connected! (handle: 0x%04X)\n", ble_con_handle);

                        // Register notification listener for ALL characteristics
                        gatt_client_listen_for_characteristic_value_updates(
                            &notification_listener, &notification_handler,
                            ble_con_handle, NULL);

                        // Request encryption/pairing - discovery starts after SM completes
                        gatt_state = GATT_STATE_IDLE;
                        printf("[BLE] Requesting encryption...\n");
                        sm_request_pairing(ble_con_handle);
                    } else {
                        printf("[BLE] Connection failed: 0x%02X\n", status);
                        is_connecting = false;
                        start_scan();
                    }
                    break;
                }
                case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
                    printf("[BLE] Connection parameters updated.\n");
                    break;
                default:
                    break;
            }
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE: {
            hci_con_handle_t handle = hci_event_disconnection_complete_get_connection_handle(packet);
            if (handle == ble_con_handle) {
                uint8_t reason = hci_event_disconnection_complete_get_reason(packet);
                printf("[BLE] Keyboard disconnected, reason: 0x%02X\n", reason);
                btstack_run_loop_remove_timer(&poll_timer);
                btstack_run_loop_remove_timer(&notification_watchdog);
                polling_active = false;
                notifications_working = false;
                gatt_client_stop_listening_for_characteristic_value_updates(&notification_listener);
                ble_con_handle = HCI_CON_HANDLE_INVALID;
                gatt_state = GATT_STATE_IDLE;
                is_connected = false;
                is_connecting = false;
                led_set_blink(LED_BLINK_MS_SCANNING);
                // Try to reconnect
                reconnect_attempted = false;
                try_reconnect_stored();
                btstack_run_loop_set_timer_handler(&reconnect_timer,
                                                   reconnect_timeout_handler);
                btstack_run_loop_set_timer(&reconnect_timer, 10000);
                btstack_run_loop_add_timer(&reconnect_timer);
            }
            break;
        }

        default:
            break;
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(3000);

    printf("\n=== Keyboard Bridge ===\n");
    printf("Bluetooth Keyboard -> VT100 UART\n\n");

    hid_keyboard_init();
    printf("[UART] Initialized at 115200 8N1 on GP0/GP1\n");

    // Initialize CYW43
    printf("[CYW43] Initializing...\n");
    if (cyw43_arch_init()) {
        printf("[FATAL] CYW43 init failed!\n");
        return -1;
    }
    printf("[BT] CYW43 initialized.\n");

    // Load CYW43 firmware via WiFi STA (required for shared bus)
    cyw43_arch_enable_sta_mode();

    // Initialize protocols
    l2cap_init();
    gatt_client_init();
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_SECURE_CONNECTION | SM_AUTHREQ_BONDING);
    sm_set_secure_connections_only_mode(false);

    // Accept wide range of BLE connection parameters
    le_connection_parameter_range_t range;
    gap_get_connection_parameter_range(&range);
    range.le_conn_interval_min = 6;
    range.le_conn_interval_max = 3200;
    range.le_conn_latency_max = 500;
    range.le_supervision_timeout_min = 10;
    range.le_supervision_timeout_max = 3200;
    gap_set_connection_parameter_range(&range);

    // LED blink timer
    btstack_run_loop_set_timer_handler(&led_timer, led_timer_handler);
    btstack_run_loop_set_timer(&led_timer, LED_BLINK_MS_SCANNING);
    btstack_run_loop_add_timer(&led_timer);

    // BLE configuration
    gap_set_local_name("PicoW Keyboard Bridge");

    // Register HCI event handler
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // Register SM event handler
    sm_event_callback_registration.callback = &packet_handler;
    sm_add_event_handler(&sm_event_callback_registration);

    printf("[BT] Initialized. Powering on...\n");
    hci_power_control(HCI_POWER_ON);

    btstack_run_loop_execute();
    return 0;
}
