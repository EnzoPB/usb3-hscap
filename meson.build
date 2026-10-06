project('hscam-cap', 'c', 'cpp', version: '0.0.1')

cc = meson.get_compiler('c')


aravis_dep = dependency('aravis-0.8')
turbojpeg_dep = dependency('libturbojpeg')
simd_dep = cc.find_library('Simd')
microhttpd_dep = cc.find_library('microhttpd')
math_dep = cc.find_library('m', required: true)

sources = files(
    'main.c',
    'write.c',
    'compressor.c',
    'web_api.c'
)

exe = executable ('hscap', sources, dependencies: [math_dep, aravis_dep, turbojpeg_dep, simd_dep, microhttpd_dep], link_language: 'cpp')
