#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
runtime_dir="${1:?usage: msf-dev-smoke.sh RUNTIME_DIR [IMAGE_PROCESS_BINARY]}"
binary="${2:-${repo_dir}/build/image-process}"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/image-process-msf-dev.XXXXXX")"
trap 'rm -rf "${test_dir}"' EXIT

if [[ ! -d "${runtime_dir}" ]]; then
  echo "error: image-process runtime directory not found: ${runtime_dir}" >&2
  exit 2
fi
runtime_dir="$(cd "${runtime_dir}" && pwd)"
if [[ ! -x "${binary}" ]]; then
  echo "error: image-process binary not found: ${binary}" >&2
  exit 2
fi
binary="$(cd "$(dirname "${binary}")" && pwd)/$(basename "${binary}")"

export IMAGE_PROCESS_SOURCE_ROOT="${repo_dir}"
export IMAGE_PROCESS_GST_PLUGIN_PATH="${runtime_dir}"
export GST_PLUGIN_PATH_1_0="${runtime_dir}:${GST_PLUGIN_PATH_1_0:-}"
export GST_PLUGIN_PATH="${runtime_dir}:${GST_PLUGIN_PATH:-}"
runtime_library_path="${runtime_dir}"
for dependency in gstreamer-1.0 glib-2.0 gobject-2.0; do
  dependency_lib_dir="$(pkg-config --variable=libdir "${dependency}" 2>/dev/null || true)"
  if [[ -n "${dependency_lib_dir}" ]]; then
    runtime_library_path="${runtime_library_path}:${dependency_lib_dir}"
  fi
done
export LD_LIBRARY_PATH="${runtime_library_path}:${LD_LIBRARY_PATH:-}"
export DYLD_LIBRARY_PATH="${runtime_library_path}:${DYLD_LIBRARY_PATH:-}"
export GST_REGISTRY="${test_dir}/registry.bin"
export MSF_LOG_CONSOLE=0

(
  cd "${repo_dir}"
  "${binary}" run \
    --input samples/pushbroom_msf_image_sequence.json \
    --work-dir "${test_dir}/dry-run" --dry-run --output json \
    >"${test_dir}/dry-run.json"
  "${binary}" run \
    --input samples/pushbroom_msf_image_sequence.json \
    --work-dir "${test_dir}/run" --output json \
    >"${test_dir}/run.json"
)

python3 - "${test_dir}" <<'PY'
import json
import pathlib
import tarfile
import sys

tmp = pathlib.Path(sys.argv[1])
dry = json.loads((tmp / "dry-run.json").read_text())
assert dry["pipeline_plan"]["source"]["factory"] == "ImgScanSrc"
assert dry["pipeline_plan"]["filter"] == {
    "factory": "ImageFilterTest",
    "role": "detection",
    "properties": {},
}

result = json.loads((tmp / "run.json").read_text())
assert result["status"] == "completed"
assert result["frame_count"] == 2
assert result["provenance"]["source"]["plugin"] == "msfsrc"
assert result["provenance"]["filter"]["plugin"] == "msffilters"
assert result["provenance"]["source"]["plugin_license"] == "MIT"
assert result["provenance"]["runtime"]["meta_abi"] == "image-process.gst-meta.v1"

with tarfile.open(tmp / "run" / result["artifact"]["path"]) as archive:
    rows = archive.extractfile("meta/frames.jsonl").read().decode().splitlines()
    metadata = [json.loads(row) for row in rows]
assert len(metadata) == 2
assert all("meta_image_dir_api" in row["meta_apis"] for row in metadata)
assert all(row["video"] == {"format": "GRAY8", "height": 4, "width": 4} for row in metadata)
PY

mkdir -p "${test_dir}/sequence-with-symlink"
ln -s "${repo_dir}/samples/fixtures/frame_001.pgm" \
  "${test_dir}/sequence-with-symlink/frame_001.pgm"
python3 - "${repo_dir}/samples/pushbroom_msf_image_sequence.json" \
  "${test_dir}/sequence-with-symlink" "${test_dir}/symlink-input.json" <<'PY'
import json
import pathlib
import sys

request = json.loads(pathlib.Path(sys.argv[1]).read_text())
request["input"]["path"] = sys.argv[2]
pathlib.Path(sys.argv[3]).write_text(json.dumps(request))
PY
set +e
"${binary}" run --input "${test_dir}/symlink-input.json" \
  --work-dir "${test_dir}/symlink-run" --output json \
  >"${test_dir}/symlink.out" 2>"${test_dir}/symlink.err"
symlink_status=$?
set -e
test "${symlink_status}" -eq 2
grep -q 'image-sequence directory contains an unsupported entry' \
  "${test_dir}/symlink.out"
test ! -e "${test_dir}/symlink-run/result.json"

echo "image-process MSF development smoke passed"
