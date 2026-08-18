#include "mk3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>

// Display dimensions
#define WIDTH 480
#define HEIGHT 272
#define SCREEN_LEFT 0

// RGB565 color definitions
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_GRAY    0x8410
#define COLOR_DARK_GRAY 0x4208
#define COLOR_ORANGE  0xFC00

static volatile int g_running = 1;
static mk3_t* g_mk3 = NULL;
static int selected_option = 0;
static int in_submenu = 0;
static int submenu_option = 0;

enum MainMenuOption {
    OPTION_CONTINUE_BOOT = 0,
    OPTION_SETUP = 1,
    OPTION_DROP_TO_SHELL = 2,
    OPTION_FORCE_REBOOT = 3,
    OPTION_COUNT = 4
};

enum SetupSubmenuOption {
    SUBOPTION_WIFI_CONFIG = 0,
    SUBOPTION_RASPI_CONFIG = 1,
    SUBOPTION_BACK = 2,
    SUBOPTION_COUNT = 3
};

static void signal_handler(int sig) {
    g_running = 0;
}

static void clear_screen(uint16_t* buffer, uint16_t color) {
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        buffer[i] = color;
    }
}

static void draw_rect(uint16_t* buffer, int x, int y, int w, int h, uint16_t color) {
    for (int py = 0; py < h && (y + py) < HEIGHT; py++) {
        for (int px = 0; px < w && (x + px) < WIDTH; px++) {
            int idx = (y + py) * WIDTH + (x + px);
            buffer[idx] = color;
        }
    }
}

static void draw_text_simple(uint16_t* buffer, int x, int y, const char* text, uint16_t fg, uint16_t bg, int scale) {
    // Simple 8x8 font rendering (scaled)
    int cx = x;
    for (const char* p = text; *p && cx < WIDTH - 8 * scale; p++) {
        // Draw a simple character representation
        for (int sy = 0; sy < 8 * scale && (y + sy) < HEIGHT; sy++) {
            for (int sx = 0; sx < 8 * scale && (x + sx) < WIDTH; sx++) {
                int idx = (y + sy) * WIDTH + (cx + sx);
                if (*p != ' ') {
                    buffer[idx] = fg;
                } else {
                    buffer[idx] = bg;
                }
            }
        }
        cx += 8 * scale;
    }
}

static void draw_menu(uint16_t* buffer) {
    clear_screen(buffer, COLOR_BLACK);
    
    if (!in_submenu) {
        // Main menu
        draw_text_simple(buffer, 20, 20, "MaschinePI System Config", COLOR_WHITE, COLOR_BLACK, 2);
        
        const char* options[] = {
            "1. Continue Boot",
            "2. Setup",
            "3. Drop to Shell",
            "4. Force Reboot"
        };
        
        uint16_t option_colors[] = {
            COLOR_GREEN,
            COLOR_CYAN,
            COLOR_YELLOW,
            COLOR_RED
        };
        
        int y_start = 80;
        int line_height = 40;
        
        for (int i = 0; i < OPTION_COUNT; i++) {
            int y = y_start + i * line_height;
            uint16_t bg = (i == selected_option) ? option_colors[i] : COLOR_BLACK;
            uint16_t fg = (i == selected_option) ? COLOR_BLACK : option_colors[i];
            
            // Draw selection highlight
            if (i == selected_option) {
                draw_rect(buffer, 10, y - 5, WIDTH - 20, line_height - 5, option_colors[i]);
            }
            
            draw_text_simple(buffer, 20, y, options[i], fg, bg, 2);
        }
        
        draw_text_simple(buffer, 20, HEIGHT - 30, "Use NAV buttons to select, NAV PUSH to confirm", COLOR_GRAY, COLOR_BLACK, 1);
    } else {
        // Setup submenu
        draw_text_simple(buffer, 20, 20, "Setup Menu", COLOR_WHITE, COLOR_BLACK, 2);
        
        const char* suboptions[] = {
            "2.1 WiFi Config",
            "2.2 Run Raspi Config",
            "Back"
        };
        
        uint16_t suboption_colors[] = {
            COLOR_CYAN,
            COLOR_CYAN,
            COLOR_GRAY
        };
        
        int y_start = 80;
        int line_height = 40;
        
        for (int i = 0; i < SUBOPTION_COUNT; i++) {
            int y = y_start + i * line_height;
            uint16_t bg = (i == submenu_option) ? suboption_colors[i] : COLOR_BLACK;
            uint16_t fg = (i == submenu_option) ? COLOR_BLACK : suboption_colors[i];
            
            // Draw selection highlight
            if (i == submenu_option) {
                draw_rect(buffer, 10, y - 5, WIDTH - 20, line_height - 5, suboption_colors[i]);
            }
            
            draw_text_simple(buffer, 20, y, suboptions[i], fg, bg, 2);
        }
        
        draw_text_simple(buffer, 20, HEIGHT - 30, "Use NAV buttons to select, NAV PUSH to confirm", COLOR_GRAY, COLOR_BLACK, 1);
    }
}

