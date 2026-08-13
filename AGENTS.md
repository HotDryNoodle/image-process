# Repository Guidelines

This is the standalone `image-process` tools plugin repository. Its canonical
remote is `https://github.com/HotDryNoodle/image-process.git`.

- Build with C++17 and Meson `warning_level=2`.
- Follow the parent workspace `.clang-format` and C++ naming rules.
- Keep requests declarative: never accept raw GStreamer launch strings, element
  factories, plugin paths, or model paths from a task payload.
- Runtime profiles under `configs/runtime/` are the only pipeline allowlist.
- Pushbroom profiles must be detection-only; stare profiles must contain the
  tracking role.
- Keep reusable runtime source under `runtime/`; never vendor the whole MSF
  tree or add a long-lived MSF submodule/subtree. Every MSF-derived path must
  be bounded by `SOURCE_PROVENANCE.json`, retain origin revision/path/license,
  and pass the fail-closed license inventory before entering a bundle.
- Runtime elements and the collector exchange metadata only through the
  versioned C ABI in `runtime/meta/include/`; do not interpret plugin-private
  native layouts in product code.
- Run `scripts/contract-test.sh build/image-process` before the commit gate.
