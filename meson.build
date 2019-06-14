project('gstnlaunch', 'c')

cc = meson.get_compiler('c')

gst_dep = dependency('gstreamer-1.0',
    fallback : ['gstreamer', 'gst_dep'])

executable('gst-n-launch-1.0', 'src/gstnlaunch.c', dependencies : [gst_dep])