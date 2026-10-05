./builddir/hscap --stdout | ffmpeg \
    -framerate 10 \
    -video_size 512x512 \
    -pixel_format bayer_bggr8 \
    -f rawvideo \
    -i - \
    -c:v libx264 \
    -g 20 \
    -pix_fmt yuv420p \
    -f mpegts \
    udp://127.0.0.1:1234