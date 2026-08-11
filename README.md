# image-process

`image-process` is a standalone `satellite-plugin-sdk` tools plugin that builds
controlled GStreamer pipelines from installed, allowlisted runtime profiles.
Requests select a sensor, acquisition mode, and profile; they cannot provide raw
pipeline strings, element names, plugin search paths, or model paths.

The repository intentionally has no remote at this stage. The containing
`satellite-workspace` checkout can build this local repository, while portable
cross-machine acquisition remains a publication concern.

## Development build

```sh
ln -s ../../../sdk subprojects/satellite-plugin-sdk
meson setup build
meson compile -C build
./scripts/contract-test.sh build/image-process
```

MSF factories are referenced by the `msf.*` profiles in
`configs/runtime/profiles.json`. Make the matching MSF runtime bundle available
through the deployment environment before running those profiles:

```sh
export IMAGE_PROCESS_GST_PLUGIN_PATH=/path/to/msf-runtime
export GST_PLUGIN_PATH_1_0="$IMAGE_PROCESS_GST_PLUGIN_PATH:${GST_PLUGIN_PATH_1_0:-}"
export GST_PLUGIN_PATH="$IMAGE_PROCESS_GST_PLUGIN_PATH:${GST_PLUGIN_PATH:-}"
./scripts/msf-dev-smoke.sh "$IMAGE_PROCESS_GST_PLUGIN_PATH"
```

`install/env.sh` performs the same registration for the standard immutable
bundle directory `install/${libdir}/satellite/image-process/gstreamer-1.0`.
Runtime
plugins and their private shared-library dependencies belong in that directory.
The request schema deliberately contains no plugin-path field.
