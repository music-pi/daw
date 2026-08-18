// File: src/tools/audio_cli/audio_cli.c

#include "audio/context.h"
#include "audio/graph.h"
#include "audio/nodes/sine_node.h"
#include "audio/nodes/sample_player_node.h"
#include "audio/nodes/dsp_node_adapter.h"  // audio_node_create_from_plugin(), audio_node_set_parameter()
#include "audio/audio_backend.h"
#include "audio/backends/backend_pipewire.h"

#include <sndfile.h>
#include <dirent.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_SAMPLES 10

typedef struct {
    audio_backend_t* backend;
    volatile bool   running;
} runner_t;

static SNDFILE*       g_sndfile = NULL;
static audio_node_t*  g_sample_nodes[MAX_SAMPLES] = { NULL };
static int            g_num_loaded_samples = 0;
static volatile bool  g_keep_main_loop_running = true;
static struct termios old_tio, new_tio;

// Non-blocking terminal setup (sampler mode)
static void setup_terminal_nonblocking(void) {
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= (~ICANON & ~ECHO);
    new_tio.c_cc[VMIN]  = 0;
    new_tio.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

static void reset_terminal(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
}

// Hook to write post-DSP output to WAV
static void (*original_process)(audio_node_t*, float**, int) = NULL;
static void process_with_wav(audio_node_t* node, float** bufs, int frames) {
    if (original_process) {
        original_process(node, bufs, frames);
    }
    if (g_sndfile && bufs && bufs[0]) {
        sf_writef_float(g_sndfile, bufs[0], frames);
    }
}

// Ctrl+C handler
static void sigint_handler(int sig) {
    (void)sig;
    g_keep_main_loop_running = false;
}

// Audio backend runner thread
static void* backend_runner(void* arg) {
    runner_t* r = (runner_t*)arg;
    audio_backend_run(r->backend);
    r->running = false;
    return NULL;
}

int main(int argc, char* argv[]) {
    float       freq            = 440.0f;
    int         duration        = 5;
    const char* wav_output_path = NULL;
    const char* sampler_folder  = NULL;
    const char* dsp_plugin_path = NULL;

    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--sine") == 0 && i+1 < argc) {
            freq = atof(argv[++i]);
        } else if (strcmp(argv[i], "--time") == 0 && i+1 < argc) {
            duration = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--out") == 0 && i+1 < argc) {
            wav_output_path = argv[++i];
        } else if (strcmp(argv[i], "--sampler") == 0 && i+1 < argc) {
            sampler_folder = argv[++i];
        } else if (strcmp(argv[i], "--dsp") == 0 && i+1 < argc) {
            dsp_plugin_path = argv[++i];
        }
    }

    // Print mode info
    if (sampler_folder) {
        printf("🎧 Sampler Mode: %s\n", sampler_folder);
    } else {
        printf("🔊 Sine Mode: %.1f Hz for %d s\n", freq, duration);
        if (wav_output_path)  printf("   Writing WAV: %s\n", wav_output_path);
        if (dsp_plugin_path)  printf("🔌 DSP Plugin: %s\n", dsp_plugin_path);
    }

    // Create audio context (which includes the graph)
    int sample_rate = 48000, block_size = 512;
    audio_context_t* ctx = audio_context_create(sample_rate, block_size);
    if (!ctx) {
        fprintf(stderr, "❌ Failed to create audio context\n");
        return 1;
    }
    audio_graph_t* graph = ctx->graph; // Use the graph from the audio context

    // --- Sampler Mode ---
    if (sampler_folder) {
        DIR* d = opendir(sampler_folder);
        if (!d) {
            fprintf(stderr, "❌ Could not open sampler folder: %s\n", sampler_folder);
            audio_context_destroy(ctx);
            // graph is part of ctx, so no separate destroy needed here
            return 1;
        }
        struct dirent* ent;
        while ((ent = readdir(d)) && g_num_loaded_samples < MAX_SAMPLES) {
            if (ent->d_type == DT_REG) {
                const char* ext = strrchr(ent->d_name, '.');
                if (ext && strcasecmp(ext, ".wav") == 0) {
                    char path[1024];
                    snprintf(path, sizeof(path), "%s/%s", sampler_folder, ent->d_name);
                    char name[64];
                    snprintf(name, sizeof(name), "sample_%d", g_num_loaded_samples);
                    printf("   Loading sample %d: %s\n",
                           g_num_loaded_samples, ent->d_name);
                    audio_node_t* sn = sample_player_node_create(
                        name, path, (float)sample_rate);
                    if (sn) {
                        sn->volume = 0.7f;
                        audio_graph_add_node(graph, sn);
                        // connect sampler to output
                        audio_graph_set_output(graph, sn);
                        g_sample_nodes[g_num_loaded_samples++] = sn;
                    }
                }
            }
        }
        closedir(d);
        if (g_num_loaded_samples == 0) {
            fprintf(stderr, "⚠️ No .wav files found in %s\n", sampler_folder);
            audio_context_destroy(ctx);
            return 1;
        }
    }
    // --- Sine + optional DSP Mode ---
    else {
        // 1) Create & add sine node
        audio_node_t* sine = sine_node_create("sine", freq, (float)sample_rate);
        audio_graph_add_node(graph, sine);

        // 2) If DSP plugin given, load & add it, then chain and set output
        audio_node_t* dsp = NULL;
        if (dsp_plugin_path) {
            dsp = audio_node_create_from_plugin(dsp_plugin_path, sample_rate);
            if (!dsp) {
                fprintf(stderr, "❌ Failed to load DSP plugin: %s\n",
                        dsp_plugin_path);
                audio_context_destroy(ctx);
                return 1;
            }
            audio_graph_add_node(graph, dsp);
            audio_graph_connect(graph, sine, 0, dsp, 0);
            audio_graph_set_output(graph, dsp);

            // Initialize a DSP parameter, e.g., cutoff for a filter, or gain.
            audio_node_set_parameter(dsp, "cutoff", 200.0f); // Initial low cutoff for filter testing
            if (wav_output_path) {
                original_process     = dsp->process;
                dsp->process         = process_with_wav;
            }
        } else {
            // No DSP: connect sine directly to output (and WAV if requested)
            audio_graph_set_output(graph, sine);
            if (wav_output_path) {
                original_process = sine->process;
                sine->process    = process_with_wav;
            }
        }

        // 3) Open WAV file if requested
        if (wav_output_path) {
            SF_INFO sfinfo = {
                .samplerate = sample_rate,
                .channels   = 1,
                .format     = SF_FORMAT_WAV | SF_FORMAT_FLOAT,
            };
            g_sndfile = sf_open(wav_output_path, SFM_WRITE, &sfinfo);
            if (!g_sndfile) {
                fprintf(stderr, "❌ Could not open WAV file: %s\n",
                        wav_output_path);
                audio_context_destroy(ctx);
                return 1;
            }
        }

        // 4) Start backend
        audio_backend_t* backend = audio_backend_pipewire_create(ctx);
        if (!backend) {
            fprintf(stderr, "❌ Failed to create backend\n");
            audio_context_destroy(ctx);
            return 1;
        }
        runner_t r = { .backend = backend, .running = true };
        pthread_t thread;
        pthread_create(&thread, NULL, backend_runner, &r);
        signal(SIGINT, sigint_handler);

        // 5) Ramp a DSP parameter (e.g., cutoff) if DSP present, otherwise just wait
        if (dsp) { // DSP is present, perform parameter ramp
            int updates_per_second = 1000; // Target 1ms updates for smoother ramping
            int usleep_interval_us = 1000000 / updates_per_second;
            int total_ramp_update_steps = duration * updates_per_second;
            if (duration > 0 && total_ramp_update_steps == 0) total_ramp_update_steps = 1; // Ensure at least one step if duration > 0

            // Define ramp parameters for "cutoff"
            const char* param_to_ramp = "cutoff";
            float param_min = 20.0f;  // Min cutoff in Hz
            float param_max = 2000.0f; // Max cutoff in Hz

            float q_min = 0.717f;
            float q_max = 25.0f;

            // Ensure parameter starts at its min value (it was set to param_min before this block)
            // audio_node_set_parameter(dsp, param_to_ramp, param_min);

            if (total_ramp_update_steps > 0) {
                int ramp_up_segment_steps = total_ramp_update_steps / 2;
                int ramp_down_segment_steps = total_ramp_update_steps - ramp_up_segment_steps;

                printf("🚀 Ramping DSP parameter '%s' UP from %.1f to %.1f...\n", param_to_ramp, param_min, param_max);
                for (int i = 0; i < ramp_up_segment_steps && g_keep_main_loop_running && r.running; ++i) {
                    float progress = (ramp_up_segment_steps > 1) ? ((float)i / (ramp_up_segment_steps - 1)) : (ramp_up_segment_steps == 1 ? 1.0f : 0.0f);
                    float current_value = param_min + (param_max - param_min) * progress;
                    float current_q = q_min + (q_max - q_min) * progress;
                    audio_node_set_parameter(dsp, "Q", current_q);
                    audio_node_set_parameter(dsp, param_to_ramp, current_value);
                    usleep(usleep_interval_us);
                }

                // Ensure parameter reaches peak if ramp-up happened and was not interrupted
                if (g_keep_main_loop_running && r.running && ramp_up_segment_steps > 0) {
                    audio_node_set_parameter(dsp, param_to_ramp, param_max);
                }

                printf("📉 Ramping DSP parameter '%s' DOWN from %.1f to %.1f...\n", param_to_ramp, param_max, param_min);
                for (int i = 0; i < ramp_down_segment_steps && g_keep_main_loop_running && r.running; ++i) {
                    float progress = (ramp_down_segment_steps > 1) ? ((float)i / (ramp_down_segment_steps - 1)) : (ramp_down_segment_steps == 1 ? 1.0f : 0.0f);
                    float current_value = param_max + (param_min - param_max) * progress;
                    audio_node_set_parameter(dsp, param_to_ramp, current_value);
                    usleep(usleep_interval_us);
                }

                // Ensure parameter settles at its min value if not interrupted
                if (g_keep_main_loop_running && r.running) {
                    audio_node_set_parameter(dsp, param_to_ramp, param_min);
                }
            } else { // duration is 0 or too short for any ramp steps
                // Parameter is already at its initial value (e.g. param_min)
            }
            printf("⏹️ DSP parameter ramp for '%s' complete.\n", param_to_ramp);

        } else { // No DSP: just wait out the duration using the original timing
            for (int i = 0; i < duration * 100 && g_keep_main_loop_running && r.running; ++i) {
                usleep(10000);
            }
            printf("⏹️ Time's up\n");
        }

        // 6) Teardown
        audio_backend_request_stop(backend);
        pthread_join(thread, NULL);
        audio_backend_destroy(backend);
        audio_context_destroy(ctx); // This will destroy graph (ctx->graph) as well

        if (g_sndfile) {
            sf_close(g_sndfile);
            printf("💾 Wrote WAV: %s\n", wav_output_path);
        }
        return 0;
    }

    // Shouldn't reach here
    audio_context_destroy(ctx);
    audio_graph_destroy(graph);
    return 0;
}
