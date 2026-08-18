/**
 * MK3 Boot Display - Minimal boot status on Maschine MK3 screens
 *
 * Shows "Booting..." as early as possible, then "Booted" with IP once ready.
 */

#include "mk3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>

// Display dimensions
#define WIDTH 480
#define HEIGHT 272

// RGB565 colors
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_GREEN     0x07E0
#define COLOR_CYAN      0x07FF
#define COLOR_ORANGE    0xFD20

// Simple 8x8 font - just the characters we need
static const unsigned char font_8x8[][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0: space
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // 1: .
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}, // 2: :
    {0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, // 3: A
    {0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00}, // 4: B
    {0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00}, // 5: D
    {0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00}, // 6: E
    {0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3A,0x00}, // 7: G
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 8: I
    {0x7C,0xC6,0xCE,0xDE,0xF6,0xE6,0x7C,0x00}, // 9: O  (also used for 0)
    {0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00}, // 10: P
    {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00}, // 11: S (also 5)
    {0x7E,0x7E,0x5A,0x18,0x18,0x18,0x3C,0x00}, // 12: T
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, // 13: U
    {0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00}, // 14: e
    {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00}, // 15: X (also x)
    {0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00}, // 16: n
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00}, // 17: o
    {0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00}, // 18: u
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, // 19: 1
    {0x7C,0xC6,0x06,0x1C,0x30,0x66,0xFE,0x00}, // 20: 2
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00}, // 21: 4
    {0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00}, // 22: 6
    {0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00}, // 23: 7
    {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00}, // 24: 8
    {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00}, // 25: 9
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0xF8}, // 26: g
    {0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00}, // 27: a
    {0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00}, // 28: r
    {0x30,0x30,0xFC,0x30,0x30,0x36,0x1C,0x00}, // 29: t
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 30: l
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, // 31: i
    {0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0x00}, // 32: m
    {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00}, // 33: h
    {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00}, // 34: 3
    {0x77,0xCC,0xCC,0xCF,0xCC,0xCC,0x77,0x00}, // 35: M
    {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00}, // 36: C
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00}, // 37: N
    {0xC6,0xC6,0xC6,0xD6,0xD6,0xFE,0x6C,0x00}, // 38: W
    {0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00}, // 39: b
    {0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00}, // 40: c
    {0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00}, // 41: d
    {0x00,0x00,0x7E,0xC0,0x7C,0x06,0xFC,0x00}, // 42: s
    {0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0xFC}, // 43: y
    {0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00}, // 44: k
    {0x3C,0x66,0x60,0xF8,0x60,0x60,0xF0,0x00}, // 45: f
    {0xC6,0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0x00}, // 46: X upper
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, // 47: -
};

// Character lookup table
static int char_to_idx(char c) {
    switch (c) {
        case ' ': return 0;
        case '.': return 1;
        case ':': return 2;
        case 'A': return 3;
        case 'B': return 4;
        case 'D': return 5;
        case 'E': return 6;
        case 'G': return 7;
        case 'I': return 8;
        case 'O': case '0': return 9;
        case 'P': return 10;
        case 'S': case '5': return 11;
        case 'T': return 12;
        case 'U': return 13;
        case 'e': return 14;
        case 'X': case 'x': return 15;
        case 'n': return 16;
        case 'o': return 17;
        case 'u': return 18;
        case '1': return 19;
        case '2': return 20;
        case '4': return 21;
        case '6': return 22;
        case '7': return 23;
        case '8': return 24;
        case '9': return 25;
        case 'g': return 26;
        case 'a': return 27;
        case 'r': return 28;
        case 't': return 29;
        case 'l': return 30;
        case 'i': return 31;
        case 'm': return 32;
        case 'h': return 33;
        case '3': return 34;
        case 'M': return 35;
        case 'C': return 36;
        case 'N': return 37;
        case 'W': return 38;
        case 'b': return 39;
        case 'c': return 40;
        case 'd': return 41;
        case 's': return 42;
        case 'y': return 43;
        case 'k': return 44;
        case 'f': return 45;
        case '-': return 47;
        default: return 0;  // space for unknown
    }
}

static volatile int g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

// Draw text with 2x scaling
static void draw_text_2x(uint16_t* fb, int x, int y, const char* text, uint16_t fg, uint16_t bg) {
    int cx = x;
    while (*text && cx < WIDTH - 16) {
        int idx = char_to_idx(*text);
        const unsigned char* glyph = font_8x8[idx];

        for (int row = 0; row < 8; row++) {
            unsigned char bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                uint16_t color = (bits & (0x80 >> col)) ? fg : bg;
                // Draw 2x2 pixel
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        int py = y + row * 2 + dy;
                        int px = cx + col * 2 + dx;
                        if (py >= 0 && py < HEIGHT && px >= 0 && px < WIDTH) {
                            fb[py * WIDTH + px] = color;
                        }
                    }
                }
            }
        }
        cx += 16;
        text++;
    }
}

