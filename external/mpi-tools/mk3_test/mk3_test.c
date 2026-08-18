#include "mk3.h"
#include "mk3_display.h"
#include "mk3_input.h"
#include "mk3_output.h"      // Include for LED functions
#include "mk3_output_map.h"  // Include for LED definitions
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>            // For srand and rand

void run_input_test(mk3_t* dev);
void run_partial_test(mk3_t* dev);
void run_output_test(mk3_t* dev); // Declare new test function

void print_help() {
    printf("Usage: mk3_test [--input | --partial | --output]\n");
    printf("  --input     Run input polling test\n");
    printf("  --partial   Run partial display rendering test\n");
    printf("  --output    Run LED output test\n");
    printf("  --help      Show this help message\n");
}

int main(int argc, char** argv) {
    int do_input = 0;
    int do_partial = 0;
    int do_output = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--input")) do_input = 1;
        else if (!strcmp(argv[i], "--partial")) do_partial = 1;
        else if (!strcmp(argv[i], "--output")) do_output = 1;
        else if (!strcmp(argv[i], "--help")) {
            print_help();
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n\n", argv[i]);
            print_help();
            return 1;
        }
    }

    if ((do_input + do_partial + do_output) != 1) {
        print_help();
        return 1;
    }

    mk3_t* dev = mk3_open();
    if (!dev) {
        fprintf(stderr, "❌ Could not open MK3 device.\n");
        return 1;
    }

    if (do_input) run_input_test(dev);
    if (do_partial) run_partial_test(dev);
    if (do_output) run_output_test(dev);

    mk3_close(dev);
    return 0;
}

void run_output_test(mk3_t* dev) {
    printf("💡 Starting MK3 LED output test (press Ctrl+C to stop)...\n");
    srand(time(NULL)); // Seed random number generator

    // Separate LED names by type for easier random selection
    const char* mono_led_names[mk3_leds_count];
    int mono_led_count = 0;
    const char* indexed_led_names[mk3_leds_count];
    int indexed_led_count = 0;

    for (int i = 0; i < mk3_leds_count; ++i) {
        if (mk3_leds[i].type == MK3_LED_TYPE_MONO) {
            mono_led_names[mono_led_count++] = mk3_leds[i].name;
        } else if (mk3_leds[i].type == MK3_LED_TYPE_INDEXED) {
            indexed_led_names[indexed_led_count++] = mk3_leds[i].name;
        }
    }

    while (1) {
        // Toggle a random mono LED
        if (mono_led_count > 0) {
            int rand_mono_idx = rand() % mono_led_count;
            const char* led_name = mono_led_names[rand_mono_idx];
            uint8_t brightness = (rand() % 2) * 63; // 0 or 63
            printf("Setting mono LED '%s' to brightness %d\n", led_name, brightness);
            mk3_led_set_brightness(dev, led_name, brightness);
        }

        // Set a random indexed LED to a random color
        if (indexed_led_count > 0) {
            int rand_indexed_idx = rand() % indexed_led_count;
            const char* led_name = indexed_led_names[rand_indexed_idx];
            // Valid indices: 0 (off), 4-67 (colors), 68-71 (whites)
            uint8_t color_index = rand() % 72; 
            printf("Setting indexed LED '%s' to color_index %d\n", led_name, color_index);
            mk3_led_set_indexed_color(dev, led_name, color_index);
        }

        usleep(100000); // 100ms delay

        // Occasionally turn all off (e.g. every 50 iterations)
        if (rand() % 50 == 0) {
            printf("Clearing all LEDs (setting to 0)...\n");
            for(int i=0; i < mono_led_count; ++i) mk3_led_set_brightness(dev, mono_led_names[i], 0);
            for(int i=0; i < indexed_led_count; ++i) mk3_led_set_indexed_color(dev, indexed_led_names[i], 0);
            usleep(500000); // Pause after clearing
        }
    }
}
