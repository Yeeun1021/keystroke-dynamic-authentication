#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "kda_model.h"

LOG_MODULE_REGISTER(kda_infer, LOG_LEVEL_INF);

// ── Scaler params from Colab Cell 3 ─────────────────────────────────────────
static const float SCALER_MEAN[] = {
    461.188f, 402.812f, 377.168f, 391.139f, 238.396f, 241.168f, 225.149f};
static const float SCALER_STD[] = {
    353.395f, 283.576f, 268.252f, 265.173f, 118.110f, 83.595f, 89.994f};
// ─────────────────────────────────────────────────────────────────────────────

#define PIN_LEN          4
#define NUM_FEATURES     7
#define DEBOUNCE_MS      50

static const struct gpio_dt_spec led_ok  =
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);  // green = authenticated
static const struct gpio_dt_spec led_bad =
    GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);  // red   = rejected

static const struct gpio_dt_spec buttons[] = {
    GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(sw2), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(sw3), gpios),
};
static struct gpio_callback btn_cb[4];

static const int PIN_SEQ[] = {0, 1, 2, 3};
static int64_t press_time[PIN_LEN];
static int64_t release_time[PIN_LEN];
static int32_t features[NUM_FEATURES];
static int     seq_pos = 0;
static bool    sample_ready = false;

constexpr int kTensorArenaSize = 20 * 1024;
static uint8_t tensor_arena[kTensorArenaSize] __attribute__((aligned(16)));
static tflite::MicroInterpreter* interpreter = nullptr;

static void button_handler(const struct device *dev,
                           struct gpio_callback *cb, uint32_t pins)
{
    if (sample_ready) return;
    int64_t now = k_uptime_get();

    static int64_t last_event[4] = {0};

    for (int i = 0; i < 4; i++) {
        if (!(pins & BIT(buttons[i].pin))) continue;

        if ((now - last_event[i]) < DEBOUNCE_MS) return;
        last_event[i] = now;

        if (i != PIN_SEQ[seq_pos]) {
            printk("# Wrong button, reset\n");
            seq_pos = 0;
            return;
        }

        int val = gpio_pin_get_dt(&buttons[i]);
        if (val == 1) {
            press_time[seq_pos] = now;
        } else {
            if (press_time[seq_pos] == 0) return;
            release_time[seq_pos] = now;
            seq_pos++;

            if (seq_pos == PIN_LEN) {
                for (int j = 0; j < PIN_LEN; j++)
                    features[j] = (int32_t)(release_time[j] - press_time[j]);
                for (int j = 0; j < PIN_LEN - 1; j++)
                    features[PIN_LEN + j] = (int32_t)(press_time[j+1] - release_time[j]);

                seq_pos = 0;
                for (int j = 0; j < PIN_LEN; j++)
                    press_time[j] = release_time[j] = 0;
                sample_ready = true;
            }
        }
        break;
    }
}

static void run_inference(void)
{
    TfLiteTensor* input = interpreter->input(0);
    for (int i = 0; i < NUM_FEATURES; i++) {
        input->data.f[i] = ((float)features[i] - SCALER_MEAN[i]) / SCALER_STD[i];
    }

    if (interpreter->Invoke() != kTfLiteOk) {
        printk("Inference failed!\n");
        return;
    }

    TfLiteTensor* output = interpreter->output(0);

    // Find highest scoring user
    int predicted_user = 0;
    float max_score = output->data.f[0];
    for (int i = 1; i < 4; i++) {
        if (output->data.f[i] > max_score) {
            max_score = output->data.f[i];
            predicted_user = i;
        }
    }

    // Print all scores
    printk("Scores: U3=%.0f%% U4=%.0f%% U5=%.0f%% U6=%.0f%%\n",
           (double)(output->data.f[0]*100), (double)(output->data.f[1]*100),
           (double)(output->data.f[2]*100), (double)(output->data.f[3]*100));

    if (max_score > 0.7f) {
        printk("IDENTIFIED as User%d\n", predicted_user + 3);
        gpio_pin_set_dt(&led_bad, 1);
        k_sleep(K_MSEC(2000));
        gpio_pin_set_dt(&led_bad, 0);
    } else {
        printk("UNKNOWN USER - rejected\n");
        gpio_pin_set_dt(&led_bad, 1);
        k_sleep(K_MSEC(2000));
        gpio_pin_set_dt(&led_bad, 0);
    }
}

int main(void)
{
    tflite::InitializeTarget();

    gpio_pin_configure_dt(&led_ok,  GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_bad, GPIO_OUTPUT_INACTIVE);

    for (int i = 0; i < 4; i++) {
        gpio_pin_configure_dt(&buttons[i], GPIO_INPUT);
        gpio_pin_interrupt_configure_dt(&buttons[i], GPIO_INT_EDGE_BOTH);
        gpio_init_callback(&btn_cb[i], button_handler, BIT(buttons[i].pin));
        gpio_add_callback(buttons[i].port, &btn_cb[i]);
    }

    const tflite::Model* model = tflite::GetModel(kda_model_data);

    static tflite::MicroMutableOpResolver<6> resolver;
    resolver.AddFullyConnected();
    resolver.AddRelu();
    resolver.AddSoftmax();
    resolver.AddReshape();
    resolver.AddQuantize();
    resolver.AddDequantize();



    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, kTensorArenaSize);
    interpreter = &static_interpreter;
    interpreter->AllocateTensors();

    printk("KDA Inference ready.\n");
    printk("Enter PIN: BTN0 -> BTN1 -> BTN2 -> BTN3\n");

    while (1) {
        if (sample_ready) {
            run_inference();
            sample_ready = false;
        }
        k_sleep(K_MSEC(10));
    }
    return 0;
}