// Draw text with 3x scaling for big messages
static void draw_text_3x(uint16_t* fb, int x, int y, const char* text, uint16_t fg, uint16_t bg) {
    int cx = x;
    while (*text && cx < WIDTH - 24) {
        int idx = char_to_idx(*text);
        const unsigned char* glyph = font_8x8[idx];

        for (int row = 0; row < 8; row++) {
            unsigned char bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                uint16_t color = (bits & (0x80 >> col)) ? fg : bg;
                // Draw 3x3 pixel
                for (int dy = 0; dy < 3; dy++) {
                    for (int dx = 0; dx < 3; dx++) {
                        int py = y + row * 3 + dy;
                        int px = cx + col * 3 + dx;
                        if (py >= 0 && py < HEIGHT && px >= 0 && px < WIDTH) {
                            fb[py * WIDTH + px] = color;
                        }
                    }
                }
            }
        }
        cx += 24;
        text++;
    }
}

// Fill screen with color
static void clear_screen(uint16_t* fb, uint16_t color) {
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        fb[i] = color;
    }
}

// Get first non-loopback IP address
static void get_local_ip(char* ip_out, size_t len) {
    struct ifaddrs *ifaddr, *ifa;
    ip_out[0] = '\0';

    if (getifaddrs(&ifaddr) == -1) {
        return;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;

        // Skip loopback
        if (strcmp(ifa->ifa_name, "lo") == 0) continue;

        struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &addr->sin_addr, ip_out, len);
        break;
    }

    freeifaddrs(ifaddr);
}

// Check if system has finished booting
static int is_system_ready(void) {
    FILE* fp = popen("systemctl is-system-running 2>/dev/null", "r");
    if (fp) {
        char buf[32];
        if (fgets(buf, sizeof(buf), fp)) {
            pclose(fp);
            // "running" or "degraded" means boot is complete
            return (strncmp(buf, "running", 7) == 0 || strncmp(buf, "degraded", 8) == 0);
        }
        pclose(fp);
    }
    return 0;
}

// Check if maschinepi service is active
static int is_maschinepi_running(void) {
    FILE* fp = popen("systemctl is-active maschinepi.service 2>/dev/null", "r");
    if (fp) {
        char buf[32];
        if (fgets(buf, sizeof(buf), fp)) {
            pclose(fp);
            return strncmp(buf, "active", 6) == 0;
        }
        pclose(fp);
    }
    return 0;
}

// Show a simple message on both screens
static void show_message(mk3_t* mk3, uint16_t* fb_left, uint16_t* fb_right,
                         const char* line1, const char* line2, uint16_t color1, uint16_t color2) {
    clear_screen(fb_left, COLOR_BLACK);
    clear_screen(fb_right, COLOR_BLACK);

    if (line1 && line1[0]) {
        draw_text_3x(fb_left, 40, 80, line1, color1, COLOR_BLACK);
        draw_text_3x(fb_right, 40, 80, line1, color1, COLOR_BLACK);
    }
    if (line2 && line2[0]) {
        draw_text_2x(fb_left, 40, 140, line2, color2, COLOR_BLACK);
        draw_text_2x(fb_right, 40, 140, line2, color2, COLOR_BLACK);
    }

    mk3_display_draw(mk3, 0, fb_left);
    mk3_display_draw(mk3, 1, fb_right);
}

// Message mode - show a message and optionally wait
static int run_message_mode(const char* line1, const char* line2, int duration) {
    mk3_t* mk3 = NULL;

    // Try to open MK3 - quick retry (5 seconds max)
    for (int i = 0; i < 5 && !mk3; i++) {
        mk3 = mk3_open();
        if (!mk3) sleep(1);
    }

    if (!mk3) {
        fprintf(stderr, "Could not open MK3 device.\n");
        return 1;
    }

    uint16_t* fb_left = calloc(WIDTH * HEIGHT, sizeof(uint16_t));
    uint16_t* fb_right = calloc(WIDTH * HEIGHT, sizeof(uint16_t));
    if (!fb_left || !fb_right) {
        mk3_close(mk3);
        return 1;
    }

    show_message(mk3, fb_left, fb_right, line1, line2, COLOR_WHITE, COLOR_CYAN);

    // If duration > 0, wait that many seconds
    // If duration == 0, wait forever (until killed)
    // If duration < 0, show and exit immediately
    if (duration > 0) {
        for (int i = 0; i < duration && g_running; i++) {
            sleep(1);
        }
    } else if (duration == 0) {
        while (g_running) {
            sleep(1);
        }
    }
    // duration < 0: just exit

    // Clear and close
    clear_screen(fb_left, COLOR_BLACK);
    clear_screen(fb_right, COLOR_BLACK);
    mk3_display_draw(mk3, 0, fb_left);
    mk3_display_draw(mk3, 1, fb_right);

    free(fb_left);
    free(fb_right);
    mk3_close(mk3);

    return 0;
}

