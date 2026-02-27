./main | ffmpeg -pixel_format bayer_rggb8 -framerate 20 -f rawvideo -video_size 640x480 -i - -f v4l2 /dev/video2
