project('image-process', 'cpp',
  version: '0.1.0',
  meson_version: '>=1.4.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=2',
    'default_library=static',
  ],
)

fs = import('fs')

nlohmann_json_sp = subproject('nlohmann_json')
nlohmann_json_dep = nlohmann_json_sp.get_variable('nlohmann_json_dep')
sdk_sp = subproject('satellite-plugin-sdk')
satellite_common_dep = sdk_sp.get_variable('satellite_common_dep')

gstreamer_dep = dependency('gstreamer-1.0', version: '>=1.20')
gstreamer_base_dep = dependency('gstreamer-base-1.0', version: '>=1.20')
gstreamer_app_dep = dependency('gstreamer-app-1.0', version: '>=1.20')
gstreamer_video_dep = dependency('gstreamer-video-1.0', version: '>=1.20')
glib_dep = dependency('glib-2.0')

gst_meta_include = include_directories('src/gst/meta')
runtime_install_dir = get_option('libdir') / 'satellite/image-process/gstreamer-1.0'
runtime_cpp_args = [
  '-DPACKAGE="image-process"',
  '-DIP_RUNTIME_VERSION="@0@"'.format(meson.project_version()),
]

gst_meta_lib = shared_library(
  'imageprocess-gstmeta',
  'src/gst/meta/gst_meta_v1.cpp',
  include_directories: gst_meta_include,
  dependencies: [gstreamer_dep, glib_dep],
  version: '1.0.0',
  soversion: '1',
  install: true,
  install_dir: runtime_install_dir,
)

gst_meta_dep = declare_dependency(
  link_with: gst_meta_lib,
  include_directories: gst_meta_include,
  dependencies: [gstreamer_dep, glib_dep],
)

internal_runtime_option = get_option('internal_runtime')
development_filters_option = get_option('development_filters')
opencv_dep = dependency(
  'opencv4',
  required: get_option('development_sources'),
)

runtime_manifest_data = configuration_data()
runtime_manifest_data.set('COMPONENT_VERSION', meson.project_version())
component_revision = get_option('component_revision')
git_program = find_program('git', required: false)
if component_revision == '' and git_program.found()
  git_revision = run_command(
    git_program,
    '-C', meson.current_source_dir(),
    'describe', '--always', '--dirty=+',
    check: false,
  )
  if git_revision.returncode() == 0
    component_revision = git_revision.stdout().strip()
  endif
endif
if component_revision == ''
  component_revision = 'unknown'
endif
runtime_manifest_data.set('IMAGE_PROCESS_REVISION', component_revision)
runtime_manifest_data.set(
  'SOURCE_PROVENANCE_SHA256',
  fs.hash('SOURCE_PROVENANCE.json', 'sha256'),
)
runtime_manifest_data.set(
  'SOURCE_FACTORIES',
  opencv_dep.found()
    ? '"CDG00Src", "CDG00AreaRepeatSrc", "ImgScanSrc"'
    : '"CDG00Src", "CDG00AreaRepeatSrc"',
)
runtime_manifest_data.set(
  'OPENCV_DEVELOPMENT_SOURCES',
  opencv_dep.found() ? 'true' : 'false',
)
runtime_manifest_data.set(
  'PRODUCT_PLUGIN_ENTRY',
  ',\n    {\n      "plugin": "ipm4",\n      "factories": ["ImageProcessGeometryNormalize", "ImageProcessMockDetector", "ImageProcessMockTracker", "ImageProcessTextSink"]\n    }',
)
runtime_manifest_data.set(
  'FILTER_PLUGIN_ENTRY',
  development_filters_option.disabled()
    ? ''
    : ',\n    {\n      "plugin": "msffilters",\n      "factories": ["ImageFilterTest"]\n    }',
)
runtime_manifest_data.set(
  'DEVELOPMENT_FILTERS',
  development_filters_option.disabled() ? 'false' : 'true',
)
runtime_manifest_data.set('MOCK_FILTERS', 'true')
lynxi_option = get_option('lynxi')
lynxi_sdk_prefix = get_option('lynxi_sdk_prefix')
if lynxi_sdk_prefix == ''
  lynxi_env = run_command(
    ['sh', '-c', 'printf %s "${LYNXI_SDK_PREFIX:-}"'],
    check: false,
  )
  if lynxi_env.returncode() == 0
    lynxi_sdk_prefix = lynxi_env.stdout().strip()
  endif
