#!/usr/bin/env bash
# /**********************************************************
# * 文件名: audit-linux-runtime.sh
# * 日期: 2026-07-13
# * 版本: v1.0
# * 更新记录: 初版创建 Linux 安装树依赖审计脚本
# * 描述: 阻止错误架构、缺失依赖和构建机绝对路径进入离线包
# **********************************************************/

set -euo pipefail

[[ $# -eq 1 ]] || { echo "用法: $0 <安装树根目录>" >&2; exit 2; }
install_tree=$1
app_root="${install_tree}/opt/lockstep-host"
[[ $(uname -m) == x86_64 ]] || { echo "仅允许在 x86_64 主机审计" >&2; exit 1; }
command -v lddtree >/dev/null 2>&1 || { echo "缺少 lddtree（pax-utils）" >&2; exit 1; }
[[ -x "${app_root}/bin/lockstep_host" ]] || { echo "主程序不存在" >&2; exit 1; }
[[ -x "${app_root}/bin/lockstep_debug_service" ]] || { echo "调试服务不存在" >&2; exit 1; }
[[ -f "${app_root}/plugins/platforms/libqxcb.so" ]] || { echo "Qt xcb 插件不存在" >&2; exit 1; }
[[ -f "${app_root}/resources/manifest.json" ]] || { echo "资源清单不存在" >&2; exit 1; }

audit_failed=0
while IFS= read -r -d '' candidate; do
    description=$(file -b "${candidate}")
    [[ ${description} == *ELF* ]] || continue
    if [[ ${description} != *x86-64* ]]; then
        echo "ELF 架构错误: ${candidate}: ${description}" >&2
        audit_failed=1
    fi
    dependencies=$(ldd "${candidate}" 2>&1 || true)
    if grep -q "not found" <<<"${dependencies}"; then
        echo "存在缺失依赖: ${candidate}" >&2
        echo "${dependencies}" >&2
        audit_failed=1
    fi
    if grep -E -q '/home/|/tmp/|/build/|C:/' <<<"${dependencies}"; then
        echo "依赖误引用构建路径: ${candidate}" >&2
        audit_failed=1
    fi
    lddtree_output=$(lddtree "${candidate}" 2>&1 || true)
    if grep -q "not found" <<<"${lddtree_output}"; then
        echo "lddtree 发现缺失依赖: ${candidate}" >&2
        echo "${lddtree_output}" >&2
        audit_failed=1
    fi
    if readelf -d "${candidate}" 2>/dev/null |
        grep -E 'RPATH|RUNPATH' |
        grep -E -q '/home/|/tmp/|/build/|C:/'; then
        echo "ELF RPATH 误引用构建路径: ${candidate}" >&2
        audit_failed=1
    fi
done < <(find "${app_root}" -type f -print0)

grep -q '"id": "debug.self_service.executable"' "${app_root}/resources/manifest.json" || {
    echo "资源清单缺少调试服务" >&2
    audit_failed=1
}
service_status=$(awk '
    /"id": "debug.self_service.executable"/ { in_service = 1 }
    in_service && /"status":/ {
        gsub(/.*"status": "/, ""); gsub(/".*/, ""); print; exit
    }
' "${app_root}/resources/manifest.json")
service_version=$(awk '
    /"id": "debug.self_service.executable"/ { in_service = 1 }
    in_service && /"version":/ {
        gsub(/.*"version": "/, ""); gsub(/".*/, ""); print; exit
    }
' "${app_root}/resources/manifest.json")
if [[ ${service_status} != enabled || -z "${service_version}" || ${service_version} == 0.0.0.0 ]]; then
    echo "调试服务资源项未启用或版本无效" >&2
    audit_failed=1
fi
expected_service_sha=$(awk '
    /"id": "debug.self_service.executable"/ { in_service = 1 }
    in_service && /"sha256":/ {
        gsub(/.*"sha256": "/, ""); gsub(/".*/, ""); print; exit
    }
' "${app_root}/resources/manifest.json")
actual_service_sha=$(sha256sum \
    "${app_root}/resources/debug_adapters/self_debug_service/lockstep_debug_service" |
    awk '{print $1}')
if [[ -z "${expected_service_sha}" || ${expected_service_sha} != "${actual_service_sha}" ]]; then
    echo "资源清单中的调试服务摘要不匹配" >&2
    audit_failed=1
fi

exit "${audit_failed}"
