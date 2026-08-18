/*
 * mk3_emu.c - Socket-based emulator backend for libmk3
 *
 * Drop-in replacement for the real libmk3 USB driver. Connects to a
 * TCP server (the Python emulator) on localhost:9999 and exchanges
 * input/output events using a simple binary protocol.
 *
 * Protocol: All messages framed as [type:u8][length:u32 BE][payload]
 */

#include "../mk3/mk3.h"
#include "../mk3/mk3_display.h"
#include "../mk3/mk3_output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define EMU_PORT 9999
#define EMU_HOST "127.0.0.1"

#define DISPLAY_WIDTH  480
#define DISPLAY_HEIGHT 272

/* Message types: emulator -> app (input) */
#define MSG_BUTTON  0x01
#define MSG_PAD     0x02
#define MSG_KNOB    0x03
#define MSG_STEPPER 0x04

/* Message types: app -> emulator (output) */
#define MSG_DISPLAY     0x10
#define MSG_LED_MONO    0x11
#define MSG_LED_INDEXED 0x12

/* Read buffer must hold at least one complete input message */
#define READ_BUF_SIZE 4096

/*
 * Internal mk3_t definition for the emulator backend.
 * The real libmk3 defines this in mk3_internal.h with USB handles;
 * here we use a TCP socket instead.
 */
struct mk3 {
    int sock_fd;

    /* Callbacks (same as real libmk3) */
    mk3_pad_callback_t     pad_callback;
    void*                  pad_callback_userdata;
    mk3_button_callback_t  button_callback;
    void*                  button_callback_userdata;
    mk3_knob_callback_t   knob_callback;
    void*                  knob_callback_userdata;
    mk3_stepper_callback_t stepper_callback;
    void*                  stepper_callback_userdata;

    /* Buffered reader for partial TCP messages */
    uint8_t read_buf[READ_BUF_SIZE];
    int     read_buf_len;
};

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static bool send_all(int fd, const void* data, size_t len)
{
    const uint8_t* p = (const uint8_t*)data;
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t sent = send(fd, p, remaining, MSG_NOSIGNAL);
        if (sent <= 0) return false;
        p += sent;
        remaining -= (size_t)sent;
    }
    return true;
}

