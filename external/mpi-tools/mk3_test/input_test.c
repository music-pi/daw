#include "mk3.h"
#include "mk3_internal.h"
#include "mk3_input.h"
#include <stdio.h>
#include <unistd.h>

static void pad_event_logger(uint8_t pad_number, bool is_pressed, uint16_t pressure, void* userdata) {
    (void)userdata;
    printf("🐾 Pad %02u %s (pressure %u)\n", pad_number, is_pressed ? "pressed" : "released", pressure);
    fflush(stdout);
}

static void button_event_logger(const char* button_name, bool is_pressed, void* userdata) {
    (void)userdata;
    printf("🔘 Button %s %s\n", button_name, is_pressed ? "pressed" : "released");
    fflush(stdout);
}

static void knob_event_logger(const char* knob_name, int16_t delta, uint16_t absolute_value, void* userdata) {
    (void)userdata;
    printf("🎚️ Knob %s %+d (value %u)\n", knob_name, delta, absolute_value);
    fflush(stdout);
}

static void stepper_event_logger(int8_t direction, uint8_t position, void* userdata) {
    (void)userdata;
    const char* direction_label = direction > 0 ? "clockwise" : "counter-clockwise";
    printf("🧭 Nav wheel %s (pos %u)\n", direction_label, position);
    fflush(stdout);
}

int run_input_test(mk3_t* dev) {
    if (!dev || !dev->handle) {
        fprintf(stderr, "Invalid MK3 device handle\n");
        return -1;
    }

    mk3_input_set_pad_callback(dev, pad_event_logger, NULL);
    mk3_input_set_button_callback(dev, button_event_logger, NULL);
    mk3_input_set_knob_callback(dev, knob_event_logger, NULL);
    mk3_input_set_stepper_callback(dev, stepper_event_logger, NULL);

    printf("📥 Starting MK3 input polling (press Ctrl+C to stop)...\n");

    while (1) {
        int result = mk3_input_poll(dev);
        if (result < 0) {
            fprintf(stderr, "⚠️  Input poll failed\n");
            break;
        }
        usleep(1000);  // Prevent busy-waiting
    }

    return 0;
}
