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

## Host CDG0.0 real-data test

The host-only parse profile reuses MSF `CDG00Src`, scales the decoded frames to
1024×1024, and writes an Ogg/Theora `video.ogv` through the same GStreamer
`theoraenc ! oggmux` path used by openEuler. It also writes concise
`meta/cdg00.jsonl` records containing
only frame id, GPS time, LLA, velocity, and roll/pitch/yaw values from the MSF
window-start sample. Zero navigation values are passed through but do not imply
valid navigation data. The profile does not enable detection or tracking models
and does not retain raw gray/frame bins.

```sh
export IMAGE_PROCESS_GST_PLUGIN_PATH=/path/to/msf-runtime
export GST_PLUGIN_PATH="$IMAGE_PROCESS_GST_PLUGIN_PATH:${GST_PLUGIN_PATH:-}"
./scripts/host-cdg00-test.sh \
  build/image-process \
  /path/to/20250404142111_A1_0.dat \
  /path/to/test-work-dir
```

The test pins the fixture basename, size, and SHA-256, runs to EOS, validates
all 64 encoded frames and concise metadata records, verifies Ogg/Theora with
GStreamer decode-to-EOS and FFmpeg tooling, and exercises bad-digest,
symlink-input, and frame-limit failures. The final work directory contains only
`video.ogv`, `meta/cdg00.jsonl`, `result.json`, and `pipeline-plan.json`.
