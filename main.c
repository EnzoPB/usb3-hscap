

#include <arv.h>

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#define NUM_BUFFERS 10
#define NUM_FRAMES 100000
// #define SAVE_FRAMES
#define DISPLAY_FRAMERATE 20
#define PIPE_STDOUT

float sum_ts = 0.0;

int main(int argc, char **argv)
{
    ArvCamera *camera;
    GError *error = NULL;

    camera = arv_camera_new(NULL, &error);

    if (ARV_IS_CAMERA(camera))
    {
        ArvStream *stream = NULL;

        fprintf(stderr, "%s\n", arv_camera_get_model_name(camera, NULL));

        arv_camera_set_acquisition_mode(camera, ARV_ACQUISITION_MODE_CONTINUOUS, &error);

        if (error == NULL)
            /* Create the stream object without callback */
            stream = arv_camera_create_stream(camera, NULL, NULL, &error);

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
                /* Start the acquisition */
                arv_camera_start_acquisition(camera, &error);

            if (error == NULL)
            {
                guint64 last_ts = 0;
                gchar *image_filename;
                size_t image_size;
                const gchar *image;
                GError *image_write_error = NULL;
                guint64 last_display_ts = 0;
                for (i = 0; i < NUM_FRAMES; i++)
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
                        if (i > 0)
                        {
                            sum_ts += (1.0 / (ts - last_ts) * 1e9);
                            if (i % (NUM_FRAMES / 50) == 0)
                                fprintf(stderr, "%d %ld\n", i, (guint64)(1.0 / (ts - last_ts) * 1e9));
                        }
                        last_ts = ts;

                        image_filename = g_strdup_printf("images/%d.raw", i);
                        image = arv_buffer_get_image_data(buffer, &image_size);
#ifdef PIPE_STDOUT
                        if (ts - last_display_ts >= (1e9 / DISPLAY_FRAMERATE))
                        {
                            fwrite(image, 1, image_size, stdout);
                            last_display_ts = ts;
                        }
#endif
                        arv_stream_push_buffer(stream, buffer);
#ifdef SAVE_FRAMES
                        g_file_set_contents(image_filename, image, image_size, &image_write_error);
                        if (image_write_error != NULL)
                        {
                            fprintf(stderr, "Error while writing image: %s\n", image_write_error->message);
                            break;
                        }
#endif
                    }
                    else
                    {
                        fprintf(stderr, "Error: NOT A BUFFER\n");
                    }
                }
                g_free(image_filename);
            }

            if (error == NULL)
                /* Stop the acquisition */
                arv_camera_stop_acquisition(camera, &error);

            /* Destroy the stream object */
            g_clear_object(&stream);
        }

        /* Destroy the camera instance */
        g_clear_object(&camera);
    }

    if (error != NULL)
    {
        /* En error happened, display the correspdonding message */
        fprintf(stderr, "Error: %s\n", error->message);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "avg framerate: %f\n", (sum_ts / (NUM_FRAMES - 1)));
    return EXIT_SUCCESS;
}