endif

lynxi_inc = []
lynxi_lib_dirs = []
lynxi_sdk_dep = []
lynxi_found = false
cpp = meson.get_compiler('cpp')
if not lynxi_option.disabled()
  prefixes = []
  if lynxi_sdk_prefix != ''
    prefixes += [lynxi_sdk_prefix]
  endif
  prefixes += ['/usr/local/lynxi', '/usr', '/usr/local']
  foreach prefix : prefixes
    if lynxi_found
      break
    endif
    header_candidates = [
      prefix / 'include' / 'lynxi' / 'lyn_api.h',
      prefix / 'include' / 'lyn_api.h',
      prefix / 'include' / 'lyn_api' / 'lyn_api.h',
      prefix / 'sdk' / 'include' / 'lyn_api' / 'lyn_api.h',
    ]
    foreach header : header_candidates
      if fs.is_file(header)
        header_dir = fs.parent(header)
        lib_dir_candidates = [
          prefix / 'sdk' / 'lib',
          prefix / 'bsp' / 'lib',
          prefix / 'lib',
          prefix / 'lib64',
        ]
        lib_dirs = []
        foreach d : lib_dir_candidates
          if fs.is_dir(d)
            lib_dirs += [d]
          endif
        endforeach
        inc_args = ['-I' + header_dir]
        bsp_inc = prefix / 'bsp' / 'include'
        if fs.is_file(bsp_inc / 'lyn_smi.h')
          inc_args += ['-I' + bsp_inc]
        endif
        lynxi_libs = []
        libs_ok = true
        foreach lib_name : ['LYNCHIPSDKCLIENT', 'LYNCHIPSDKCLIENTCOMM', 'LYNSMICLIENTCOMM']
          lib_dep = cpp.find_library(lib_name, dirs: lib_dirs, required: false)
          if not lib_dep.found()
            libs_ok = false
            break
          endif
          lynxi_libs += [lib_dep]
        endforeach
        if libs_ok
          lynxi_inc = [header_dir]
          lynxi_lib_dirs = lib_dirs
          lynxi_found = true
          lynxi_sdk_dep = lynxi_libs + [declare_dependency(
            compile_args: inc_args,
          )]
          break
        endif
      endif
    endforeach
  endforeach
  if lynxi_option.enabled() and not lynxi_found
    error('Lynxi detector requested but lyn_api.h / LYNCHIPSDKCLIENT were not found. Set -Dlynxi_sdk_prefix= or LYNXI_SDK_PREFIX.')
  endif
endif

runtime_manifest_data.set(
  'NPU_PLUGIN_ENTRY',
  lynxi_found
    ? ',\n    {\n      "plugin": "ipnpu",\n      "factories": ["ImageProcessLynxiDetector"]\n    }'
    : '',
)
runtime_manifest_data.set('NPU_DETECTOR', lynxi_found ? 'true' : 'false')
runtime_manifest = configure_file(
  input: 'configs/runtime/runtime-manifest.json.in',
  output: 'image-process-runtime-manifest.json',
  configuration: runtime_manifest_data,
)

