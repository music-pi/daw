#include "mk3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#define FLAG_FILE "/tmp/maschinepi-skip-boot"
#define TIMEOUT_SECONDS 10
#define REQUIRED_PRESSES 3
#define PRESS_WINDOW_MS 3000

static volatile int g_running = 1;
static mk3_t* g_mk3 = NULL;
static int shift_pressed = 0;
static int settings_press_count = 0;
static time_t first_press_time = 0;

static void signal_handler(int sig) {
    g_running = 0;
}

static void button_callback(const char* button_name, bool is_pressed, void* userdata) {
    if (strcmp(button_name, "shift") == 0) {
        shift_pressed = is_pressed ? 1 : 0;
        if (!is_pressed) {
            // Reset count if shift is released
            settings_press_count = 0;
            first_press_time = 0;
        }
        return;
    }
    
    if (strcmp(button_name, "settings") == 0 && is_pressed && shift_pressed) {
        time_t now = time(NULL);
        
        // Reset if too much time has passed since first press
        if (first_press_time > 0 && (now - first_press_time) * 1000 > PRESS_WINDOW_MS) {
            settings_press_count = 1;
            first_press_time = now;
        } else if (first_press_time == 0) {
            // First press
            settings_press_count = 1;
            first_press_time = now;
        } else {
            // Subsequent press within window
            settings_press_count++;
        }
        
        if (settings_press_count >= REQUIRED_PRESSES) {
            // Create flag file
            FILE* fp = fopen(FLAG_FILE, "w");
            if (fp) {
                fprintf(fp, "1\n");
                fclose(fp);
                printf("Boot skip flag created: %s\n", FLAG_FILE);
            }
            g_running = 0;
        }
    }
}

int main(int argc, char** argv) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Remove flag file if it exists from previous boot
    unlink(FLAG_FILE);
    
    printf("MK3 Boot Listener: Waiting for Shift+Settings (3x) to skip boot...\n");
    printf("Timeout: %d seconds\n", TIMEOUT_SECONDS);
    
    // Try to open MK3 device
    g_mk3 = mk3_open();
    if (!g_mk3) {
        fprintf(stderr, "Warning: Could not open MK3 device. Continuing without boot skip detection.\n");
        // Continue without detection - normal boot
        sleep(TIMEOUT_SECONDS);
        return 0;
    }
    
    printf("MK3 device opened successfully\n");
    
    // Register button callback
    mk3_input_set_button_callback(g_mk3, button_callback, NULL);
    
    time_t start_time = time(NULL);
    
    // Poll for input with timeout
    while (g_running) {
        time_t now = time(NULL);
        if ((now - start_time) >= TIMEOUT_SECONDS) {
            printf("Timeout reached. Proceeding with normal boot.\n");
            break;
        }
        
        // Poll for input events
        mk3_input_poll(g_mk3);
        
        // Small delay to avoid busy-waiting
        usleep(10000); // 10ms
    }
    
    mk3_close(g_mk3);
    return 0;
}

