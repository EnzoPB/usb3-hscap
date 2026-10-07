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

#include "glibconfig.h"
#include "state.h"
#include "write.h"
#include "compressor.h"
#include "web_api.h"

#define NUM_BUFFERS 1000
#define DISPLAY_FRAMERATE 10
#define CAPTURE_FRAMERATE 815

guint64 frame_index = 0;
guint64 buffer_errors = 0;
guint16 framerate = 0;

gboolean recording = false;
gboolean should_record = false;
GDateTime* record_start_time = NULL;
guint64 record_start_frame_index = 0;
gchar* record_folder;

static volatile int keepRunning = 1;

void intHandler(int dummy)
{
    keepRunning = 0;
}

int main(int argc, char **argv)
{
    bool enable_web_api = false;

    // Parse CLI arguments
    for (int j = 1; j < argc; j++) {
        if (strcmp(argv[j], "--web-api") == 0) {
            enable_web_api = true;
        } else {
            fprintf(stderr, "Usage: %s [--web-api]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    signal(SIGINT, intHandler);
    signal(SIGPIPE, intHandler);
    
    // Give some time for the system/camera to settle if needed
    g_usleep(1e6);

    if (enable_web_api) {
        if (web_api_init() != 0) {
            fprintf(stderr, "Error: could not initialize web api server\n");
            return EXIT_FAILURE;
        } else {
            fprintf(stderr, "Started web api server\n");
        }
    }
    
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

    writer_init(width, height);
    compressor_init(width, height);

    fprintf(stderr, "Start acquisition OK\n");

    guint64 last_display_ts = 0;
    guint64 last_fps_ts = 0;

    ArvBuffer *buffer;

    while (keepRunning) {
        do {
            buffer = arv_stream_try_pop_buffer(stream);
        } while (buffer == NULL || !ARV_IS_BUFFER(buffer));

        if (arv_buffer_get_status(buffer) != ARV_BUFFER_STATUS_SUCCESS) {
            buffer_errors++;
            arv_stream_push_buffer(stream, buffer);
            if (buffer_errors % 100 == 0)
                fprintf(stderr, "Error: incomplete or corrupted buffer (x%lu) (frame %lu)\n", buffer_errors, arv_buffer_get_frame_id(buffer));
            continue;
        }

        guint64 ts = arv_buffer_get_system_timestamp(buffer);

        if (frame_index % (CAPTURE_FRAMERATE/2) == 0) {
            if (last_fps_ts != 0) {
                double delta_s = (ts - last_fps_ts) / 1e9;
                framerate = CAPTURE_FRAMERATE / 2.0 / delta_s;
                fprintf(stderr, "\033[999;100H");
                if (recording) {
                    fprintf(stderr, "i:%lu fps:%hu compq=%d writeq=%d errors=%lu\r",
                    frame_index, framerate,
                    g_async_queue_length(jpeg_queue_free), g_async_queue_length(write_queue_free),
                    buffer_errors);
                } else {
                    fprintf(stderr, "i:%lu fps:%hu errors=%lu\r", frame_index, framerate, buffer_errors);
                }
                fflush(stderr);
            }
            last_fps_ts = ts;
        }

        size_t image_size;
        const gchar *image = arv_buffer_get_image_data(buffer, &image_size);

        if (ts - last_display_ts >= (1e9 / DISPLAY_FRAMERATE)) {
            if (fwrite(image, 1, image_size, stdout) != image_size) {
                fprintf(stderr, "Error while writing to stdout\n");
                keepRunning = 0;
            }
            fflush(stdout);
            last_display_ts = ts;
        }

        if (!recording && should_record) {  // start record
            record_start_time = g_date_time_new_now_local();
            record_start_frame_index = frame_index;
            record_folder = g_date_time_format(record_start_time, "recordings/%d-%m-%Y_%H-%M-%S");
            if (g_mkdir_with_parents(record_folder, 0700) != 0) {
                fprintf(stderr, "Error: could not create recording directory %s\n", record_folder);
                should_record = false;
            } else {
                recording = true;
                fprintf(stderr, "Started recording in folder %s\n", record_folder);
            }
        }

        if (
            recording && !should_record &&
            g_async_queue_length(jpeg_queue) <= 0 && g_async_queue_length(write_queue) <= 0
        ) {  // record stopped, all files are written
            fprintf(stderr, "Stopped recording %s\n", record_folder);
            recording = false;
            record_folder = "";
            record_start_time = NULL;
            record_start_frame_index = 0;
        }

        if (recording && should_record) {
            jpeg_input_data *input_data = (jpeg_input_data *)g_async_queue_pop(jpeg_queue_free);
            input_data->data_buffer = image;
            input_data->buffer = buffer;
            input_data->index = frame_index;
            input_data->stream = stream;
            g_async_queue_push(jpeg_queue, input_data);
        } else {
            // don't do that is saving frames, compressor will take care of that
            arv_stream_push_buffer(stream, buffer);
        }
        frame_index++;
    }

    arv_camera_stop_acquisition(camera, &error);
    if (error != NULL) {
        fprintf(stderr, "Error stopping acquisition: %s\n", error->message);
        g_clear_error(&error);
    }

cleanup_stream:
    writer_destroy();
    compressor_destroy();
    if (enable_web_api) {
        web_api_destroy();
    }
    g_clear_object(&stream);
cleanup_camera:
    g_clear_object(&camera);
    g_clear_error(&error);

    fprintf(stderr, "exit\n");
    return EXIT_SUCCESS;
}
