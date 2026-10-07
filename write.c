#include "write.h"
#include "glib.h"
#include "state.h"

GAsyncQueue *write_queue_free;
GAsyncQueue *write_queue;
GThread *writer_worker;
bool write_should_run = true;
int write_buffer_capacity;


void writer_init(int width, int height) {
  write_queue = g_async_queue_new();
  write_queue_free = g_async_queue_new();

  write_buffer_capacity = tjBufSize(width, height, TJSAMP_444);

  for (int i = 0; i < WRITE_QUEUE_LENGTH; i++) {
    write_input_data *b = g_malloc0(sizeof(write_input_data));
    b->data_buffer = tjAlloc(write_buffer_capacity);
    b->index = 0;
    b->size = 0;
    g_async_queue_push(write_queue_free, b);
  }
  fprintf(stderr, "initialized %d write buffers\n", WRITE_QUEUE_LENGTH);

  writer_worker = g_thread_new("writer", writer_worker_func, NULL);
}

void writer_destroy() {
  fprintf(stderr, "Closing writers...\n");
  write_should_run = false;
  g_thread_join(writer_worker);
}

gpointer writer_worker_func() {
  char image_filename[64];
  while (write_should_run) {
    write_input_data *input_data =
        (write_input_data *)g_async_queue_timeout_pop(write_queue, 100000);
    if (input_data == NULL) {
      continue;
    }

    snprintf(
      image_filename, sizeof(image_filename), "%s/%010lu.jpg",
      record_folder, input_data->index - record_start_frame_index);

    FILE *f = fopen(image_filename, "wb");
    if (f) {
      fwrite(input_data->data_buffer, 1, input_data->size, f);
      fclose(f);
    } else {
      fprintf(stderr, "Error: could not open %s\n", image_filename);
    }

    // done, push it back to the free queue
    g_async_queue_push(write_queue_free, input_data);
  }
}