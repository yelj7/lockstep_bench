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
[[ -x "${app_root}/bin/lockstep_ui_preview" ]] || { echo "主程序不存在" >&2; exit 1; }
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

if grep -R -q --include='*.json' 'targetDebugToolPath' "${app_root}/resources"; then
    echo "资源清单仍引用已删除的独立调试服务" >&2
    audit_failed=1
fi

exit "${audit_failed}"
