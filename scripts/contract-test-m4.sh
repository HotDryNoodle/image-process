#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${1:-${repo_dir}/build/image-process}"
binary_dir="$(cd "$(dirname "${binary}")" && pwd)"
fixture="${repo_dir}/data/20250404142111_A1_0.dat"

if [[ ! -f "${fixture}" ]]; then
  echo "EIP2-M4 skipped: missing gitignored fixture ${fixture}"
  exit 0
fi

export IMAGE_PROCESS_SOURCE_ROOT="${repo_dir}"
export IMAGE_PROCESS_GST_PLUGIN_PATH="${IMAGE_PROCESS_GST_PLUGIN_PATH:-${binary_dir}}"
export GST_PLUGIN_PATH="${IMAGE_PROCESS_GST_PLUGIN_PATH}${GST_PLUGIN_PATH:+:${GST_PLUGIN_PATH}}"

test_dir="$(mktemp -d "${TMPDIR:-/tmp}/image-process-m4.XXXXXX")"
trap 'rm -rf "${test_dir}"' EXIT

python3 - "${repo_dir}" "${fixture}" "${test_dir}" <<'PY'
import json
import pathlib
import sys

repo = pathlib.Path(sys.argv[1])
fixture = pathlib.Path(sys.argv[2])
tmp = pathlib.Path(sys.argv[3])
digest = "336a30600aacb1a747829c717d7f71abce8a24ff3744ae7698d2d91da3def01e"
size = 52822016

def write_request(name, sample, profile=None, mode=None, sensor=None):
    request = json.loads((repo / "samples" / sample).read_text())
    request["input"]["path"] = str(fixture)
    request["input"]["sha256"] = digest
    request["input"]["size_bytes"] = size
    if profile is not None:
        request["processing"]["runtime_profile"] = profile
    if mode is not None:
        request["acquisition_mode"] = mode
    if sensor is not None:
        request["sensor"]["id"] = sensor
    path = tmp / name
    path.write_text(json.dumps(request))
    return path

write_request("eip2-m4-01.json", "m4_sat_c_pushbroom.json")
write_request("eip2-m4-02.json", "m4_sat_a_stare.json")
write_request("eip2-m4-03-a.json", "m4_sat_a_stare.json",
              profile="mock.sat-c.pushbroom.v1", mode="pushbroom",
              sensor="sat-a.area-array")
write_request("eip2-m4-03-c.json", "m4_sat_c_pushbroom.json",
              profile="mock.sat-a.stare.v1", mode="stare",
              sensor="sat-c.line-array")
write_request("eip2-m4-04-zero-targets.json", "m4_sat_c_pushbroom.json",
              profile="mock.sat-c.pushbroom.zero.v1")
write_request("eip2-m4-04-zero-frames.json", "m4_sat_c_pushbroom.json",
              profile="mock.sat-c.pushbroom.zero-frame.v1")
write_request("eip2-m4-05.json", "m4_sat_c_pushbroom.json",
              profile="mock.sat-c.pushbroom.scale-pad.v1")
PY

run_json() {
  local input="$1"
  local work="$2"
  local out="$3"
  "${binary}" run --input "${input}" --work-dir "${work}" --output json >"${out}"
}

# EIP2-M4-01
run_json "${test_dir}/eip2-m4-01.json" "${test_dir}/run-01" "${test_dir}/run-01.json"

# EIP2-M4-02
run_json "${test_dir}/eip2-m4-02.json" "${test_dir}/run-02" "${test_dir}/run-02.json"

# EIP2-M4-03
set +e
"${binary}" validate --input "${test_dir}/eip2-m4-03-a.json" \
  >"${test_dir}/eip2-m4-03-a.out"
status_a=$?
"${binary}" validate --input "${test_dir}/eip2-m4-03-c.json" \
  >"${test_dir}/eip2-m4-03-c.out"
status_c=$?
set -e
test "${status_a}" -eq 2
test "${status_c}" -eq 2

# EIP2-M4-04 zero targets
run_json "${test_dir}/eip2-m4-04-zero-targets.json" \
  "${test_dir}/run-04-zero-targets" "${test_dir}/run-04-zero-targets.json"

# EIP2-M4-04 zero frames
set +e
"${binary}" run --input "${test_dir}/eip2-m4-04-zero-frames.json" \
  --work-dir "${test_dir}/run-04-zero-frames" --output json \
  >"${test_dir}/run-04-zero-frames.json"
zero_frame_status=$?
set -e
test "${zero_frame_status}" -eq 4

# EIP2-M4-05
run_json "${test_dir}/eip2-m4-05.json" "${test_dir}/run-05" "${test_dir}/run-05.json"

python3 - "${test_dir}" <<'PY'
import json
import pathlib
import sys

tmp = pathlib.Path(sys.argv[1])
digest = "336a30600aacb1a747829c717d7f71abce8a24ff3744ae7698d2d91da3def01e"

def load_json(path):
    return json.loads(path.read_text())

def load_jsonl(path):
    lines = [json.loads(line) for line in path.read_text().splitlines() if line]
    return lines

def assert_no_row_keys(record):
    assert "schema" not in record
    assert "image_id" not in record
    assert "coordinates" not in record

