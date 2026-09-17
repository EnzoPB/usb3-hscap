project('hscam-cap', 'c', 'cpp', version: '0.0.1')

cc = meson.get_compiler('c')


aravis_dep = dependency('aravis-0.8')
turbojpeg_dep = dependency('libturbojpeg')
simd_dep = cc.find_library('Simd')
math_dep = cc.find_library('m', required: true)

exe = executable ('hscap', 'main.c', dependencies: [math_dep, aravis_dep, turbojpeg_dep, simd_dep], link_language: 'cpp')
