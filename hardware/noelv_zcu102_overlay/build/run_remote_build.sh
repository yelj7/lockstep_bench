#!/usr/bin/env bash
# /**********************************************************
# * 文件名: run_remote_build.sh
# * 日期: 2026-07-19
# * 版本: 1.1
# * 更新记录: 增加远端身份、摘要格式和删除路径边界门禁。
# * 描述: 仅在 78 服务器调用 Vivado 2022.2 完成非 OOC 顶层实现。
# **********************************************************/

set -euo pipefail

EXPECTED_BASE="/home/ylj/NOVELV_SAMPLE"
EXPECTED_HOST_IP="192.168.31.78"
SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd -P)"
if [[ "$(id -un)" != "ylj" ]] ||
   [[ " $(hostname -I 2>/dev/null) " != *" ${EXPECTED_HOST_IP} "* ]]; then
  echo "BUILD_GATE_ERROR: 仅允许 ylj@${EXPECTED_HOST_IP} 执行" >&2
  exit 2
fi
DIGEST="$(tr -d '\r\n' < "${SCRIPT_DIR}/../manifests/source_digest.txt")"
if [[ ! "${DIGEST}" =~ ^[0-9a-f]{64}$ ]]; then
  echo "BUILD_GATE_ERROR: source digest 必须为 64 位小写十六进制" >&2
  exit 2
fi
EXPECTED_ROOT="${EXPECTED_BASE}/${DIGEST}"
ROOT="$(realpath -m -- "${EXPECTED_ROOT}")"
if [[ "${ROOT}" != "${EXPECTED_ROOT}" || "${SCRIPT_DIR}" != "${ROOT}/overlay/build" ]]; then
  echo "BUILD_GATE_ERROR: 远端根路径越界 ${ROOT}" >&2
  exit 2
fi
ARTIFACTS="${ROOT}/artifacts"
GENERATED_PROJECT="${ROOT}/source/designs/noelv-xilinx-zcu102/vivado/noelv-xilinx-zcu102"
BUILD="${ROOT}/overlay/build"
export XILINXD_LICENSE_FILE="/tools/Vivado_license_2037.lic"
export LM_LICENSE_FILE="/tools/Vivado_license_2037.lic"

if ! grep -q "\"source_digest\"[[:space:]]*:[[:space:]]*\"${DIGEST}\"" "${ROOT}/input/simulation_gate.json" ||
   ! grep -q '"status"[[:space:]]*:[[:space:]]*"pass"' "${ROOT}/input/simulation_gate.json"; then
  echo "BUILD_GATE_ERROR: XSim gate 缺失或摘要不一致" >&2
  exit 2
fi

mkdir -p "${ARTIFACTS}/archive"
if [[ -f "${ARTIFACTS}/build.log" ]]; then
  archive_stamp="$(date -r "${ARTIFACTS}/build.log" +%Y%m%dT%H%M%S)"
  mv "${ARTIFACTS}/build.log" "${ARTIFACTS}/archive/build.failed.${archive_stamp}.log"
fi
rm -rf -- "${GENERATED_PROJECT}" "${ROOT}/.Xil" "${ROOT}/.cache" "${ROOT}/.tmpCRC" \
  "${ARTIFACTS}/common" "${ARTIFACTS}/debug" "${ARTIFACTS}/release"
rm -f -- "${ARTIFACTS}/SHA256SUMS"

printf '%s\n' "$$" > "${ROOT}/build.pid"
trap 'rm -f -- "${ROOT}/build.pid"' EXIT
(
  cd "${ROOT}/source"
  find . -type f -print0 | sort -z | xargs -0 sha256sum
) > "${ARTIFACTS}/source_manifest.sha256"
sha256sum "${BUILD}/build_remote.tcl" "${BUILD}/fixed_probes.xdc" \
  "${BUILD}/run_remote_build.sh" "${ROOT}/input/simulation_gate.json" \
  > "${ARTIFACTS}/build_inputs.sha256"

set +e
/tools/Xilinx/Vivado/2022.2/bin/vivado -mode batch -nojournal -nolog \
  -source "${BUILD}/build_remote.tcl" \
  -tclargs "${ROOT}/source" "${ARTIFACTS}" "${DIGEST}" \
  2>&1 | tee "${ARTIFACTS}/build.log"
vivado_status="${PIPESTATUS[0]}"
set -e
if [[ "${vivado_status}" -ne 0 ]]; then
  echo "BUILD_GATE_ERROR: Vivado 返回 ${vivado_status}" >&2
  exit "${vivado_status}"
fi

(
  cd "${ARTIFACTS}"
  find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
  sha256sum -c SHA256SUMS
)
