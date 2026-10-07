#pragma once

#include <glib.h>

extern guint64 frame_index;
extern guint64 buffer_errors;
extern guint16 framerate;

extern gboolean recording;
extern gboolean should_record;
extern GDateTime* record_start_time;
extern guint64 record_start_frame_index;
extern gchar* record_folder;