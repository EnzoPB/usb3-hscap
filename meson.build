project('hscam-cap', 'c', 'cpp', version: '0.0.1')

cc = meson.get_compiler('c')

aravis_dep = dependency('aravis-0.8')
turbojpeg_dep = dependency('libturbojpeg')
simd_dep = cc.find_library('Simd')
microhttpd_dep = cc.find_library('microhttpd')
avformat_dep = cc.find_library('avformat')
avcodec_dep = cc.find_library('avcodec')
avutil_dep = cc.find_library('avutil')
math_dep = cc.find_library('m', required: true)

deps = [
    math_dep,
    aravis_dep,
    turbojpeg_dep,
    simd_dep,
    microhttpd_dep,
    avformat_dep,
    avcodec_dep,
    avutil_dep
]

sources = files(
    'main.c',
    'write.c',
    'compressor.c',
    'web_api.c',
    'stream.c'
)

exe = executable ('hscap', sources, dependencies: deps, link_language: 'cpp')
