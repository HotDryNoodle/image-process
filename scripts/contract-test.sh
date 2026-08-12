#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${1:-${repo_dir}/build/image-process}"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/image-process-contract.XXXXXX")"
trap 'rm -rf "${test_dir}"' EXIT

export IMAGE_PROCESS_SOURCE_ROOT="${repo_dir}"

"${binary}" manifest --output json >"${test_dir}/manifest.json"
python3 - "${repo_dir}" "${test_dir}" <<'PY'
import json
import pathlib
import sys

repo = pathlib.Path(sys.argv[1])
tmp = pathlib.Path(sys.argv[2])
manifest = json.loads((tmp / "manifest.json").read_text())
disk = json.loads((repo / "configs/plugins/image-process.json").read_text())
assert manifest == disk

try:
    import jsonschema
except ImportError:
    jsonschema = None
if jsonschema is not None:
    sdk_schema_paths = list((repo / "subprojects").glob("*/schemas/plugin_manifest.1.1.schema.json"))
    sdk_schema_paths += list((repo / "subprojects").glob("*/sdk/schemas/plugin_manifest.1.1.schema.json"))
    assert sdk_schema_paths, "plugin SDK schema not found under resolved subprojects"
    sdk_schema = json.loads(sdk_schema_paths[0].read_text())
    jsonschema.validate(manifest, sdk_schema)
PY

"${binary}" validate \
  --input "${repo_dir}/samples/pushbroom_fixture.json" \
  --output json >"${test_dir}/validate.json"

"${binary}" run \
  --input "${repo_dir}/samples/pushbroom_fixture.json" \
  --work-dir "${test_dir}/dry-run" \
  --dry-run --output json >"${test_dir}/dry-run.json"

"${binary}" run \
  --input "${repo_dir}/samples/pushbroom_fixture.json" \
  --work-dir "${test_dir}/run-a" \
  --output json >"${test_dir}/run-a.json"
"${binary}" run \
  --input "${repo_dir}/samples/pushbroom_fixture.json" \
  --work-dir "${test_dir}/run-b" \
  --output json >"${test_dir}/run-b.json"

python3 - "${repo_dir}" "${test_dir}" <<'PY'
import hashlib
import json
import pathlib
import tarfile
import sys

repo = pathlib.Path(sys.argv[1])
tmp = pathlib.Path(sys.argv[2])
validated = json.loads((tmp / "validate.json").read_text())
assert validated["ok"] is True
assert validated["details"]["filter_role"] == "detection_fixture"

dry = json.loads((tmp / "dry-run.json").read_text())
assert dry["status"] == "dry_run"
assert "detection" in dry["pipeline_plan"]["filter"]["role"]
assert "tracking" not in dry["pipeline_plan"]["filter"]["role"]
assert not (tmp / "dry-run/product.bin").exists()

run_a = json.loads((tmp / "run-a.json").read_text())
run_b = json.loads((tmp / "run-b.json").read_text())
assert run_a["status"] == "completed"
assert run_a["frame_count"] == 2
assert run_a["provenance"]["source"]["factory"] == "videotestsrc"
assert run_a["provenance"]["filter"]["factory"] == "identity"
assert run_a["artifact"]["sha256"] == run_b["artifact"]["sha256"]
assert not (tmp / "run-a/product.bin.partial").exists()

product = tmp / "run-a" / run_a["artifact"]["path"]
assert hashlib.sha256(product.read_bytes()).hexdigest() == run_a["artifact"]["sha256"]
with tarfile.open(product) as archive:
    names = archive.getnames()
    assert names == sorted(names)
    assert names == [
        "frames/000000.bin",
        "frames/000001.bin",
        "manifest.json",
        "meta/frames.jsonl",
    ]
    bundle = json.load(archive.extractfile("manifest.json"))
    assert bundle["frame_count"] == 2
    assert bundle["pipeline_plan"]["sink"]["max_buffers"] == 2

try:
    import jsonschema
except ImportError:
    jsonschema = None
if jsonschema is not None:
    output_schema = json.loads((repo / "schemas/image_process.output.schema.json").read_text())
    jsonschema.validate(run_a, output_schema)
PY

python3 - "${repo_dir}/samples/pushbroom_fixture.json" "${test_dir}/raw-pipeline.json" <<'PY'
import json
import pathlib
import sys

request = json.loads(pathlib.Path(sys.argv[1]).read_text())
request["pipeline"] = "videotestsrc ! fakesink"
pathlib.Path(sys.argv[2]).write_text(json.dumps(request))
PY
set +e
"${binary}" validate --input "${test_dir}/raw-pipeline.json" \
  >"${test_dir}/raw-pipeline.out"
raw_pipeline_status=$?
set -e
test "${raw_pipeline_status}" -eq 2

python3 - "${repo_dir}/samples/pushbroom_fixture.json" "${test_dir}/wrong-mode.json" <<'PY'
import json
import pathlib
import sys

request = json.loads(pathlib.Path(sys.argv[1]).read_text())
request["acquisition_mode"] = "stare"
pathlib.Path(sys.argv[2]).write_text(json.dumps(request))
PY
set +e
"${binary}" validate --input "${test_dir}/wrong-mode.json" \
  >"${test_dir}/wrong-mode.out"
wrong_mode_status=$?
set -e
test "${wrong_mode_status}" -eq 2

set +e
"${binary}" run --input "${repo_dir}/samples/stare_cdg00.json" \
  --work-dir "${test_dir}/msf-preflight" --dry-run \
  >"${test_dir}/msf-preflight.out"
msf_status=$?
set -e
test "${msf_status}" -eq 3

python3 - "${repo_dir}/samples/cdg00_parse_host.json" "${test_dir}/cdg00-missing-digest.json" <<'PY'
import json
import pathlib
import sys

request = json.loads(pathlib.Path(sys.argv[1]).read_text())
request["input"].pop("sha256")
pathlib.Path(sys.argv[2]).write_text(json.dumps(request))
PY
set +e
"${binary}" validate --input "${test_dir}/cdg00-missing-digest.json" \
  >"${test_dir}/cdg00-missing-digest.out"
cdg00_missing_digest_status=$?
set -e
test "${cdg00_missing_digest_status}" -eq 2

echo "image-process contract tests passed"
