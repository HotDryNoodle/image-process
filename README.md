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

Meson obtains the SDK and JSON dependency from the pinned wraps. Workspace
builds replace the SDK wrap with the parent repository's in-tree `sdk/` through
the workspace bootstrap scripts. A standalone build requires the pinned private
SDK repository to exist and be accessible; until that repository is published,
use the workspace build rather than changing or bypassing the SDK pin.

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
