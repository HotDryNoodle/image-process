#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${1:-${repo_dir}/build/image-process}"
work_dir="${2:-${repo_dir}/data/lynxi_models/model_yolov8n_onnx/hawaii-run}"
sample="${repo_dir}/samples/npu_hawaii_pushbroom.json"
fixture="${repo_dir}/data/Hawaii_A2.dat"
binary_dir="$(cd "$(dirname "${binary}")" && pwd)"

if [[ ! -x "${binary}" ]]; then
  echo "missing image-process binary: ${binary}" >&2
  exit 1
fi
if [[ ! -f "${fixture}" ]]; then
  echo "missing CDG00 fixture: ${fixture}" >&2
  exit 1
fi
if [[ ! -d "${repo_dir}/data/lynxi_models/model_yolov8n_onnx/Net_0" ]]; then
  echo "missing Lynxi model dir: ${repo_dir}/data/lynxi_models/model_yolov8n_onnx/Net_0" >&2
  exit 1
fi

export IMAGE_PROCESS_SOURCE_ROOT="${repo_dir}"
export IMAGE_PROCESS_GST_PLUGIN_PATH="${IMAGE_PROCESS_GST_PLUGIN_PATH:-${binary_dir}}"
export GST_PLUGIN_PATH="${IMAGE_PROCESS_GST_PLUGIN_PATH}${GST_PLUGIN_PATH:+:${GST_PLUGIN_PATH}}"

if ! gst-inspect-1.0 ImageProcessLynxiDetector >/dev/null 2>&1; then
  echo "ImageProcessLynxiDetector is not installed in IMAGE_PROCESS_GST_PLUGIN_PATH=${IMAGE_PROCESS_GST_PLUGIN_PATH}" >&2
  echo "Build with: meson setup build -Dlynxi=enabled -Dlynxi_sdk_prefix=<prefix>" >&2
  exit 1
fi

mkdir -p "${work_dir}"
echo "running npu.sat-c.pushbroom.v1" >&2
echo "  input  ${fixture}" >&2
echo "  work   ${work_dir}" >&2
echo "  crops  ${work_dir}/crops/<class>/*.tif" >&2

"${binary}" run --input "${sample}" --work-dir "${work_dir}" --output json
echo "inspect ${work_dir}/targets.jsonl ${work_dir}/image-meta.jsonl ${work_dir}/crops" >&2
