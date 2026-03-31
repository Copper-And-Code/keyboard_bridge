/**
 * board_test.c - Minimal board diagnostic without CYW43
 *
 * Prints system clock, board name, flash size, and a heartbeat counter.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"

#ifndef PICO_BOARD
#define PICO_BOARD "unknown"
#endif

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

int main(void) {
    stdio_init_all();
    sleep_ms(3000);

    printf("\n=== Board Test ===\n");

    uint32_t sys_clk = clock_get_hz(clk_sys);
    printf("[BOARD] Board:       %s\n", PICO_BOARD);
    printf("[BOARD] System clock: %u MHz\n", sys_clk / 1000000);
    printf("[BOARD] Flash size:  %u KB\n", PICO_FLASH_SIZE_BYTES / 1024);

    printf("[BOARD] Starting heartbeat...\n");

    int count = 0;
    while (true) {
        printf("[HEARTBEAT] %d\n", count);
        count++;
        sleep_ms(1000);
    }

    return 0;
}
