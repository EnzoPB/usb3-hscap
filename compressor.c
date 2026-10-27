#include "compressor.h"
#include "glib.h"
#include "state.h"
#include "stream.h"
#include <turbojpeg.h>

GAsyncQueue* jpeg_queue_free;  // queue of free buffers
GAsyncQueue* jpeg_queue;  // queue of buffers waiting to be compressed
GThread *jpeg_workers[NUM_JPEG_WORKERS];
bool jpeg_should_run = true;
int image_width;
int image_height;

void compressor_init(int width, int height) {
  jpeg_queue_free = g_async_queue_new();
  jpeg_queue = g_async_queue_new();

  image_width = width;
  image_height = height;

  // create buffers
  for (int i = 0; i < JPEG_QUEUE_LENGTH; i++) {
    jpeg_input_data *b = g_malloc0(sizeof(jpeg_input_data));
    g_async_queue_push(jpeg_queue_free, b);
  }
  fprintf(stderr, "initialized %d jpeg buffers\n", WRITE_QUEUE_LENGTH);

  for (int i = 0; i < NUM_JPEG_WORKERS; i++) {
    char worker_name[64];
    sprintf(worker_name, "jpeg worker %d", i);
    fprintf(stderr, "Created worker %s\n", worker_name);
    jpeg_workers[i] = g_thread_new(worker_name, compressor_worker_func, NULL);
  }
}

void compressor_destroy() {
  fprintf(stderr, "Closing compressors...\n");
  jpeg_should_run = false;
  for (int i = 0; i < NUM_JPEG_WORKERS; i++) {
    g_thread_join(jpeg_workers[i]);
  }
}

gpointer compressor_worker_func() {
  tjhandle jpeg_compressor = tjInitCompress();

  if (jpeg_compressor == NULL) {
    fprintf(stderr, "Error: could not initialize JPEG compressor thread %s: %s\n",
      g_thread_get_name(g_thread_self()), tjGetErrorStr());
    return NULL;
  }

  // convert to bgr, jpeg doesn't support bayer pixel format
  // use SIMD to be fast (ka-chow)
  unsigned char *bgr_buf = SimdAllocate(image_width * image_height * 3, 2);
  if (bgr_buf == NULL) {
    fprintf(stderr, "Error: could not allocate SIMD buffer on thread %s\n", g_thread_get_name(g_thread_self()));
    return NULL;
  }

  while (jpeg_should_run) {
    jpeg_input_data *input_data = (jpeg_input_data*)g_async_queue_timeout_pop(jpeg_queue, 100000);
    if (input_data == NULL) {
      continue;
    }

    SimdBayerToBgr((const unsigned char *)input_data->data_buffer, image_width, image_height, image_width,
                   SimdPixelFormatBayerBggr, bgr_buf, image_width * 3);

    // take a write buffer from the free queue
    write_input_data *output_data = (write_input_data *)g_async_queue_pop(write_queue_free);
    output_data->index = input_data->index;
    output_data->size = write_buffer_capacity;  // max jpeg size

    if (tjCompress2(
      jpeg_compressor,
      bgr_buf,
      image_width,
      0,
      image_height,
      TJPF_RGB,
      &output_data->data_buffer,
      &output_data->size,
      TJSAMP_420,
      90,
      TJFLAG_FASTDCT) == 0) {
      if (recording && should_record) {
        // done, send the data to the writer
        g_async_queue_push(write_queue, output_data);
      } else {
        // done, but not recording so send the data back to the free queue
        
        if (g_async_queue_length(stream_queue_free) > 0) {
          stream_input_data* b = g_async_queue_pop(stream_queue_free);
          b->size = output_data->size;
          b->index = output_data->index;
          memcpy(b->buffer, output_data->data_buffer, b->size);
          g_async_queue_push(stream_queue, b);
        }

        g_async_queue_push(write_queue_free, output_data);
      }
    } else {
      fprintf(stderr, "Error: turbojpeg compression failure: %s\n",
              tjGetErrorStr());
      // send the buffer back to the writer free queue
      g_async_queue_push(write_queue_free, output_data);
    }
    // we don't need the aravis & jpeg buffer anymore, push them back
    arv_stream_push_buffer(input_data->stream, input_data->buffer);
    g_async_queue_push(jpeg_queue_free, input_data);
  }
  SimdFree(bgr_buf);
  tjDestroy(jpeg_compressor);
}