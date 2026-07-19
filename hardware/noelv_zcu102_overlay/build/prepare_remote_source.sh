#!/usr/bin/env bash
# /**********************************************************
# * 文件名: prepare_remote_source.sh
# * 日期: 2026-07-19
# * 版本: 1.1
# * 更新记录: 增加远端身份、摘要格式和删除路径边界门禁。
# * 描述: 校验固定基线和覆盖层摘要，在新摘要目录应用项目维护文件。
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

OLD_DIGEST="4434ee411957b63cf7cfe725ef345b5363016831689b5ed2537a48af2354e968"
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
OLD_ROOT="/home/ylj/NOVELV_SAMPLE/${OLD_DIGEST}"
OVERLAY="${ROOT}/overlay"
SOURCE="${ROOT}/source"
LOCK_FILE="${OVERLAY}/manifests/source_lock.csv"

if [[ ! -d "${OLD_ROOT}/source" || ! -f "${LOCK_FILE}" ]]; then
  echo "BUILD_GATE_ERROR: 基线源码或 source lock 缺失" >&2
  exit 2
fi

rm -rf -- "${SOURCE}"
cp -a "${OLD_ROOT}/source" "${SOURCE}"

while IFS=, read -r ownership relative_path expected_hash; do
  ownership="${ownership//\"/}"
  relative_path="${relative_path//\"/}"
  expected_hash="${expected_hash//\"/}"
  if [[ "${ownership}" == "baseline" ]]; then
    target="${SOURCE}/${relative_path}"
  elif [[ "${ownership}" == "overlay" ]]; then
    target="${OVERLAY}/${relative_path}"
  else
    echo "BUILD_GATE_ERROR: 未知 source lock ownership=${ownership}" >&2
    exit 2
  fi
  if [[ ! -f "${target}" ]]; then
    echo "BUILD_GATE_ERROR: source lock 文件缺失 ${target}" >&2
    exit 2
  fi
  actual_hash="$(sha256sum "${target}" | awk '{print $1}')"
  if [[ "${actual_hash}" != "${expected_hash}" ]]; then
    echo "BUILD_GATE_ERROR: source lock 摘要不一致 ${target}" >&2
    exit 2
  fi
done < <(tail -n +2 "${LOCK_FILE}" | tr -d '\r')

cp -a "${OVERLAY}/rtl/lockstep_capture/." \
  "${SOURCE}/designs/noelv-xilinx-zcu102/rtl/lockstep_capture/"
cp -f "${OVERLAY}/rtl/lockstep_capture/lockstep_capture_sources.lst" \
  "${SOURCE}/designs/noelv-xilinx-zcu102/lockstep_capture_sources.lst"
cp -f "${OVERLAY}/integration/designs/noelv-generic/rtl/core/lockstep_ahb_sample_pack.vhd" \
  "${SOURCE}/designs/noelv-generic/rtl/core/lockstep_ahb_sample_pack.vhd"
cp -f "${OVERLAY}/integration/designs/noelv-xilinx-zcu102/rtl/noelvmp.vhd" \
  "${SOURCE}/designs/noelv-xilinx-zcu102/rtl/noelvmp.vhd"

printf '%s\n' "${DIGEST}" > "${ROOT}/prepared_source_digest.txt"
echo "PASS remote source prepared digest=${DIGEST}"
