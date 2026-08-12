#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${1:-${repo_dir}/build/image-process}"
data_file="${2:-}"
work_dir="${3:-}"

if [[ -z "${data_file}" || -z "${work_dir}" ]]; then
  echo "usage: $0 [binary] DATA_FILE WORK_DIR" >&2
  exit 64
fi

expected_name="20250404142111_A1_0.dat"
expected_size="52822016"
expected_sha256="336a30600aacb1a747829c717d7f71abce8a24ff3744ae7698d2d91da3def01e"

actual_name="$(basename "${data_file}")"
actual_size="$(stat -f '%z' "${data_file}" 2>/dev/null || stat -c '%s' "${data_file}")"
actual_sha256="$(shasum -a 256 "${data_file}" | awk '{print $1}')"
test "${actual_name}" = "${expected_name}"
test "${actual_size}" = "${expected_size}"
test "${actual_sha256}" = "${expected_sha256}"

for command in python3 ffprobe gst-discoverer-1.0 gst-launch-1.0; do
  command -v "${command}" >/dev/null || {
    echo "error: required host validator is missing: ${command}" >&2
    exit 1
  }
done

test_root="$(mktemp -d "${TMPDIR:-/tmp}/image-process-cdg00.XXXXXX")"
trap 'rm -rf "${test_root}"' EXIT
request="${test_root}/request.json"

python3 - "${repo_dir}/samples/cdg00_parse_host.json" "${data_file}" "${request}" <<'PY'
import json
import pathlib
import sys

request = json.loads(pathlib.Path(sys.argv[1]).read_text())
request["input"]["path"] = str(pathlib.Path(sys.argv[2]).resolve())
pathlib.Path(sys.argv[3]).write_text(json.dumps(request))
PY

if [[ -e "${work_dir}" ]] && [[ -n "$(find "${work_dir}" -mindepth 1 -print -quit)" ]]; then
  echo "error: work directory must be empty: ${work_dir}" >&2
  exit 2
fi
mkdir -p "${work_dir}"
export IMAGE_PROCESS_SOURCE_ROOT="${repo_dir}"

run_image_process() {
  local input="$1"
  local output_dir="$2"
  local stdout_file="$3"
  local stderr_file="$4"
  if [[ -n "${IMAGE_PROCESS_TASK_CLIENT:-}" ]]; then
    "${IMAGE_PROCESS_TASK_CLIENT}" \
      --plugin-bin "${IMAGE_PROCESS_PLUGIN_BIN:-$(dirname "${binary}")}" \
      plugin-run --tool image.process --input "${input}" \
      --work-dir "${output_dir}" >"${stdout_file}" 2>"${stderr_file}"
  else
    "${binary}" run --input "${input}" --work-dir "${output_dir}" \
      --output json >"${stdout_file}" 2>"${stderr_file}"
  fi
}

python3 - "${request}" "${test_root}/bad-digest.json" "${test_root}/frame-limit.json" <<'PY'
import json
import pathlib
import sys

request = json.loads(pathlib.Path(sys.argv[1]).read_text())
bad_digest = json.loads(json.dumps(request))
bad_digest["input"]["sha256"] = "0" * 64
pathlib.Path(sys.argv[2]).write_text(json.dumps(bad_digest))
frame_limit = json.loads(json.dumps(request))
frame_limit["processing"]["max_frames"] = 1
pathlib.Path(sys.argv[3]).write_text(json.dumps(frame_limit))
PY

set +e
run_image_process "${test_root}/bad-digest.json" "${test_root}/bad-digest" \
  "${test_root}/bad-digest.out" "${test_root}/bad-digest.err"
bad_digest_status=$?
run_image_process "${test_root}/frame-limit.json" "${test_root}/frame-limit" \
  "${test_root}/frame-limit.out" "${test_root}/frame-limit.err"
frame_limit_status=$?
set -e
test "${bad_digest_status}" -eq 2
test "${frame_limit_status}" -eq 5
test -z "$(find "${test_root}/bad-digest" "${test_root}/frame-limit" \
  -type f \( -name '*.partial' -o -name 'video.ogv' -o -name 'video.mp4' \
    -o -name 'product.bin' \) \
  -print 2>/dev/null)"

ln -s "${data_file}" "${test_root}/${expected_name}"
python3 - "${request}" "${test_root}/${expected_name}" "${test_root}/symlink.json" <<'PY'
import json
import pathlib
import sys

request = json.loads(pathlib.Path(sys.argv[1]).read_text())
request["input"]["path"] = sys.argv[2]
pathlib.Path(sys.argv[3]).write_text(json.dumps(request))
PY
set +e
run_image_process "${test_root}/symlink.json" "${test_root}/symlink" \
  "${test_root}/symlink.out" "${test_root}/symlink.err"
symlink_status=$?
set -e
test "${symlink_status}" -eq 2

run_image_process "${request}" "${work_dir}" "${test_root}/stdout.json" \
  "${test_root}/run.err"

gst-discoverer-1.0 "${work_dir}/video.ogv" \
  >"${test_root}/gst-discoverer.txt"
gst-launch-1.0 -q filesrc location="${work_dir}/video.ogv" \
  ! oggdemux ! theoradec ! fakesink
ffprobe -v error -count_frames -select_streams v:0 \
  -show_entries stream=codec_name,width,height,r_frame_rate,nb_read_frames \
  -of json "${work_dir}/video.ogv" >"${test_root}/ffprobe.json"

