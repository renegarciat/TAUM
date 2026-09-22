/* The almost useless machine
 *
 * A doorbell that doesn't ring when you press it, until you leave the
 * doorway. See README.md for the idea.
 *
 * Inputs:  a push button, an HC-SR04 ultrasonic distance sensor.
 * Output:  a buzzer.
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "sdkconfig.h"

static const char *TAG = "useless_machine";

#define BUTTON_GPIO CONFIG_BUTTON_GPIO
#define TRIG_GPIO   CONFIG_TRIG_GPIO
#define ECHO_GPIO   CONFIG_ECHO_GPIO
#define BUZZER_GPIO CONFIG_BUZZER_GPIO

#define LOOP_PERIOD_MS      100
#define BUTTON_DEBOUNCE_MS  30
#define PRESENCE_DEBOUNCE_SAMPLES 3   /* consecutive matching reads before believing a presence change */
#define ECHO_TIMEOUT_US     30000     /* ~5 m round trip; also covers "nothing in range" */
#define US_TO_CM            0.01715f  /* speed of sound (343 m/s) / 2, applied to round-trip us */

typedef enum {
    STATE_IDLE = 0,     /* nothing stored, waiting for a button press while someone is near */
    STATE_ARMED,        /* button was pressed while someone was near; waiting for them to leave */
    STATE_COUNTDOWN,    /* they left; timer running before the buzzer fires */
    STATE_BUZZING,       /* the buzzer is doing its belated job */
} machine_state_t;

static void configure_gpio(void)
{
    gpio_reset_pin(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);

    gpio_reset_pin(TRIG_GPIO);
    gpio_set_direction(TRIG_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(TRIG_GPIO, 0);

    gpio_reset_pin(ECHO_GPIO);
    gpio_set_direction(ECHO_GPIO, GPIO_MODE_INPUT);

    gpio_reset_pin(BUZZER_GPIO);
    gpio_set_direction(BUZZER_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BUZZER_GPIO, 0);
}

/* Blocking HC-SR04 read. Returns distance in cm, or -1 if no echo (out of range / no object). */
static float measure_distance_cm(void)
{
    gpio_set_level(TRIG_GPIO, 0);
    esp_rom_delay_us(2);
    gpio_set_level(TRIG_GPIO, 1);
    esp_rom_delay_us(10);
    gpio_set_level(TRIG_GPIO, 0);

    int64_t wait_start = esp_timer_get_time();
    while (gpio_get_level(ECHO_GPIO) == 0) {
        if (esp_timer_get_time() - wait_start > ECHO_TIMEOUT_US) {
            return -1.0f;
        }
    }

    int64_t echo_start = esp_timer_get_time();
    while (gpio_get_level(ECHO_GPIO) == 1) {
        if (esp_timer_get_time() - echo_start > ECHO_TIMEOUT_US) {
            return -1.0f;
        }
    }
    int64_t echo_end = esp_timer_get_time();

    return (float)(echo_end - echo_start) * US_TO_CM;
}

/* Edge-triggered, debounced button read. Returns true exactly once per physical press. */
static bool button_pressed_edge(void)
{
    static bool stable_state = true;      /* true = released (pulled up) */
    static bool last_raw = true;
    static int64_t last_change_us = 0;

    bool raw = gpio_get_level(BUTTON_GPIO);
    int64_t now = esp_timer_get_time();

    if (raw != last_raw) {
        last_raw = raw;
        last_change_us = now;
    }

    if (raw != stable_state && (now - last_change_us) >= (BUTTON_DEBOUNCE_MS * 1000)) {
        bool was_release_to_press = (stable_state == true && raw == false);
        stable_state = raw;
        return was_release_to_press;
    }
    return false;
}

/* Debounced presence read: requires several consecutive matching samples before flipping. */
static bool presence_debounced(float distance_cm)
{
    static bool confirmed = false;
    static bool candidate = false;
    static int matches = 0;

    bool raw_presence = (distance_cm > 0) && (distance_cm <= CONFIG_PRESENCE_THRESHOLD_CM);

    if (raw_presence == candidate) {
        if (matches < PRESENCE_DEBOUNCE_SAMPLES) {
            matches++;
        }
    } else {
        candidate = raw_presence;
        matches = 1;
    }

    if (matches >= PRESENCE_DEBOUNCE_SAMPLES) {
        confirmed = candidate;
    }
    return confirmed;
}

void app_main(void)
{
    configure_gpio();

    machine_state_t state = STATE_IDLE;
    int64_t countdown_deadline_us = 0;
    int64_t next_beep_toggle_us = 0;
    int beeps_remaining = 0;
    bool buzzer_on = false;

    ESP_LOGI(TAG, "The almost useless machine is ready. Press the button while standing close.");

    while (1) {
        float distance = measure_distance_cm();
        bool present = presence_debounced(distance);
        bool pressed = button_pressed_edge();

        switch (state) {
        case STATE_IDLE:
            if (pressed && present) {
                ESP_LOGI(TAG, "Button pressed while someone is near. Remembering that, doing nothing else.");
                state = STATE_ARMED;
            } else if (pressed) {
                ESP_LOGI(TAG, "Button pressed, but nobody's close enough. Ignored.");
            }
            break;

        case STATE_ARMED:
            if (!present) {
                ESP_LOGI(TAG, "They left. Starting the %d s countdown...", CONFIG_LEAVE_DELAY_SEC);
                countdown_deadline_us = esp_timer_get_time() + (int64_t)CONFIG_LEAVE_DELAY_SEC * 1000000;
                state = STATE_COUNTDOWN;
            }
            break;

        case STATE_COUNTDOWN:
            if (esp_timer_get_time() >= countdown_deadline_us) {
                ESP_LOGI(TAG, "Ding dong. Too little, too late.");
                beeps_remaining = CONFIG_BUZZER_BEEP_COUNT * 2; /* on+off per beep */
                next_beep_toggle_us = esp_timer_get_time();
                state = STATE_BUZZING;
            }
            break;

        case STATE_BUZZING:
            if (esp_timer_get_time() >= next_beep_toggle_us) {
                buzzer_on = !buzzer_on;
                gpio_set_level(BUZZER_GPIO, buzzer_on);
                next_beep_toggle_us += (int64_t)CONFIG_BUZZER_BEEP_MS * 1000;
                beeps_remaining--;
                if (beeps_remaining <= 0) {
                    gpio_set_level(BUZZER_GPIO, 0);
                    ESP_LOGI(TAG, "Done. Back to waiting for the next victim.");
                    state = STATE_IDLE;
                }
            }
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(LOOP_PERIOD_MS));
    }
}
