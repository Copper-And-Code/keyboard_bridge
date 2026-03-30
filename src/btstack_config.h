#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

// BTstack features to enable
#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP
#define ENABLE_CLASSIC
#define ENABLE_BLE
#define ENABLE_HFP_WIDE_BAND_SPEECH

// BTstack configuration (memory pools, etc.)
#define MAX_NR_BTSTACK_LINK_KEYS            16
#define MAX_NR_BNEP_CHANNELS                0
#define MAX_NR_BNEP_SERVICES                0
#define MAX_NR_HCI_CONNECTIONS              2
#define MAX_NR_HID_HOST_CONNECTIONS         1
#define MAX_NR_L2CAP_CHANNELS               4
#define MAX_NR_L2CAP_SERVICES               3
#define MAX_NR_RFCOMM_CHANNELS              0
#define MAX_NR_RFCOMM_MULTIPLEXERS          0
#define MAX_NR_RFCOMM_SERVICES              0
#define MAX_NR_SERVICE_RECORD_ITEMS         4
#define MAX_NR_SM_LOOKUP_ENTRIES            3
#define MAX_NR_WHITELIST_ENTRIES            1

#define MAX_NR_LE_DEVICE_DB_ENTRIES         4

// HID Host
#define MAX_NR_HID_HOST_CONNECTIONS         1
#define HID_HOST_BOOT_PROTOCOL_MODE_SUPPORTED

// Link Key DB
#define NVM_NUM_LINK_KEYS                   16

#endif // BTSTACK_CONFIG_H
