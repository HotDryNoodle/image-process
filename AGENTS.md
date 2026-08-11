# Repository Guidelines

This is the standalone `image-process` tools plugin repository. It intentionally
has no remote at the current stage.

- Build with C++17 and Meson `warning_level=2`.
- Follow the parent workspace `.clang-format` and C++ naming rules.
- Keep requests declarative: never accept raw GStreamer launch strings, element
  factories, plugin paths, or model paths from a task payload.
- Runtime profiles under `configs/runtime/` are the only pipeline allowlist.
- Pushbroom profiles must be detection-only; stare profiles must contain the
  tracking role.
- Do not vendor the MSF source tree. Consume a separately prepared runtime
  bundle and preserve its revision and ABI provenance.
- Run `scripts/contract-test.sh build/image-process` before the commit gate.
