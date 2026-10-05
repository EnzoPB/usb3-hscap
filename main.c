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
#include <glib.h>

#include "write.h"
#include "compressor.h"

#define NUM_BUFFERS 1000
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
    g_usleep(3e6);
    
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

    arv_camera_set_frame_rate(camera, CAPTURE_FRAMERATE, &error);
    if (error != NULL) {
        fprintf(stderr, "Error: failed to set framerate: %s\n", error->message);
        g_clear_error(&error);
    }

    arv_camera_set_pixel_format(camera, ARV_PIXEL_FORMAT_BAYER_BG_8, &error);
    if (error != NULL) {
        fprintf(stderr, "Error: failed to set pixel format: %s\n", error->message);
        g_clear_error(&error);
    }

    fprintf(stderr, "Pixel format: %s %d\n", arv_camera_get_pixel_format_as_string(camera, NULL), arv_camera_get_pixel_format(camera, NULL));

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
        ArvBuffer *buf = arv_buffer_new_allocate(payload);
        arv_stream_push_buffer(stream, buf);
    }

    arv_camera_start_acquisition(camera, &error);
    if (error != NULL) {
        fprintf(stderr, "Error: could not start acquisition: %s\n", error->message);
        goto cleanup_stream;
    }


    gint width;
    gint height;
    arv_camera_get_region(camera, NULL, NULL, &width, &height, &error);
    if (error != NULL) {
        fprintf(stderr, "Error: could not get camera image size: %s\n", error->message);
        goto cleanup_stream;
    }

    if (save_frames) {
        writer_init(width, height);
        compressor_init(width, height);
    }

    fprintf(stderr, "Start acquisition OK\n");

    guint64 last_display_ts = 0;
    guint64 last_fps_ts = 0;
    guint64 frame_count = 0;
    guint64 buffer_errors = 0;

    while (keepRunning) {
        ArvBuffer *buffer = arv_stream_pop_buffer(stream);
        if (!ARV_IS_BUFFER(buffer)) {
            continue;
        }

        if (arv_buffer_get_status(buffer) != ARV_BUFFER_STATUS_SUCCESS) {
            buffer_errors++;
            arv_stream_push_buffer(stream, buffer);
            if (buffer_errors % 100 == 0)
                fprintf(stderr, "Error: incomplete or corrupted buffer (x%lu) (frame %lu)\n", buffer_errors, arv_buffer_get_frame_id(buffer));
            continue;
        }

        guint64 ts = arv_buffer_get_timestamp(buffer);

        if (frame_count % (CAPTURE_FRAMERATE/2) == 0) {
            if (last_fps_ts != 0) {
                double delta_s = (ts - last_fps_ts) / 1e9;
                double real_fps = CAPTURE_FRAMERATE / 2.0 / delta_s;
                if (pipe_stdout) {
                    fprintf(stderr, "\033[999;100H");
                }
                if (save_frames) {
                    fprintf(stderr, "i:%lu fps:%.1f compq=%d writeq=%d errors=%lu\r", frame_count, real_fps, g_async_queue_length(jpeg_queue_free), g_async_queue_length(write_queue_free), buffer_errors);//get_time_diff_us(t1, t2));
                } else {
                    fprintf(stderr, "i:%lu fps:%.1f errors=%lu\r", frame_count, real_fps, buffer_errors);
                }
                if (pipe_stdout) {
                    fflush(stderr);
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

            jpeg_input_data *input_data = (jpeg_input_data *)g_async_queue_pop(jpeg_queue_free);
            input_data->data_buffer = image;
            input_data->buffer = buffer;
            input_data->index = frame_count;
            input_data->stream = stream;
            g_async_queue_push(jpeg_queue, input_data);

            
            clock_gettime(CLOCK_MONOTONIC, &t2);
        } else {
            // don't do that is saving frames, compressor will take care of that
            arv_stream_push_buffer(stream, buffer);
        }
        frame_count++;
    }

    arv_camera_stop_acquisition(camera, &error);
    if (error != NULL) {
        fprintf(stderr, "Error stopping acquisition: %s\n", error->message);
        g_clear_error(&error);
    }

cleanup_stream:
    if (save_frames) {
        writer_destroy();
        compressor_destroy();
    }
    g_clear_object(&stream);
cleanup_camera:
    g_clear_object(&camera);
    g_clear_error(&error);

    fprintf(stderr, "exit\n");
    return EXIT_SUCCESS;
}
