#!/usr/bin/env bash
# /**********************************************************
# * 文件名: run_remote_sim.sh
# * 日期: 2026-07-19
# * 版本: 1.2
# * 更新记录: 门禁扩展为十三项，增加跨采集全局时间戳回归。
# * 描述: 对统一 overlay 运行 XSim，失败时不生成 pass gate。
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
SRC="${ROOT}/overlay/rtl/lockstep_capture"
OUT="${ROOT}/artifacts/simulation"
INPUT="${ROOT}/input"
XVLOG="/tools/Xilinx/Vivado/2022.2/bin/xvlog"
XELAB="/tools/Xilinx/Vivado/2022.2/bin/xelab"
XSIM="/tools/Xilinx/Vivado/2022.2/bin/xsim"

mapfile -t SOURCES < <(grep -v '^[[:space:]]*#' "${SRC}/lockstep_capture_sources.lst" | grep -v '^[[:space:]]*$' | sed "s#^#${SRC}/#")
TESTS=(
  tb_lockstep_capture_arm_delay
  tb_lockstep_capture_recovery
  tb_lockstep_command_responses
  tb_lockstep_ft601_hello
  tb_lockstep_protocol_probe_real_only
  tb_lockstep_event_capture_core
  tb_lockstep_event_async_fifo
  tb_lockstep_event_capture_controller
  tb_lockstep_protocol_event_encoder
  tb_lockstep_event_frame_source
  tb_lockstep_event_config_parser
  tb_lockstep_capture_stream_arbiter
  tb_lockstep_global_timestamp
)

rm -rf -- "${OUT}"
mkdir -p "${OUT}" "${INPUT}"
cd "${OUT}"
TEST_FILES=()
for top in "${TESTS[@]}"; do
  TEST_FILES+=("${SRC}/tests/${top}.v")
done

"${XVLOG}" --include "${SRC}/include" "${SOURCES[@]}" "${TEST_FILES[@]}" > xvlog.log 2>&1
for top in "${TESTS[@]}"; do
  "${XELAB}" "${top}" -s "${top}" --debug typical > "${top}.xelab.log" 2>&1
  "${XSIM}" "${top}" -runall > "${top}.log" 2>&1
  if grep -q '^FAIL' "${top}.log" || ! grep -q "^PASS ${top}$" "${top}.log"; then
    echo "BUILD_GATE_ERROR: XSim 回归失败 ${top}" >&2
    exit 2
  fi
done

cat > "${ROOT}/input/simulation_gate.json" <<JSON
{
  "schema": "lockstep-simulation-gate-v2",
  "source_digest": "${DIGEST}",
  "vivado_version": "2022.2",
  "status": "pass",
  "test_count": ${#TESTS[@]}
}
JSON
(
  cd "${OUT}"
  find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
  sha256sum -c SHA256SUMS
)
echo "PASS remote XSim gate digest=${DIGEST} tests=${#TESTS[@]}"
