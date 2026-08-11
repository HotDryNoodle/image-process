# image-process

`image-process` is a standalone `satellite-plugin-sdk` tools plugin that builds
controlled GStreamer pipelines from installed, allowlisted runtime profiles.
Requests select a sensor, acquisition mode, and profile; they cannot provide raw
pipeline strings, element names, plugin search paths, or model paths.

The canonical repository is
`https://github.com/HotDryNoodle/image-process.git`. The containing
`satellite-workspace` consumes a pinned commit through its submodule and
`VERSIONS.lock` entries.

## Development build

```sh
meson setup build
meson compile -C build
./scripts/contract-test.sh build/image-process
```

Meson obtains JSON and the SDK sources from pinned wraps. The SDK wrap pins the
canonical in-tree `sdk/` at a specific `satellite-workspace` commit; the SDK no
longer has a separate repository. Workspace builds replace this wrap with the
current parent repository's in-tree `sdk/` through the bootstrap scripts.

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