python3 - "${work_dir}" "${test_root}/stdout.json" "${data_file}" \
  "${repo_dir}" "${test_root}/ffprobe.json" <<'PY'
import hashlib
import json
import pathlib
import sys

work = pathlib.Path(sys.argv[1])
stdout_path = pathlib.Path(sys.argv[2])
stdout = json.loads(stdout_path.read_text())
data_file = pathlib.Path(sys.argv[3]).resolve()
repo = pathlib.Path(sys.argv[4])
ffprobe = json.loads(pathlib.Path(sys.argv[5]).read_text())

assert stdout["status"] == "completed"
assert stdout["runtime_profile"] == "msf.cdg00.parse-host.v1"
assert stdout["frame_count"] == 64
plan = stdout["pipeline_plan"]
assert plan["source"]["factory"] == "CDG00Src"
metadata_contract = plan["metadata_probe"]["contract"]
assert metadata_contract["sample_point"] == "window_start"
assert metadata_contract["fields"] == [
    "frame_id", "lla", "rpy", "velocity", "gps_time"
]
assert metadata_contract["gps_time_scale"] == "GPS"
assert metadata_contract["zero_values_do_not_imply_valid"] is True
assert plan["video"]["caps"] == {
    "format": "I420",
    "width": 1024,
    "height": 1024,
    "framerate_num": 30,
    "framerate_den": 1,
}
assert plan["video"]["encoder"]["factory"] == "theoraenc"
assert plan["video"]["encoder"]["properties"] == {
    "bitrate": 6000,
    "drop-frames": False,
    "keyframe-force": 30,
    "speed-level": 3,
}
assert "parser" not in plan["video"]
assert plan["video"]["muxer"] == {"factory": "oggmux", "properties": {}}

video = work / "video.ogv"
metadata = work / "meta/cdg00.jsonl"
result = work / "result.json"
plan_file = work / "pipeline-plan.json"
expected_files = {video, metadata, result, plan_file}
actual_files = {path for path in work.rglob("*") if path.is_file()}
assert actual_files == expected_files, sorted(str(path) for path in actual_files)
assert 0 < video.stat().st_size <= 64 * 1024 * 1024
assert metadata.stat().st_size > 0
assert not list(work.rglob("*.partial"))
assert not list(work.rglob("*.bin"))
assert not (work / "video.gray").exists()
assert not (work / "product.bin").exists()

streams = ffprobe["streams"]
assert len(streams) == 1, streams
stream = streams[0]
assert stream["codec_name"] == "theora"
assert stream["width"] == 1024 and stream["height"] == 1024
assert stream["r_frame_rate"] == "30/1"
assert int(stream["nb_read_frames"]) == 64

records = [json.loads(line) for line in metadata.read_text().splitlines()]
assert len(records) == 64
assert [record["frame_id"] for record in records] == list(range(64))
for record in records:
    assert set(record) == {"frame_id", "gps_time", "lla", "rpy", "velocity"}
    assert set(record["gps_time"]) == {"scale", "week", "seconds"}
    assert record["gps_time"] == {"scale": "GPS", "week": 0, "seconds": 0}
    assert record["lla"] == [0.0, 0.0, 0.0]
    assert record["rpy"] == [0.0, 0.0, 0.0]
    assert record["velocity"] == [0.0, 0.0, 0.0]
for forbidden in (
    "channel_id", "strip_number", "row_number", "time_sync_status",
    "exposure", "timestamp", "camera_time", "window_end", "native-v1",
):
    assert forbidden not in metadata.read_text()

result_doc = json.loads(result.read_text())
assert result_doc == stdout
for document in (result.read_text(), metadata.read_text(), plan_file.read_text()):
    assert str(data_file) not in document
assert result_doc["resource_usage"]["peak_rss_bytes"] <= 512 * 1024 * 1024
assert result_doc["resource_usage"]["wall_time_seconds"] <= 60
assert result_doc["resource_usage"]["input_bytes"] == data_file.stat().st_size
assert result_doc["resource_usage"]["video_output_bytes"] == video.stat().st_size
assert result_doc["resource_usage"]["metadata_output_bytes"] == metadata.stat().st_size
assert result_doc["resource_usage"]["frames_per_second"] > 0

for key, path in (("artifact", video), ("meta_artifact", metadata)):
    entry = stdout[key]
    assert (work / entry["path"]) == path
    assert entry["size_bytes"] == path.stat().st_size
    assert hashlib.sha256(path.read_bytes()).hexdigest() == entry["sha256"]
assert stdout["artifact"]["media_type"] == "video/ogg;codecs=theora"
assert stdout["meta_artifact"]["media_type"] == "application/x-ndjson"

try:
    import jsonschema
except ImportError:
    jsonschema = None
if jsonschema is not None:
    schema = json.loads((repo / "schemas/image_process.output.schema.json").read_text())
    jsonschema.validate(stdout, schema)

print(json.dumps({
    "video_bytes": video.stat().st_size,
    "metadata_bytes": metadata.stat().st_size,
    "wall_time_seconds": result_doc["resource_usage"]["wall_time_seconds"],
    "peak_rss_bytes": result_doc["resource_usage"]["peak_rss_bytes"],
    "frames_per_second": result_doc["resource_usage"]["frames_per_second"],
}, sort_keys=True))
PY

if [[ -n "${IMAGE_PROCESS_TASK_CLIENT:-}" ]]; then
  echo "image-process TM-client CDG00 test passed"
else
  echo "image-process host CDG00 test passed"
fi
