#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "btstack.h"
#include "btstack_run_loop.h"
#include "classic/hid_host.h"

#include "hid_keyboard.h"

// LED blink interval to indicate BT state
#define LED_BLINK_MS_SCANNING       500
#define LED_BLINK_MS_CONNECTED      0       // Solid on

// HID Host connection handle
static uint16_t hid_host_cid = 0;
static bd_addr_t connected_addr;
static bool is_connected = false;

// LED state for status indication
static btstack_timer_source_t led_timer;
static bool led_state = false;
static uint32_t led_blink_interval_ms = LED_BLINK_MS_SCANNING;

// Inquiry state
static bool inquiry_active = false;

/**
 * Toggle the on-board LED to indicate Bluetooth status.
 */
static void led_timer_handler(btstack_timer_source_t *ts) {
    if (led_blink_interval_ms == 0) {
        // Solid on when connected
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    } else {
        led_state = !led_state;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
    }
    btstack_run_loop_set_timer(ts, led_blink_interval_ms ? led_blink_interval_ms : 1000);
    btstack_run_loop_add_timer(ts);
}

/**
 * Start LED blinking with a given interval (0 = solid on).
 */
static void led_set_blink(uint32_t interval_ms) {
    led_blink_interval_ms = interval_ms;
}

/**
 * GAP inquiry result callback — connect to the first HID keyboard found.
 */
static void handle_inquiry_result(uint8_t *packet) {
    // Extract device class from inquiry result
    uint32_t class_of_device = gap_event_inquiry_result_get_class_of_device(packet);

    // Check if device is a keyboard:
    // Major Device Class = Peripheral (0x05), Minor = Keyboard (bit 6 set)
    uint8_t major_class = (class_of_device >> 8) & 0x1F;
    uint8_t minor_class = (class_of_device >> 2) & 0x3F;

    // Major class 0x05 = Peripheral, minor bit 0x10 = Keyboard
    if (major_class == 0x05 && (minor_class & 0x10)) {
        bd_addr_t addr;
        gap_event_inquiry_result_get_bd_addr(packet, addr);
        printf("[BT] Found keyboard: %s (CoD: 0x%06X)\n",
               bd_addr_to_str(addr), (unsigned int)class_of_device);

        // Stop inquiry and connect
        gap_inquiry_stop();
        inquiry_active = false;

        printf("[BT] Connecting to %s...\n", bd_addr_to_str(addr));
        memcpy(connected_addr, addr, sizeof(bd_addr_t));
        hid_host_connect(addr, HID_PROTOCOL_MODE_BOOT, &hid_host_cid);
    }
}

/**
 * Start Bluetooth inquiry to discover keyboards.
 */
static void start_inquiry(void) {
    if (inquiry_active) return;
    printf("[BT] Scanning for Bluetooth keyboards...\n");
    inquiry_active = true;
    gap_inquiry_start(10);  // 10 * 1.28s = ~12.8 seconds
}

/**
 * BTstack HID Host event handler.
 */
