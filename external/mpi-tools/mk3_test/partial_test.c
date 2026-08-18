#include "mk3.h"
#include "mk3_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void run_partial_test(mk3_t* dev) {
    mk3_display_clear(dev, 0, 0x0000);

    uint16_t colors[] = {
        0xf800, 0x07e0, 0x001f, 0xffe0, 0xf81f, 0x07ff, 0xffff
    };
    int color_index = 0;

    int sizes[] = {480, 240, 128, 64, 32, 16, 8, 4, 2};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    int fps_values[] = {10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60};
    int num_fps = sizeof(fps_values) / sizeof(fps_values[0]);

    for (int f = 0; f < num_fps; f++) {
        int delay_us = 1000000 / fps_values[f];
        printf("\n🚀 Testing at %d FPS (delay %dus)\n", fps_values[f], delay_us);

        for (int s = 0; s < num_sizes; s++) {
            int step = sizes[s];
            uint16_t COLOR = colors[color_index];
            printf("🔁 Region size: %dx%d\n", step, step);

            for (int y = 0; y < 272; y += step) {
                int h = (y + step <= 272) ? step : (272 - y);
                if (h <= 0) continue;

                for (int x = 0; x < 480; x += step) {
                    int w = (x + step <= 480) ? step : (480 - x);
                    if (w <= 0) continue;

                    uint16_t* buf = calloc(w * h, sizeof(uint16_t));
                    if (!buf) {
                        printf("❌ Memory allocation failed\n");
                        return;
                    }

                    for (int i = 0; i < w * h; i++) buf[i] = COLOR;

                    printf("Testing x=%3d y=%3d w=%3d h=%3d... ", x, y, w, h);
                    fflush(stdout);

                    int res = mk3_display_draw_partial(dev, 0, x, y, w, h, buf);
                    if (res != 0) {
                        printf("❌ Region FAILED! Exiting. x=%d y=%d w=%d h=%d\n", x, y, w, h);
                        free(buf);
                        return;
                    } else {
                        printf("✅ OK\n");
                    }

                    free(buf);
                    usleep(delay_us);
                }
            }

            color_index = (color_index + 1) % (sizeof(colors) / sizeof(colors[0]));
        }
    }

    printf("\n✅ All test rounds completed without error!\n");
}