// Boot monitor mode - original behavior
static int run_boot_mode(void) {
    printf("MK3 Boot Display starting...\n");

    // Try to open MK3 device - retry for 60 seconds
    mk3_t* mk3 = NULL;
    for (int i = 0; i < 60 && !mk3 && g_running; i++) {
        mk3 = mk3_open();
        if (!mk3) {
            printf("Waiting for MK3... (%d/60)\n", i + 1);
            sleep(1);
        }
    }

    if (!mk3) {
        fprintf(stderr, "Could not open MK3 device. Exiting.\n");
        return 1;
    }

    printf("MK3 opened.\n");

    // Allocate framebuffers
    uint16_t* fb_left = calloc(WIDTH * HEIGHT, sizeof(uint16_t));
    uint16_t* fb_right = calloc(WIDTH * HEIGHT, sizeof(uint16_t));
    if (!fb_left || !fb_right) {
        fprintf(stderr, "Failed to allocate framebuffers\n");
        mk3_close(mk3);
        return 1;
    }

    // Show "Booting..." immediately
    clear_screen(fb_left, COLOR_BLACK);
    clear_screen(fb_right, COLOR_BLACK);
    draw_text_3x(fb_left, 80, 100, "Booting...", COLOR_WHITE, COLOR_BLACK);
    mk3_display_draw(mk3, 0, fb_left);
    mk3_display_draw(mk3, 1, fb_right);

    printf("Showing 'Booting...' on display\n");

    // Wait for boot to complete or maschinepi to start
    int booted = 0;
    for (int elapsed = 0; elapsed < 180 && g_running; elapsed++) {
        // Check if maschinepi is running - exit and let it take over
        if (is_maschinepi_running()) {
            printf("MaschinePI is running. Exiting.\n");
            break;
        }

        // Check if system has booted
        if (!booted && is_system_ready()) {
            booted = 1;
            printf("System ready, showing IP\n");

            // Get IP address
            char ip[64] = "No network";
            get_local_ip(ip, sizeof(ip));
            if (ip[0] == '\0') {
                strcpy(ip, "No network");
            }

            // Show booted message with IP
            clear_screen(fb_left, COLOR_BLACK);
            draw_text_3x(fb_left, 100, 60, "Booted", COLOR_GREEN, COLOR_BLACK);

            // Show IP
            char ip_line[80];
            snprintf(ip_line, sizeof(ip_line), "IP: %s", ip);
            draw_text_2x(fb_left, 60, 140, ip_line, COLOR_CYAN, COLOR_BLACK);

            // Show user info
            draw_text_2x(fb_left, 60, 180, "User: mpi", COLOR_ORANGE, COLOR_BLACK);

            mk3_display_draw(mk3, 0, fb_left);

            // Also show on right screen
            clear_screen(fb_right, COLOR_BLACK);
            draw_text_2x(fb_right, 60, 80, "MaschinePI", COLOR_WHITE, COLOR_BLACK);
            draw_text_2x(fb_right, 60, 120, "Starting...", COLOR_CYAN, COLOR_BLACK);
            mk3_display_draw(mk3, 1, fb_right);
        }

        sleep(1);
    }

    // Cleanup
    clear_screen(fb_left, COLOR_BLACK);
    clear_screen(fb_right, COLOR_BLACK);
    mk3_display_draw(mk3, 0, fb_left);
    mk3_display_draw(mk3, 1, fb_right);

    free(fb_left);
    free(fb_right);
    mk3_close(mk3);

    printf("MK3 Boot Display exiting.\n");
    return 0;
}

static void print_usage(const char* prog) {
    printf("Usage: %s [options] [message] [line2]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  -t SECONDS   Display duration (0=forever, -1=instant, default=0)\n");
    printf("  -h           Show this help\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s                          # Boot monitor mode\n", prog);
    printf("  %s \"Installing...\"          # Show message until killed\n", prog);
    printf("  %s \"Done\" \"Rebooting\" -t 3  # Show for 3 seconds\n", prog);
    printf("  %s \"Status\" -t -1           # Flash message and exit\n", prog);
}

int main(int argc, char** argv) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    const char* line1 = NULL;
    const char* line2 = NULL;
    int duration = 0;

    // Parse arguments
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            duration = atoi(argv[++i]);
        } else if (argv[i][0] != '-') {
            if (!line1) {
                line1 = argv[i];
            } else if (!line2) {
                line2 = argv[i];
            }
        }
        i++;
    }

    // If no message arguments, run boot monitor mode
    if (!line1) {
        return run_boot_mode();
    }

    // Message mode
    return run_message_mode(line1, line2 ? line2 : "", duration);
}