static bool send_message(mk3_t* dev, uint8_t type,
                          const void* payload, uint32_t payload_len)
{
    uint8_t header[5];
    header[0] = type;
    header[1] = (uint8_t)((payload_len >> 24) & 0xFF);
    header[2] = (uint8_t)((payload_len >> 16) & 0xFF);
    header[3] = (uint8_t)((payload_len >>  8) & 0xFF);
    header[4] = (uint8_t)( payload_len        & 0xFF);

    if (!send_all(dev->sock_fd, header, 5)) return false;
    if (payload_len > 0 && payload != NULL) {
        if (!send_all(dev->sock_fd, payload, payload_len)) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                          */
/* ------------------------------------------------------------------ */

mk3_t* mk3_open(void)
{
    /* Allow overriding the port via environment variable */
    const char* port_env = getenv("MK3_EMU_PORT");
    int port = port_env ? atoi(port_env) : EMU_PORT;
    if (port <= 0 || port > 65535) port = EMU_PORT;

    mk3_t* dev = (mk3_t*)calloc(1, sizeof(mk3_t));
    if (!dev) return NULL;

    dev->sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (dev->sock_fd < 0) {
        fprintf(stderr, "[mk3-emu] Failed to create socket\n");
        free(dev);
        return NULL;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, EMU_HOST, &addr.sin_addr);

    if (connect(dev->sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr,
                "[mk3-emu] Cannot connect to emulator at %s:%d "
                "— running in offline mode\n",
                EMU_HOST, port);
        close(dev->sock_fd);
        free(dev);
        return NULL;
    }

    /* Low-latency: disable Nagle's algorithm */
    int flag = 1;
    setsockopt(dev->sock_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    /* Non-blocking reads for mk3_input_poll() */
    int flags = fcntl(dev->sock_fd, F_GETFL, 0);
    fcntl(dev->sock_fd, F_SETFL, flags | O_NONBLOCK);

    fprintf(stderr, "[mk3-emu] Connected to emulator at %s:%d\n",
            EMU_HOST, port);
    return dev;
}

void mk3_close(mk3_t* dev)
{
    if (!dev) return;
    if (dev->sock_fd >= 0) close(dev->sock_fd);
    free(dev);
}

/* ------------------------------------------------------------------ */
/*  Input callback registration                                        */
/* ------------------------------------------------------------------ */

void mk3_input_set_pad_callback(mk3_t* dev,
                                 mk3_pad_callback_t callback, void* userdata)
{
    if (!dev) return;
    dev->pad_callback = callback;
    dev->pad_callback_userdata = userdata;
}

void mk3_input_set_button_callback(mk3_t* dev,
                                    mk3_button_callback_t callback, void* userdata)
{
    if (!dev) return;
    dev->button_callback = callback;
    dev->button_callback_userdata = userdata;
}

void mk3_input_set_knob_callback(mk3_t* dev,
                                  mk3_knob_callback_t callback, void* userdata)
{
    if (!dev) return;
    dev->knob_callback = callback;
    dev->knob_callback_userdata = userdata;
}

void mk3_input_set_stepper_callback(mk3_t* dev,
                                     mk3_stepper_callback_t callback, void* userdata)
{
    if (!dev) return;
    dev->stepper_callback = callback;
    dev->stepper_callback_userdata = userdata;
}

/* ------------------------------------------------------------------ */
/*  Input message processing                                           */
/* ------------------------------------------------------------------ */

static void process_message(mk3_t* dev, uint8_t type,
                             const uint8_t* payload, uint32_t len)
{
    switch (type) {

    case MSG_BUTTON: {
        if (len < 2) return;
        uint8_t name_len = payload[0];
        if (len < (uint32_t)(1 + name_len + 1)) return;

        char name[256]; /* uint8_t name_len (max 255) always fits + NUL */
        memcpy(name, payload + 1, name_len);
        name[name_len] = '\0';

        bool pressed = payload[1 + name_len] != 0;

        if (dev->button_callback)
            dev->button_callback(name, pressed, dev->button_callback_userdata);
        break;
    }

    case MSG_PAD: {
        if (len < 3) return;
        uint8_t pad_index = payload[0];
        uint16_t pressure = ((uint16_t)payload[1] << 8) | payload[2];
        bool is_pressed = pressure > 256;

        if (dev->pad_callback)
            dev->pad_callback(pad_index, is_pressed, pressure,
                              dev->pad_callback_userdata);
        break;
    }

    case MSG_KNOB: {
        if (len < 2) return;
        uint8_t name_len = payload[0];
        if (len < (uint32_t)(1 + name_len + 4)) return;

        char name[256]; /* uint8_t name_len (max 255) always fits + NUL */
        memcpy(name, payload + 1, name_len);
        name[name_len] = '\0';

        int16_t delta = (int16_t)(
            ((uint16_t)payload[1 + name_len] << 8) |
             (uint16_t)payload[2 + name_len]);
        uint16_t absolute = (uint16_t)(
            ((uint16_t)payload[3 + name_len] << 8) |
             (uint16_t)payload[4 + name_len]);

        if (dev->knob_callback)
            dev->knob_callback(name, delta, absolute,
                               dev->knob_callback_userdata);
        break;
    }

    case MSG_STEPPER: {
        if (len < 2) return;
        int8_t direction = (int8_t)payload[0];
        uint8_t position = payload[1];

        if (dev->stepper_callback)
            dev->stepper_callback(direction, position,
                                  dev->stepper_callback_userdata);
        break;
    }

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  Input polling                                                      */
/* ------------------------------------------------------------------ */

int mk3_input_poll(mk3_t* dev)
{
    if (!dev || dev->sock_fd < 0) return -1;

    int events = 0;

    /* Drain all available data from socket (non-blocking) */
    while (dev->read_buf_len < (int)sizeof(dev->read_buf)) {
        ssize_t n = recv(dev->sock_fd,
                         dev->read_buf + dev->read_buf_len,
                         (size_t)(sizeof(dev->read_buf) - dev->read_buf_len),
                         0);
        if (n <= 0) break; /* EAGAIN (no data) or error */
        dev->read_buf_len += (int)n;
    }

    /* Process complete messages: [type:1][length:4][payload:length] */
    while (dev->read_buf_len >= 5) {
        uint8_t type = dev->read_buf[0];
        uint32_t payload_len =
            ((uint32_t)dev->read_buf[1] << 24) |
            ((uint32_t)dev->read_buf[2] << 16) |
            ((uint32_t)dev->read_buf[3] <<  8) |
             (uint32_t)dev->read_buf[4];

        uint32_t msg_total = 5 + payload_len;

        /* Guard against absurdly large messages */
        if (payload_len > READ_BUF_SIZE) {
            /* Protocol error — discard buffer */
            dev->read_buf_len = 0;
            break;
        }

        if ((uint32_t)dev->read_buf_len < msg_total)
            break; /* incomplete message, wait for more data */

        process_message(dev, type, dev->read_buf + 5, payload_len);
        events++;

        /* Shift remaining data forward */
        int remaining = dev->read_buf_len - (int)msg_total;
        if (remaining > 0)
            memmove(dev->read_buf, dev->read_buf + msg_total, (size_t)remaining);
        dev->read_buf_len = remaining;
    }

    return events;
}

/* ------------------------------------------------------------------ */
/*  Display output                                                     */
/* ------------------------------------------------------------------ */

int mk3_display_draw(mk3_t* dev, int screen_index, const uint16_t* rgb565)
{
    if (!dev || dev->sock_fd < 0 || !rgb565) return -1;

    /* Payload: [screen:1][pixel_data: W*H*2] */
    uint32_t pixel_bytes = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2;
    uint32_t payload_len = 1 + pixel_bytes;

    /* Send header + screen index as one chunk, then pixel data */
    uint8_t header[6];
    header[0] = MSG_DISPLAY;
    header[1] = (uint8_t)((payload_len >> 24) & 0xFF);
    header[2] = (uint8_t)((payload_len >> 16) & 0xFF);
    header[3] = (uint8_t)((payload_len >>  8) & 0xFF);
    header[4] = (uint8_t)( payload_len        & 0xFF);
    header[5] = (uint8_t)screen_index;

    if (!send_all(dev->sock_fd, header, 6)) return -1;
    if (!send_all(dev->sock_fd, rgb565, pixel_bytes)) return -1;

    return 0;
}

int mk3_display_draw_partial(mk3_t* dev, int screen_index,
                              int x, int y, int w, int h,
                              const uint16_t* pixels)
{
    /* Partial rendering is not supported in the emulator — no-op.
     * (The app disables partial rendering anyway via
     *  mk3_display_disable_partial_rendering(dev, true)) */
    (void)dev; (void)screen_index;
    (void)x; (void)y; (void)w; (void)h; (void)pixels;
    return 0;
}

int mk3_display_clear(mk3_t* dev, int screen_index, uint16_t color)
{
    if (!dev || dev->sock_fd < 0) return -1;

    /* Allocate a solid-color frame and send it */
    size_t pixel_count = DISPLAY_WIDTH * DISPLAY_HEIGHT;
    uint16_t* buf = (uint16_t*)malloc(pixel_count * 2);
    if (!buf) return -1;

    for (size_t i = 0; i < pixel_count; i++)
        buf[i] = color;

    int result = mk3_display_draw(dev, screen_index, buf);
    free(buf);
    return result;
}

void mk3_display_disable_partial_rendering(mk3_t* dev, bool disable)
{
    /* No-op — emulator always sends full frames */
    (void)dev;
    (void)disable;
}

/* ------------------------------------------------------------------ */
/*  LED output                                                         */
/* ------------------------------------------------------------------ */

int mk3_led_set_brightness(mk3_t* dev, const char* led_name, uint8_t brightness)
{
    if (!dev || dev->sock_fd < 0 || !led_name) return -1;

    uint8_t name_len = (uint8_t)strlen(led_name);
    uint8_t payload[258]; /* 1 + 255 + 1 max */
    payload[0] = name_len;
    memcpy(payload + 1, led_name, name_len);
    payload[1 + name_len] = brightness;

    return send_message(dev, MSG_LED_MONO, payload,
                         (uint32_t)(1 + name_len + 1)) ? 0 : -1;
}

int mk3_led_set_indexed_color(mk3_t* dev, const char* led_name, uint8_t color_index)
{
    if (!dev || dev->sock_fd < 0 || !led_name) return -1;

    uint8_t name_len = (uint8_t)strlen(led_name);
    uint8_t payload[258];
    payload[0] = name_len;
    memcpy(payload + 1, led_name, name_len);
    payload[1 + name_len] = color_index;

    return send_message(dev, MSG_LED_INDEXED, payload,
                         (uint32_t)(1 + name_len + 1)) ? 0 : -1;
}
