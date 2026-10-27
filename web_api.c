#include "web_api.h"
#include "glib.h"
#include "state.h"
#include "write.h"
#include "compressor.h"
#include <microhttpd.h>
#include <stdio.h>

enum MHD_Result ahc_echo(void *cls, struct MHD_Connection *connection,
                    const char *url, const char *method, const char *version,
                    const char *upload_data, unsigned long *upload_data_size,
                    void **ptr) {
  static int aptr;
  int response_code = MHD_HTTP_OK;
  const gchar* index_content = cls;
  gchar* content;

  if (strcmp(method, "GET") != 0)
    return MHD_NO; /* unexpected method */
  if (&aptr != *ptr) {
    /* do never respond on first call */
    *ptr = &aptr;
    return MHD_YES;
  }
  *ptr = NULL; /* reset when done */
  if (strcmp(url, "/") == 0) {
    content = (gchar *)index_content;
  } else if (strcmp(url, "/get_state") == 0) {
    content = g_strdup_printf(
      "frame_index=%lu\n"
      "framerate=%hu\n"
      "buffer_errors=%lu\n"
      "jpeg_queue_free=%d\n"
      "write_queue_free=%d\n"
      "recording=%d\n"
      "recorded_frames=%lu\n"
      "record_folder=%s\n",
      frame_index, framerate, buffer_errors,
      g_async_queue_length(jpeg_queue_free), g_async_queue_length(write_queue_free),
      recording, recording ? frame_index - record_start_frame_index : 0, recording ? record_folder : "");
  } else if (strcmp(url, "/toggle_record") == 0) {
    if (!should_record && g_async_queue_length(write_queue) > 0) {  // trying to start record, but images are still waiting to be written
      content = "record unavailable, images are still being written to disk";
      response_code = MHD_HTTP_BAD_REQUEST;
    } else {  // any other case is ok
      should_record = !should_record;
      content = "ok";
    }
  } else {
    content = "404 not found\n";
    response_code = MHD_HTTP_NOT_FOUND;
  }
  
  struct MHD_Response* response = MHD_create_response_from_buffer(
      g_utf8_strlen(content, -1),
      (void*)content,
      MHD_RESPMEM_PERSISTENT);
  int ret = MHD_queue_response(connection, response_code, response);
  MHD_destroy_response(response);
  return ret;
}

struct MHD_Daemon *d;

int web_api_init() {
  gchar* index_content;
  GError* error = NULL;
  g_file_get_contents("index.html", &index_content, NULL, &error);
  if (error != NULL) {
    fprintf(stderr, "Error: %s\n", error->message);
    return 1;
  }

  d = MHD_start_daemon(
    MHD_USE_SELECT_INTERNALLY | MHD_USE_DEBUG,
    8080, NULL, NULL, &ahc_echo,
    index_content, MHD_OPTION_CONNECTION_TIMEOUT,
    (unsigned int)120, MHD_OPTION_END);

  if (d == NULL)
    return 1;
  return 0;
}

void web_api_destroy() { MHD_stop_daemon(d); }