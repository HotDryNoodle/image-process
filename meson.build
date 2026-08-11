project('image-process', 'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=2',
    'default_library=static',
  ],
)

nlohmann_json_sp = subproject('nlohmann_json')
sdk_sp = subproject('satellite-plugin-sdk')
satellite_common_dep = sdk_sp.get_variable('satellite_common_dep')

gstreamer_dep = dependency('gstreamer-1.0', version: '>=1.20')
gstreamer_app_dep = dependency('gstreamer-app-1.0', version: '>=1.20')
gstreamer_video_dep = dependency('gstreamer-video-1.0', version: '>=1.20')
glib_dep = dependency('glib-2.0')

image_process_lib = static_library(
  'image_process',
  files(
    'src/artifact.cpp',
    'src/image_process.cpp',
    'src/pipeline.cpp',
  ),
  include_directories: include_directories('src'),
  dependencies: [
    satellite_common_dep,
    gstreamer_dep,
    gstreamer_app_dep,
    gstreamer_video_dep,
    glib_dep,
  ],
  cpp_args: [
    '-DIMAGE_PROCESS_SOURCE_ROOT="@0@"'.format(meson.current_source_dir()),
    '-DIMAGE_PROCESS_VERSION="@0@"'.format(meson.project_version()),
  ],
)

image_process_exe = executable(
  'image-process',
  'src/main.cpp',
  include_directories: include_directories('src'),
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
  'schemas/image_process.input.schema.json',
  'schemas/image_process.output.schema.json',
  install_dir: get_option('datadir') / 'image-process/schemas',
)

install_data(
  'samples/pushbroom_fixture.json',
  'samples/stare_cdg00.json',
  install_dir: get_option('datadir') / 'image-process/samples',
)

test(
  'image-process-contract',
  find_program('scripts/contract-test.sh'),
  args: [image_process_exe],
  env: ['IMAGE_PROCESS_SOURCE_ROOT=' + meson.current_source_dir()],
  timeout: 60,
)

summary('SDK via subproject', 'satellite-plugin-sdk', section: 'Dependencies')
