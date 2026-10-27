#pragma once

#include <glib.h>
#include <arv.h>
#include <stdbool.h>
#include <stdint.h>
#include <turbojpeg.h>
#include <Simd/SimdLib.h>
#include "write.h"

#define NUM_JPEG_WORKERS 5
#define JPEG_QUEUE_LENGTH 1000

extern GAsyncQueue* jpeg_queue_free;  // queue of free buffers
extern GAsyncQueue* jpeg_queue;  // queue of buffers waiting to be compressed

// input data struct (raw data coming from the camera)
typedef struct {
  long index;
  ArvBuffer* buffer;
  ArvStream* stream;
  const char* data_buffer;
} jpeg_input_data;

void compressor_init(int width, int height);
void compressor_destroy();
gpointer compressor_worker_func();