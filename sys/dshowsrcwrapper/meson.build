project('gstdshow', 'cpp',
        version: '0.10',
        meson_version: '>= 0.47',
        default_options: ['warning_level=1',
                          'buildtype=release'])


conf_data = configuration_data()

conf_data.set('version', meson.project_version())
conf_data.set('pkg_name', meson.project_name())
conf_data.set('pkg_origin', 'fluendo@fluendo.com')

add_project_arguments('-DHAVE_CONFIG_H=1 -DFILTER_DLL', language: 'cpp')


configure_file(input: 'config.h.in',
               output: 'config.h',
               configuration: conf_data)

pkg_suffix = target_machine.cpu_family()

dep_gstreamer = dependency('gstreamer-0.10', version: '>=0.10')
dep_gstreamerbase = dependency('gstreamer-base-0.10', version: '>=0.10')
dep_pluginsbase = dependency('gstreamer-plugins-base-0.10', version: '>=0.10')
dep_basevideo = dependency('gstreamer-basevideo-0.10', version: '>=0.10')
dep_video = dependency('gstreamer-video-0.10', version: '>=0.10')
dep_baseaudio = dependency('gstreamer-audio-0.10', version: '>=0.10')

includedir = include_directories('.', 'baseclasses')


plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-0.10')
cc = meson.get_compiler('cpp')
cpp_dep = cc.find_library('stdc++', required: true)
dep_strmiids = cc.find_library('strmiids', required: true)
dep_ole32 = cc.find_library('ole32', required: true)
dep_uuid = cc.find_library('uuid', required: true)
dep_winmm = cc.find_library('winmm', required: true)
dep_rpcrt4 = cc.find_library('rpcrt4', required: true)
dep_amstrmid = cc.find_library('amstrmid', required: true)
#dep_strmbase = cc.find_library('strmbase', required: true)

libgstfludxcapsrc = library('gstdshow',
                            sources: [
                                'gstdshow.cpp',
                                'gstdshowaudiosrc.cpp',
                                'gstdshowfakesink.cpp',
                                'gstdshowsrcwrapper.cpp',
                                'gstdshowvideosrc.cpp',
                                'baseclasses/amextra.cpp',
                                'baseclasses/amfilter.cpp',
                                'baseclasses/amvideo.cpp',
                                'baseclasses/arithutil.cpp',
                                'baseclasses/combase.cpp',
                                'baseclasses/cprop.cpp',
                                'baseclasses/ctlutil.cpp',
                                'baseclasses/ddmm.cpp',
                                'baseclasses/dllentry.cpp',
                                'baseclasses/dllsetup.cpp',
                                'baseclasses/mtype.cpp',
                                'baseclasses/outputq.cpp',
                                'baseclasses/perflog.cpp',
                                'baseclasses/pstream.cpp',
                                'baseclasses/pullpin.cpp',
                                'baseclasses/refclock.cpp',
                                'baseclasses/renbase.cpp',
                                'baseclasses/schedule.cpp',
                                'baseclasses/seekpt.cpp',
                                'baseclasses/source.cpp',
                                'baseclasses/strmctl.cpp',
                                'baseclasses/sysclock.cpp',
                                'baseclasses/transfrm.cpp',
                                'baseclasses/transip.cpp',
                                'baseclasses/videoctl.cpp',
                                'baseclasses/vtrans.cpp',
                                'baseclasses/winctrl.cpp',
                                'baseclasses/winutil.cpp',
                                'baseclasses/wxdebug.cpp',
                                'baseclasses/wxlist.cpp',
                                'baseclasses/wxutil.cpp'],
                            include_directories: [includedir],
                            install: true, install_dir: plugins_install_dir,
                            dependencies: [
                                dep_gstreamer, dep_gstreamerbase, dep_pluginsbase,
								dep_baseaudio, dep_basevideo, dep_video,
								cpp_dep, dep_strmiids, dep_ole32, dep_uuid, 
								dep_winmm, dep_rpcrt4, dep_amstrmid ]
                            )