if internal_runtime_option.enabled() or internal_runtime_option.auto()
  source_plugin_sources = files(
    'src/gst/sources/cdg00_src.cpp',
    'src/gst/sources/cdg00_area_repeat_src.cpp',
    'src/gst/sources/plugin_init.cpp',
  )
  source_plugin_cpp_args = runtime_cpp_args + ['-DIP_HAVE_OPENCV=0']
  source_plugin_dependencies = [gstreamer_dep, gstreamer_base_dep, gst_meta_dep]
  if opencv_dep.found()
    source_plugin_sources += files('src/gst/sources/img_scan_src.cpp')
    source_plugin_cpp_args = runtime_cpp_args + ['-DIP_HAVE_OPENCV=1']
    source_plugin_dependencies += opencv_dep
  endif

  shared_module(
    'gstmsfsrc',
    source_plugin_sources,
    include_directories: include_directories('src/gst/sources'),
    dependencies: source_plugin_dependencies,
    cpp_args: source_plugin_cpp_args,
    install: true,
    install_dir: runtime_install_dir,
  )

  shared_module(
    'gstipm4',
    files(
      'src/gst/filters/geometry_normalize.cpp',
      'src/gst/filters/mock_detector.cpp',
      'src/gst/filters/mock_tracker.cpp',
      'src/gst/sinks/text_sink.cpp',
      'src/gst/sinks/plugin_init.cpp',
    ),
    dependencies: [
      gstreamer_dep,
      gstreamer_base_dep,
      gstreamer_video_dep,
      gst_meta_dep,
      nlohmann_json_dep,
    ],
    cpp_args: runtime_cpp_args,
    install: true,
    install_dir: runtime_install_dir,
  )

  if lynxi_found
    opencv_lynxi_dep = dependency('opencv4', required: true)
    threads_dep = dependency('threads')
    lynxi_backend = static_library(
      'imageprocess-lynxi-backend',
      files(
        'src/backends/lynxi/infer/environment.cpp',
        'src/backends/lynxi/infer/smi.cpp',
        'src/backends/lynxi/infer/typeConv.cpp',
        'src/backends/lynxi/infer/yolovXPostProcess.cpp',
        'src/backends/lynxi/yolox_runtime.cpp',
      ),
      include_directories: include_directories(
        'src/backends/lynxi',
        'src/backends/lynxi/include',
        'src/backends/lynxi/infer',
      ),
      dependencies: lynxi_sdk_dep + [opencv_lynxi_dep, threads_dep, gstreamer_dep],
      override_options: ['warning_level=1'],
    )
    shared_module(
      'gstipnpu',
      files(
        'src/gst/filters/lynxi_detector.cpp',
        'src/gst/filters/lynxi_plugin_init.cpp',
      ),
      include_directories: include_directories(
        'src/backends/lynxi',
        'src/backends/lynxi/include',
        'src/backends/lynxi/infer',
      ),
      dependencies: [
        gstreamer_dep,
        gstreamer_base_dep,
        gstreamer_video_dep,
        gst_meta_dep,
        opencv_lynxi_dep,
        threads_dep,
      ] + lynxi_sdk_dep,
      link_with: lynxi_backend,
      cpp_args: runtime_cpp_args,
      install: true,
      install_dir: runtime_install_dir,
      build_rpath: ':'.join(lynxi_lib_dirs),
      install_rpath: ':'.join(lynxi_lib_dirs),
    )
  endif

  if not development_filters_option.disabled()
    shared_module(
      'gstmsffilters',
      files(
        'src/gst/filters/image_filter_test.cpp',
        'src/gst/filters/plugin_init.cpp',
      ),
      dependencies: [gstreamer_dep, gstreamer_base_dep],
      cpp_args: runtime_cpp_args,
      install: true,
      install_dir: runtime_install_dir,
    )
  endif
endif

image_process_lib = static_library(
  'image_process',
  files(
    'src/tool/artifact.cpp',
    'src/tool/image_process.cpp',
    'src/tool/pipeline.cpp',
  ),
  include_directories: include_directories('src/tool'),
  dependencies: [
    satellite_common_dep,
    gst_meta_dep,
    gstreamer_dep,
    gstreamer_app_dep,
    gstreamer_video_dep,
    glib_dep,
  ],
  cpp_args: [
    '-DIMAGE_PROCESS_SOURCE_ROOT="@0@"'.format(meson.current_source_dir()),
    '-DIMAGE_PROCESS_VERSION="@0@"'.format(meson.project_version()),
    '-DIMAGE_PROCESS_RUNTIME_MANIFEST_PATH="@0@"'.format(runtime_manifest.full_path()),
  ],
)

