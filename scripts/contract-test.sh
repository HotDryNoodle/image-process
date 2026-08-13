#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${1:-${repo_dir}/build/image-process}"
binary_dir="$(cd "$(dirname "${binary}")" && pwd)"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/image-process-contract.XXXXXX")"
trap 'rm -rf "${test_dir}"' EXIT

export IMAGE_PROCESS_SOURCE_ROOT="${repo_dir}"

"${binary}" manifest --output json >"${test_dir}/manifest.json"
python3 - "${repo_dir}" "${test_dir}" "${binary_dir}" <<'PY'
import json
import pathlib
import subprocess
import sys
from copy import deepcopy

repo = pathlib.Path(sys.argv[1])
tmp = pathlib.Path(sys.argv[2])
build = pathlib.Path(sys.argv[3])
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
    provenance = json.loads((repo / "SOURCE_PROVENANCE.json").read_text())
    provenance_schema = json.loads(
        (repo / "schemas/source_provenance.schema.json").read_text()
    )
    jsonschema.validate(provenance, provenance_schema)
    assert provenance["reviewed_revision"] == \
        "c4046d66a60da97cf4b28d20b9cedd9a9bffb254"
    assert {item["license_status"] for item in provenance["excluded"]} >= {
        "explicit_lgpl", "explicit_gpl", "unreviewed"
    }
    history = provenance["history_import"]
    assert history is not None
    imported_paths = subprocess.check_output(
        [
            "git", "-C", str(repo), "ls-tree", "-r", "--name-only",
            history["filtered_tip"],
        ],
        text=True,
    ).splitlines()
    assert imported_paths == history["approved_paths"]
    merge_parents = subprocess.check_output(
        ["git", "-C", str(repo), "show", "-s", "--format=%P", history["merge_commit"]],
        text=True,
    ).split()
    assert len(merge_parents) == 2
    assert merge_parents[1] == history["filtered_tip"]
    assert len(history["commit_map"]) == 12
    assert len({item["original_commit"] for item in history["commit_map"]}) == 12
    assert len({item["imported_commit"] for item in history["commit_map"]}) == 12
    for item in history["commit_map"]:
        subprocess.run(
            ["git", "-C", str(repo), "cat-file", "-e", item["imported_commit"] + "^{commit}"],
            check=True,
        )
    for entry in provenance["entries"]:
        assert (repo / entry["target_path"]).is_file(), entry["target_path"]
        assert all(path.startswith("source/") for path in entry["origin_paths"])
        assert all(not any(token in path for token in ("*", "{", "}"))
                   for path in entry["origin_paths"])
    invalid_preserved = deepcopy(provenance)
    invalid_preserved["history_status"] = "preserved_bounded_import"
    next(
        entry for entry in invalid_preserved["entries"]
        if entry["treatment"] == "refactored"
    )["import_commit"] = None
    try:
        jsonschema.validate(invalid_preserved, provenance_schema)
    except jsonschema.ValidationError:
        pass
    else:
        raise AssertionError("preserved history accepted without import commit")
    invalid_pending = deepcopy(provenance)
    invalid_pending["history_status"] = "pending_commit_gate"
    invalid_pending["history_import"] = None
    next(
        entry for entry in invalid_pending["entries"]
        if entry["treatment"] == "refactored"
    )["import_commit"] = "0" * 40
    try:
        jsonschema.validate(invalid_pending, provenance_schema)
    except jsonschema.ValidationError:
        pass
    else:
        raise AssertionError("pending history accepted an import commit")
    runtime_manifest = json.loads(
        (build / "image-process-runtime-manifest.json").read_text()
    )
    runtime_schema = json.loads(
        (repo / "schemas/runtime_manifest.schema.json").read_text()
    )
    jsonschema.validate(runtime_manifest, runtime_schema)

collector = (repo / "src/pipeline.cpp").read_text()
assert "MsfGenericMetaLayout" not in collector
assert "Cdg00ParameterLayout" not in collector
assert "msf.cdg00.native-v1-host-adapter" not in collector
assert "reinterpret_cast" not in collector
assert "ip_buffer_get_cdg00_meta" in collector
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
assert run_a["provenance"]["runtime"]["meta_abi"] == "image-process.gst-meta.v1"
assert run_a["provenance"]["runtime"]["component"] == "image-process"
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
