#include <arv.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <stdbool.h>

#define NUM_BUFFERS 100
#define DISPLAY_FRAMERATE 10
#define CAPTURE_FRAMERATE 815

double get_time_diff_us(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1e6 + (end.tv_nsec - start.tv_nsec) / 1e3;
}
struct timespec t1 = {0}, t2 = {0};

static volatile int keepRunning = 1;

void intHandler(int dummy)
{
    keepRunning = 0;
}

int main(int argc, char **argv)
{
    bool save_frames = false;
    bool pipe_stdout = false;

    // Parse CLI arguments
    for (int j = 1; j < argc; j++) {
        if (strcmp(argv[j], "--save") == 0) {
            save_frames = true;
        } else if (strcmp(argv[j], "--stdout") == 0) {
            pipe_stdout = true;
        } else {
            fprintf(stderr, "Usage: %s [--save] [--stdout]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    signal(SIGINT, intHandler);
    signal(SIGPIPE, intHandler);
    
    // Give some time for the system/camera to settle if needed
    g_usleep(5e6);
    
    ArvCamera *camera;
    GError *error = NULL;

    camera = arv_camera_new(NULL, &error);
    if (!ARV_IS_CAMERA(camera)) {
        fprintf(stderr, "Error: could not find or initialize camera%s%s\n", 
                error ? ": " : "", error ? error->message : "");
        if (error) g_error_free(error);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "Connected to %s\n", arv_camera_get_model_name(camera, NULL));

    arv_camera_set_acquisition_mode(camera, ARV_ACQUISITION_MODE_CONTINUOUS, &error);
    if (error != NULL) {
        fprintf(stderr, "Error: could not set acquisition mode: %s\n", error->message);
        goto cleanup_camera;
    }

    /* Create the stream object without callback */
    ArvStream *stream = arv_camera_create_stream(camera, NULL, NULL, &error);
    if (!ARV_IS_STREAM(stream)) {
        fprintf(stderr, "Error: could not create stream: %s\n", error ? error->message : "unknown");
        goto cleanup_camera;
    }

    /* Retrieve the payload size for buffer creation */
    size_t payload = arv_camera_get_payload(camera, &error);
    if (error != NULL) {
        fprintf(stderr, "Error: could not get payload size: %s\n", error->message);
        goto cleanup_stream;
    }

    /* Insert buffers in the stream buffer pool */
    for (int i = 0; i < NUM_BUFFERS; i++) {
        ArvBuffer *buf = arv_buffer_new(payload, NULL);
        arv_stream_push_buffer(stream, buf);
    }

    arv_camera_start_acquisition(camera, &error);
    if (error != NULL) {
        fprintf(stderr, "Error: could not start acquisition: %s\n", error->message);
        goto cleanup_stream;
    }

    fprintf(stderr, "Start acquisition OK\n");

    if (save_frames) {
        // Ensure the output directory exists
        struct stat st = {0};
        if (stat("images", &st) == -1) {
            mkdir("images", 0700);
        }
    }

    guint64 last_display_ts = 0;
    guint64 last_fps_ts = 0;
    guint64 i = 0;

    while (keepRunning) {
        ArvBuffer *buffer = arv_stream_pop_buffer(stream);
        if (!ARV_IS_BUFFER(buffer)) {
            continue;
        }

        if (arv_buffer_get_status(buffer) != ARV_BUFFER_STATUS_SUCCESS) {
            fprintf(stderr, "Error: incomplete or corrupted buffer (frame %lu)\n", arv_buffer_get_frame_id(buffer));
            arv_stream_push_buffer(stream, buffer);
            continue;
        }

        guint64 ts = arv_buffer_get_timestamp(buffer);
        if (i % CAPTURE_FRAMERATE == 0) {
            if (last_fps_ts != 0) {
                double delta_s = (ts - last_fps_ts) / 1e9;
                double real_fps = CAPTURE_FRAMERATE / delta_s;
                if (save_frames) {
                    fprintf(stderr, "i:%lu fps:%.1f save_time:%.2fus\n", i, real_fps, get_time_diff_us(t1, t2));
                } else {
                    fprintf(stderr, "i:%lu fps:%.1f\n", i, real_fps);
                }
            }
            last_fps_ts = ts;
        }

        size_t image_size;
        const gchar *image = arv_buffer_get_image_data(buffer, &image_size);

        if (pipe_stdout) {
            if (ts - last_display_ts >= (1e9 / DISPLAY_FRAMERATE)) {
                if (fwrite(image, 1, image_size, stdout) != image_size) {
                    fprintf(stderr, "Error while writing to stdout\n");
                    keepRunning = 0;
                }
                fflush(stdout);
                last_display_ts = ts;
            }
        }

        if (save_frames) {
            clock_gettime(CLOCK_MONOTONIC, &t1);
            char image_filename[64];
            snprintf(image_filename, sizeof(image_filename), "images/%lu.raw", i);

            FILE *f = fopen(image_filename, "wb");
            if (f) {
                fwrite(image, 1, image_size, f);
                fclose(f);
            } else {
                fprintf(stderr, "Error: could not open %s\n", image_filename);
            }
            clock_gettime(CLOCK_MONOTONIC, &t2);
        }

        arv_stream_push_buffer(stream, buffer);
        i++;
    }

    arv_camera_stop_acquisition(camera, &error);
    if (error != NULL) {
        fprintf(stderr, "Error stopping acquisition: %s\n", error->message);
        g_error_free(error);
        error = NULL;
    }

cleanup_stream:
    g_clear_object(&stream);
cleanup_camera:
    g_clear_object(&camera);
    if (error != NULL) {
        g_error_free(error);
    }

    fprintf(stderr, "exit\n");
    return EXIT_SUCCESS;
}
