/**
 * cyw43_test.c - Minimal CYW43 test without BTstack
 *
 * Tests CYW43 initialization with retry logic and LED blink.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

int main(void) {
    stdio_init_all();
    sleep_ms(3000);

    printf("\n=== CYW43 Test ===\n");

    int rc = -1;
    int delays[] = {0, 1000, 3000};

    for (int attempt = 0; attempt < 3; attempt++) {
        if (delays[attempt] > 0) {
            printf("[CYW43] Waiting %d ms before retry...\n", delays[attempt]);
            sleep_ms(delays[attempt]);
        }

        printf("[CYW43] Init attempt %d/3...\n", attempt + 1);
        rc = cyw43_arch_init();
        if (rc == 0) {
            printf("[CYW43] Init succeeded on attempt %d!\n", attempt + 1);
            break;
        }
        printf("[CYW43] Init failed (rc=%d)\n", rc);
    }

    if (rc != 0) {
        printf("[FATAL] CYW43 init failed after 3 attempts.\n");
        while (true) {
            sleep_ms(1000);
        }
    }

    printf("[CYW43] Starting LED blink loop...\n");

    int count = 0;
    while (true) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(250);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(250);
        count++;
        if (count % 10 == 0) {
            printf("[CYW43] Blink count: %d\n", count);
        }
    }

    return 0;
}
