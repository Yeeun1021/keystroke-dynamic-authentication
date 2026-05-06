// main.c - Keystroke Dynamics Data Collector
// Board: nrf54l15dk/nrf54l15/cpuapp
// Logs timing data via UART in CSV format

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <inttypes.h>

LOG_MODULE_REGISTER(kda_collect, LOG_LEVEL_INF);

// nRF54L15 DK button definitions
#define BTN0_NODE DT_ALIAS(sw0)
#define BTN1_NODE DT_ALIAS(sw1)
#define BTN2_NODE DT_ALIAS(sw2)
#define BTN3_NODE DT_ALIAS(sw3)

static const struct gpio_dt_spec buttons[] = {
    GPIO_DT_SPEC_GET(BTN0_NODE, gpios),
    GPIO_DT_SPEC_GET(BTN1_NODE, gpios),
    GPIO_DT_SPEC_GET(BTN2_NODE, gpios),
    GPIO_DT_SPEC_GET(BTN3_NODE, gpios),
};

// Expected PIN sequence: 0 -> 1 -> 2 -> 3
static const int PIN_SEQUENCE[] = {0, 1, 2, 3};
#define PIN_LEN 4
#define NUM_TIMING_FEATURES 7  // 4 hold + 3 flight times

static int64_t press_time[PIN_LEN];
static int64_t release_time[PIN_LEN];
static int32_t features[NUM_TIMING_FEATURES];
static int seq_pos = 0;  // current position in PIN sequence

static struct gpio_callback btn_cb[4];

#define DEBOUNCE_MS 50

static void button_handler(const struct device *dev,
                           struct gpio_callback *cb, uint32_t pins)
{
    int64_t now = k_uptime_get();
    int btn_idx = -1;

    for (int i = 0; i < 4; i++) {
        if (pins & BIT(buttons[i].pin)) {
            btn_idx = i;
            break;
        }
    }
    if (btn_idx < 0) return;

    // Check it's the expected button in the sequence
    if (btn_idx != PIN_SEQUENCE[seq_pos]) {
        printk("# Wrong button! Expected %d, got %d. Resetting.\n",
               PIN_SEQUENCE[seq_pos], btn_idx            );
        seq_pos = 0;
        return;
    }

    int val = gpio_pin_get_dt(&buttons[btn_idx]);

// Debounce: ignore edges faster than DEBOUNCE_MS
    static int64_t last_event_time[4] = {0};
    if ((now - last_event_time[btn_idx]) < DEBOUNCE_MS) {
        return;
    }
    last_event_time[btn_idx] = now;


    if (val == 1) {
        // Button pressed
        press_time[seq_pos] = now;
    } else {
        // Only record release if we have a valid press
        if (press_time[seq_pos] == 0) return;
        release_time[seq_pos] = now;
        seq_pos++;

        if (seq_pos == PIN_LEN) {
            // Full PIN entered - compute features
            // Hold times (ms)
            for (int i = 0; i < PIN_LEN; i++) {
                features[i] = (int32_t)(release_time[i] - press_time[i]);
            }
            // Flight times: release[i] -> press[i+1]
            for (int i = 0; i < PIN_LEN - 1; i++) {
                features[PIN_LEN + i] = (int32_t)(press_time[i+1] - release_time[i]);
            }
             // Validity check
            bool valid = true;
            for (int i = 0; i < NUM_TIMING_FEATURES; i++) {
                if (features[i] <= 5 || features[i] > 5000) {
                    valid = false;
                    break;
                }
            }

            // Print as CSV: h0,h1,h2,h3,f01,f12,f23
            if (valid) {
            printk("%d,%d,%d,%d,%d,%d,%d\n",
                   (int)features[0], (int)features[1], (int)features[2], (int)features[3],
                   (int)features[4], (int)features[5], (int)features[6]);

            } else {
                printk("# Bad sample skipped\n");
            }

            seq_pos = 0;  // ready for next sample
            for (int i = 0; i < PIN_LEN; i++) {
                press_time[i] = 0;
                release_time[i] = 0;
            }
        }
    }
}

int main(void)
{
    printk("# Keystroke Dynamics Data Collector\n");
    printk("# Enter PIN sequence: BTN0 -> BTN1 -> BTN2 -> BTN3\n");
    printk("# Format: hold0,hold1,hold2,hold3,flight01,flight12,flight23\n");

    for (int i = 0; i < 4; i++) {
        if (!gpio_is_ready_dt(&buttons[i])) {
            printk("Button %d not ready!\n", i);
            return -1;
        }
        gpio_pin_configure_dt(&buttons[i], GPIO_INPUT);
        gpio_pin_interrupt_configure_dt(&buttons[i],
                                        GPIO_INT_EDGE_BOTH);
        gpio_init_callback(&btn_cb[i], button_handler,
                           BIT(buttons[i].pin));
        gpio_add_callback(buttons[i].port, &btn_cb[i]);
    }

    printk("# Ready! Start entering PIN sequences.\n");

    while (1) {
        k_sleep(K_MSEC(50));
    }
    return 0;
}