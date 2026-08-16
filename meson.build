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
  opencv_dep.found() ? '"CDG00Src", "ImgScanSrc"' : '"CDG00Src"',
)
runtime_manifest_data.set(
  'OPENCV_DEVELOPMENT_SOURCES',
  opencv_dep.found() ? 'true' : 'false',
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
runtime_manifest = configure_file(
  input: 'configs/runtime/runtime-manifest.json.in',
  output: 'image-process-runtime-manifest.json',
  configuration: runtime_manifest_data,
)

if internal_runtime_option.enabled() or internal_runtime_option.auto()
  source_plugin_sources = files(
    'src/gst/sources/cdg00_src.cpp',
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
    dependencies: source_plugin_dependencies,
    cpp_args: source_plugin_cpp_args,
    install: true,
    install_dir: runtime_install_dir,
  )

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
  install_dir: get_option('datadir') / 'image-process/samples',
)

test(
  'image-process-contract',
  find_program('scripts/contract-test.sh'),
  args: [image_process_exe],
  env: ['IMAGE_PROCESS_SOURCE_ROOT=' + meson.current_source_dir()],
  timeout: 60,
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
summary('runtime install dir', runtime_install_dir, section: 'Runtime')