image_process_exe = executable(
  'image-process',
  'src/tool/main.cpp',
  include_directories: include_directories('src/tool'),
  link_with: image_process_lib,
  dependencies: [satellite_common_dep],
  cpp_args: ['-DIMAGE_PROCESS_VERSION="@0@"'.format(meson.project_version())],
  install: true,
)

install_data(
  'configs/plugins/image-process.json',
  install_dir: get_option('datadir') / 'satellite/plugins',
)

install_data(
  'configs/runtime/profiles.json',
  install_dir: get_option('datadir') / 'image-process/runtime',
)

install_data(
  'configs/runtime/category-maps/mock-optical-v1.json',
  'configs/runtime/category-maps/lynxi-optical-v1.json',
  install_dir: get_option('datadir') / 'image-process/runtime/category-maps',
)

install_data(
  'configs/runtime/models/lynxi-yolov8n-onnx.v1.json',
  install_dir: get_option('datadir') / 'image-process/runtime/models',
)

install_data(
  runtime_manifest,
  rename: 'runtime-manifest.json',
  install_dir: get_option('datadir') / 'image-process/runtime',
)

install_data(
  'schemas/image_process.input.schema.json',
  'schemas/image_process.output.schema.json',
  'schemas/runtime_manifest.schema.json',
  'schemas/source_provenance.schema.json',
  install_dir: get_option('datadir') / 'image-process/schemas',
)

install_data(
  'SOURCE_PROVENANCE.json',
  'NOTICE',
  'LICENSES/MSF-PROJECT.txt',
  install_dir: get_option('datadir') / 'image-process',
)

install_headers(
  'src/gst/meta/image_process/gst_meta_v1.h',
  subdir: 'image-process/image_process',
)

install_data(
  'samples/pushbroom_fixture.json',
  'samples/stare_cdg00.json',
  'samples/cdg00_parse_host.json',
  'samples/m4_sat_c_pushbroom.json',
  'samples/m4_sat_a_stare.json',
  'samples/npu_hawaii_pushbroom.json',
  install_dir: get_option('datadir') / 'image-process/samples',
)

test(
  'image-process-contract',
  find_program('scripts/contract-test.sh'),
  args: [image_process_exe],
  env: [
    'IMAGE_PROCESS_SOURCE_ROOT=' + meson.current_source_dir(),
    'IMAGE_PROCESS_GST_PLUGIN_PATH=' + meson.current_build_dir(),
  ],
  timeout: 60,
)

test(
  'image-process-m4-contract',
  find_program('scripts/contract-test-m4.sh'),
  args: [image_process_exe],
  env: [
    'IMAGE_PROCESS_SOURCE_ROOT=' + meson.current_source_dir(),
    'IMAGE_PROCESS_GST_PLUGIN_PATH=' + meson.current_build_dir(),
  ],
  timeout: 180,
)

gst_meta_v1_test = executable(
  'gst-meta-v1-test',
  'tests/gst_meta_v1_test.cpp',
  dependencies: [gst_meta_dep],
)

test('gst-meta-v1-contract', gst_meta_v1_test)

summary('SDK via subproject', 'satellite-plugin-sdk', section: 'Dependencies')
summary('internal runtime', internal_runtime_option, section: 'Runtime')
summary('OpenCV development sources', opencv_dep.found(), section: 'Runtime')
summary('development filters', development_filters_option, section: 'Runtime')
summary('Lynxi detector', lynxi_found, section: 'Runtime')
summary('runtime install dir', runtime_install_dir, section: 'Runtime')
