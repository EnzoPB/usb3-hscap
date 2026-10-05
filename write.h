#pragma once

#include <glib.h>
#include <arv.h>
#include <stdbool.h>
#include <turbojpeg.h>
#include <Simd/SimdLib.h>
#include <stdio.h>

#define WRITE_QUEUE_LENGTH 1000

extern GAsyncQueue* write_queue_free;
extern GAsyncQueue* write_queue;
extern int write_buffer_capacity;

// input data struct (jpeg data coming from the compressor)
typedef struct {
  long index;
  unsigned char* data_buffer;
  unsigned long size;
} write_input_data;

void writer_init(int width, int height);
void writer_destroy();
gpointer writer_worker_func();