static void execute_action() {
    if (!in_submenu) {
        switch (selected_option) {
            case OPTION_CONTINUE_BOOT:
                printf("Continuing boot...\n");
                g_running = 0;
                break;
            case OPTION_SETUP:
                in_submenu = 1;
                submenu_option = 0;
                break;
            case OPTION_DROP_TO_SHELL:
                printf("Dropping to shell...\n");
                system("systemctl stop maschinepi.service 2>/dev/null || true");
                system("systemctl disable maschinepi.service 2>/dev/null || true");
                // Keep running but don't launch maschinepi
                g_running = 0;
                break;
            case OPTION_FORCE_REBOOT:
                printf("Rebooting system...\n");
                system("reboot");
                break;
        }
    } else {
        switch (submenu_option) {
            case SUBOPTION_WIFI_CONFIG:
                printf("Launching WiFi config...\n");
                system("raspi-config nonint do_wifi_country 2>/dev/null || nmtui 2>/dev/null || true");
                // Return to main menu after WiFi config
                in_submenu = 0;
                break;
            case SUBOPTION_RASPI_CONFIG:
                printf("Launching raspi-config with controller input emulation...\n");
                // For now, just launch raspi-config normally
                // TODO: Add input emulation from controller
                system("raspi-config");
                in_submenu = 0;
                break;
            case SUBOPTION_BACK:
                in_submenu = 0;
                break;
        }
    }
}

static void button_callback(const char* button_name, bool is_pressed, void* userdata) {
    if (!is_pressed) return;
    
    if (strcmp(button_name, "navUp") == 0) {
        if (!in_submenu) {
            selected_option = (selected_option - 1 + OPTION_COUNT) % OPTION_COUNT;
        } else {
            submenu_option = (submenu_option - 1 + SUBOPTION_COUNT) % SUBOPTION_COUNT;
        }
    } else if (strcmp(button_name, "navDown") == 0) {
        if (!in_submenu) {
            selected_option = (selected_option + 1) % OPTION_COUNT;
        } else {
            submenu_option = (submenu_option + 1) % SUBOPTION_COUNT;
        }
    } else if (strcmp(button_name, "navPush") == 0) {
        execute_action();
    } else if (strcmp(button_name, "navLeft") == 0 && in_submenu) {
        // Go back from submenu
        in_submenu = 0;
    }
}

int main(int argc, char** argv) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("MaschinePI System Config\n");
    
    // Open MK3 device
    g_mk3 = mk3_open();
    if (!g_mk3) {
        fprintf(stderr, "Error: Could not open MK3 device.\n");
        return 1;
    }
    
    printf("MK3 device opened successfully\n");
    
    // Register button callback
    mk3_input_set_button_callback(g_mk3, button_callback, NULL);
    
    uint16_t* framebuffer = calloc(WIDTH * HEIGHT, sizeof(uint16_t));
    if (!framebuffer) {
        fprintf(stderr, "Failed to allocate framebuffer\n");
        mk3_close(g_mk3);
        return 1;
    }
    
    // Initial draw
    draw_menu(framebuffer);
    mk3_display_draw(g_mk3, SCREEN_LEFT, framebuffer);
    
    // Main loop
    while (g_running) {
        // Poll for input
        mk3_input_poll(g_mk3);
        
        // Redraw menu (selection may have changed)
        draw_menu(framebuffer);
        mk3_display_draw(g_mk3, SCREEN_LEFT, framebuffer);
        
        usleep(50000); // 50ms
    }
    
    // Cleanup
    clear_screen(framebuffer, COLOR_BLACK);
    mk3_display_draw(g_mk3, SCREEN_LEFT, framebuffer);
    
    free(framebuffer);
    mk3_close(g_mk3);
    
    return 0;
}