static void hid_host_packet_handler(uint8_t packet_type, uint16_t channel,
                                     uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;

    if (packet_type == HCI_EVENT_PACKET) {
        uint8_t event_type = hci_event_packet_get_type(packet);

        switch (event_type) {
            case BTSTACK_EVENT_STATE:
                if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                    printf("[BT] Bluetooth stack ready.\n");
                    start_inquiry();
                }
                break;

            case GAP_EVENT_INQUIRY_RESULT:
                handle_inquiry_result(packet);
                break;

            case GAP_EVENT_INQUIRY_COMPLETE:
                printf("[BT] Inquiry complete.\n");
                inquiry_active = false;
                if (!is_connected) {
                    // Restart inquiry if no keyboard found
                    printf("[BT] No keyboard found, restarting scan...\n");
                    start_inquiry();
                }
                break;

            case HCI_EVENT_HID_META: {
                uint8_t subevent = hci_event_hid_meta_get_subevent_code(packet);
                switch (subevent) {
                    case HID_SUBEVENT_CONNECTION_OPENED: {
                        uint8_t status = hid_subevent_connection_opened_get_status(packet);
                        if (status == ERROR_CODE_SUCCESS) {
                            hid_host_cid = hid_subevent_connection_opened_get_hid_cid(packet);
                            is_connected = true;
                            led_set_blink(LED_BLINK_MS_CONNECTED);
                            printf("[BT] Keyboard connected! (cid: 0x%04X)\n", hid_host_cid);
                        } else {
                            printf("[BT] Connection failed: 0x%02X\n", status);
                            is_connected = false;
                            start_inquiry();
                        }
                        break;
                    }

                    case HID_SUBEVENT_CONNECTION_CLOSED:
                        printf("[BT] Keyboard disconnected.\n");
                        hid_host_cid = 0;
                        is_connected = false;
                        led_set_blink(LED_BLINK_MS_SCANNING);
                        // Restart scanning
                        start_inquiry();
                        break;

                    case HID_SUBEVENT_REPORT: {
                        // Get the HID report data
                        const uint8_t *report = hid_subevent_report_get_report(packet);
                        uint16_t report_len = hid_subevent_report_get_report_len(packet);
                        hid_keyboard_process_report(report, report_len);
                        break;
                    }

                    default:
                        break;
                }
                break;
            }

            case HCI_EVENT_PIN_CODE_REQUEST: {
                // Handle legacy pairing: respond with "0000"
                bd_addr_t addr;
                hci_event_pin_code_request_get_bd_addr(packet, addr);
                printf("[BT] PIN code request from %s, using '0000'\n",
                       bd_addr_to_str(addr));
                gap_pin_code_response(addr, "0000");
                break;
            }

            case HCI_EVENT_USER_CONFIRMATION_REQUEST: {
                // Auto-accept SSP pairing
                bd_addr_t addr;
                hci_event_user_confirmation_request_get_bd_addr(packet, addr);
                printf("[BT] SSP confirmation request from %s — accepting\n",
                       bd_addr_to_str(addr));
                gap_ssp_confirmation_response(addr);
                break;
            }

            default:
                break;
        }
    }
}

int main(void) {
    // Initialize Pico standard I/O (for debug printf via USB if needed)
    stdio_init_all();

    printf("\n=== Keyboard Bridge ===\n");
    printf("Bluetooth Keyboard -> VT100 UART\n\n");

    // Initialize the HID keyboard handler (configures UART)
    hid_keyboard_init();
    printf("[UART] Initialized at 115200 8N1 on GP0/GP1\n");

    // Initialize CYW43 (Wi-Fi/BT chip)
    if (cyw43_arch_init()) {
        printf("[ERROR] CYW43 init failed!\n");
        return -1;
    }
    printf("[BT] CYW43 initialized.\n");

    // Setup LED blink timer
    btstack_run_loop_set_timer_handler(&led_timer, led_timer_handler);
    btstack_run_loop_set_timer(&led_timer, LED_BLINK_MS_SCANNING);
    btstack_run_loop_add_timer(&led_timer);

    // Configure Bluetooth
    // Allow role switch and sniff mode for better power management
    gap_set_default_link_policy_settings(
        LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE);

    // Set device as discoverable and connectable
    gap_discoverable_control(1);
    gap_connectable_control(1);

    // Set local Bluetooth name
    gap_set_local_name("PicoW Keyboard Bridge");

    // Set class of device: Desktop (major=Computer, minor=Desktop)
    // This helps keyboards identify us as a valid host
    gap_set_class_of_device(0x000104);  // Computer - Desktop Workstation

    // Enable Secure Simple Pairing
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_ssp_set_auto_accept(1);

    // Initialize HID Host
    hid_host_init(sizeof(hid_boot_report_t));
    hid_host_register_packet_handler(hid_host_packet_handler);

    // Turn on Bluetooth
    hci_power_control(HCI_POWER_ON);

    // Run the BTstack event loop (never returns)
    btstack_run_loop_execute();

    return 0;
}
