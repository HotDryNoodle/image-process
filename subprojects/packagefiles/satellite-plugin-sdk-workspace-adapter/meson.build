project('satellite-plugin-sdk', 'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=2',
    'default_library=static',
  ],
)

json_dep = dependency('nlohmann_json')
sdk_root = meson.current_source_dir() / 'sdk'
sdk_inc = include_directories('sdk/src')

satellite_common_lib = static_library(
  'satellite_common',
  files(
    'sdk/src/satellite/json_io.cpp',
    'sdk/src/satellite/manifest.cpp',
    'sdk/src/satellite/result_envelope.cpp',
    'sdk/src/satellite/schema_validate.cpp',
    'sdk/src/satellite/cli_options.cpp',
  ),
  cpp_args: [
    '-DSATELLITE_SDK_SCHEMAS_DIR="@0@"'.format(sdk_root / 'schemas'),
  ],
  include_directories: sdk_inc,
  dependencies: [json_dep],
)

satellite_common_dep = declare_dependency(
  link_with: satellite_common_lib,
  include_directories: sdk_inc,
  dependencies: [json_dep],
)