run_01 = load_json(tmp / "run-01.json")
assert run_01["status"] == "completed"
assert run_01["frame_count"] == 1
assert run_01["target_count"] == 1
assert run_01["image_id"] == f"sha256:{digest}"
assert run_01["artifacts"]["targets"]["schema"] == "image-process.target-frame.v1"
assert run_01["provenance"]["filter_backend"] == "mock"
assert run_01["provenance"]["evidence_class"] == "host_mock_filter"
assert run_01["provenance"]["filter_factory"] == "ImageProcessMockDetector"
assert run_01["provenance"]["source"]["factory"] == "CDG00Src"
assert "appsink" not in json.dumps(run_01["pipeline_plan"])

targets = load_jsonl(tmp / "run-01/targets.jsonl")
assert len(targets) == 1
record = targets[0]
assert_no_row_keys(record)
assert record["frame_id"] == 0
assert record["observation_time"]["scale"] == "camera"
assert "seconds" in record["observation_time"]
assert "microseconds" in record["observation_time"]
assert record["exposure_duration"]["unit"] == "ns"
bbox = record["targets"][0]["bbox"]
assert bbox == {"x_min": 100.0, "y_min": 40.0, "x_max": 140.0, "y_max": 80.0}
item = record["targets"][0]
assert item["class"]["index"] == 1
assert item["class"]["type"] == "mock.ship"
assert item["class"]["mapping_id"] == "mock-optical-v1"
assert item["confidence"] == 0.9
assert item["geolocation_sample"]["line"] == 60.0
assert item["geolocation_sample"]["pixel"] == 120.0
meta = load_jsonl(tmp / "run-01/image-meta.jsonl")
assert len(meta) == 1
assert meta[0]["sensor_id"] == "sat-c.line-array"
assert meta[0]["acquisition_mode"] == "pushbroom"
assert meta[0]["geometry_id"] == "identity.v1"
assert meta[0]["observation_time"] == record["observation_time"]

run_02 = load_json(tmp / "run-02.json")
assert run_02["status"] == "completed"
assert run_02["frame_count"] == 3
assert run_02["track_observation_count"] == 3
assert run_02["provenance"]["filter_backend"] == "mock"
assert run_02["provenance"]["filter_factory"] == "ImageProcessMockTracker"
assert run_02["provenance"]["source"]["factory"] == "CDG00AreaRepeatSrc"
assert run_02["provenance"]["source"]["factory"] != "CDG00Src"
tracks = load_jsonl(tmp / "run-02/tracks.jsonl")
assert len(tracks) == 3
times = []
for index, frame in enumerate(tracks):
    assert_no_row_keys(frame)
    assert frame["frame_id"] == index
    assert "line" not in json.dumps(frame["tracks"][0]["image_sample"])
    assert frame["tracks"][0]["track_id"] == 7
    assert frame["tracks"][0]["class"]["type"] == "mock.vehicle"
    assert frame["tracks"][0]["image_sample"]["row"] == 60.0
    assert frame["tracks"][0]["image_sample"]["column"] == 40.0
    assert frame["tracks"][0]["bbox"] == {
        "x_min": 20.0, "y_min": 30.0, "x_max": 60.0, "y_max": 90.0
    }
    times.append(frame["observation_time"])
assert times[0] == times[1] == times[2]
assert "confidence" not in tracks[0]["tracks"][0]

zero_targets = load_json(tmp / "run-04-zero-targets.json")
assert zero_targets["status"] == "completed"
assert zero_targets["frame_count"] == 1
assert zero_targets["target_count"] == 0
zero_lines = load_jsonl(tmp / "run-04-zero-targets/targets.jsonl")
assert len(zero_lines) == 1
assert zero_lines[0]["targets"] == []
assert (tmp / "run-04-zero-targets/image-meta.jsonl").stat().st_size > 0

assert not (tmp / "run-04-zero-frames/targets.jsonl").exists()
assert not (tmp / "run-04-zero-frames/image-meta.jsonl").exists()

run_05 = load_json(tmp / "run-05.json")
assert run_05["status"] == "completed"
scaled = load_jsonl(tmp / "run-05/targets.jsonl")[0]
assert scaled["targets"][0]["bbox"] == {
    "x_min": 100.0, "y_min": 40.0, "x_max": 140.0, "y_max": 80.0
}
assert scaled["targets"][0]["geolocation_sample"]["line"] == 60.0
assert scaled["targets"][0]["geolocation_sample"]["pixel"] == 120.0
scaled_meta = load_jsonl(tmp / "run-05/image-meta.jsonl")[0]
assert scaled_meta["geometry_id"] == "scale-pad.v1"
assert scaled_meta["filter_width"] == 2176
assert scaled_meta["filter_height"] == 2176
assert scaled_meta["original_width"] == 4096

forbidden_factories = ("Yolov8", "DeepSort", "mps", "lynxi")
for name in ("run-01.json", "run-02.json"):
    result = load_json(tmp / name)
    blob = json.dumps(result)
    for token in forbidden_factories:
        assert token not in blob, f"{name} contains {token}"
    assert "algorithm_validated" not in blob
    assert result["provenance"]["filter_backend"] == "mock"
    assert result["provenance"]["evidence_class"] == "host_mock_filter"
    assert result["provenance"]["filter"]["factory"] in (
        "ImageProcessMockDetector", "ImageProcessMockTracker")
    assert result["provenance"]["filter"]["factory"] not in (
        "Yolov8Detection", "DeepSortTracking")

print("EIP2-M4-01..06 passed")
PY

echo "image-process M4 contract tests passed"
