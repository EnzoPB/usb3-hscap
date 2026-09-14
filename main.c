#include <arv.h>

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <time.h>

#define NUM_BUFFERS 100
// #define NUM_FRAMES 10000000
#define SAVE_FRAMES
#define DISPLAY_FRAMERATE 10
#define CAPTURE_FRAMERATE 815
#define PIPE_STDOUT

double get_time_diff_us(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1e6 + (end.tv_nsec - start.tv_nsec) / 1e3;
}
struct timespec t1, t2, t3;

static volatile int keepRunning = 1;

void intHandler(int dummy)
{
    keepRunning = 0;
}

float sum_ts = 0.0;

int main(int argc, char **argv)
{
    signal(SIGINT, intHandler);
    signal(SIGPIPE, intHandler);
    g_usleep(5e6);
    ArvCamera *camera;
    GError *error = NULL;

    camera = arv_camera_new(NULL, &error);

    if (ARV_IS_CAMERA(camera))
    {
        ArvStream *stream = NULL;

        fprintf(stderr, "%s\n", arv_camera_get_model_name(camera, NULL));

        arv_camera_set_acquisition_mode(camera, ARV_ACQUISITION_MODE_CONTINUOUS, &error);

        if (error == NULL) {
            /* Create the stream object without callback */
            stream = arv_camera_create_stream(camera, NULL, NULL, &error);
        } else {
            fprintf(stderr, "Error: could not create stream %s", error->message);
            exit(EXIT_FAILURE);
        }

        if (ARV_IS_STREAM(stream))
        {
            int i;
            size_t payload;

            /* Retrieve the payload size for buffer creation */
            payload = arv_camera_get_payload(camera, &error);
            if (error == NULL)
            {
                /* Insert some buffers in the stream buffer pool */
                for (i = 0; i < NUM_BUFFERS; i++)
                {
                    ArvBuffer *buf = arv_buffer_new(payload, NULL);
                    arv_stream_push_buffer(stream, buf);
                }
            }

            if (error == NULL)
                arv_camera_start_acquisition(camera, &error);

            fprintf(stderr, "Start acquisition OK\n");

            if (error == NULL)
            {
                size_t image_size;
                const gchar *image;
                guint64 last_display_ts = 0;
                guint64 last_fps_ts = 0;
                guint64 i = 0;
                while (keepRunning)
                {
                    ArvBuffer *buffer;

                    buffer = arv_stream_pop_buffer(stream);
                    if (ARV_IS_BUFFER(buffer))
                    {
                        /* Don't destroy the buffer, but put it back into the buffer pool */
                        if (arv_buffer_get_status(buffer) != ARV_BUFFER_STATUS_SUCCESS)
                        {
                            fprintf(stderr, "Error: couldn't get buffer\n");
                            break;
                        }
                        guint64 ts = arv_buffer_get_timestamp(buffer);
                        if (i % CAPTURE_FRAMERATE == 0) {
                            if (last_fps_ts != 0) {
                                // get elapsed time
                                double delta_s = (ts - last_fps_ts) / 1e9;
                                double real_fps = CAPTURE_FRAMERATE / delta_s;
                                fprintf(stderr, "i:%lu fps:%.1f t:%.2f \n", i, real_fps, get_time_diff_us(t1, t2));
                            }
                            last_fps_ts = ts;
                        }

                        image = arv_buffer_get_image_data(buffer, &image_size);
#ifdef PIPE_STDOUT
                        if (ts - last_display_ts >= (1e9 / DISPLAY_FRAMERATE))
                        {
                            if (fwrite(image, 1, image_size, stdout) != image_size) {
                                fprintf(stderr, "Error while writing to stdout\n");
                                keepRunning = 0;
                            }
                            fflush(stdout);
                            //fprintf(stderr, "img\n");
                            last_display_ts = ts;
                        }
#ifdef SAVE_FRAMES
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
#endif
#endif
                        arv_stream_push_buffer(stream, buffer);
                    }
                    else
                    {
                        fprintf(stderr, "Error: NOT A BUFFER\n");
                    }
                    i++;
                }  // while
            } else {
                fprintf(stderr, "Error: could not start acquisition: %s", error->message);
            }

            if (error == NULL)
                arv_camera_stop_acquisition(camera, &error);

            g_clear_object(&stream);
        }

        g_clear_object(&camera);
    }

    if (error != NULL)
    {
        fprintf(stderr, "Error: %s\n", error->message);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "exit\n");
    return EXIT_SUCCESS;
}
