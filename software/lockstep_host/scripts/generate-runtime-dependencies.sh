#!/usr/bin/env bash
# /**********************************************************
# * 文件名: generate-runtime-dependencies.sh
# * 日期: 2026-07-13
# * 版本: v1.0
# * 更新记录: 初版创建运行时依赖清单生成脚本
# * 描述: 记录安装树 ELF 及其系统依赖的架构、版本、摘要、许可证和归属
# **********************************************************/

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "用法: $0 <安装树根目录> <输出 JSON>" >&2
    exit 2
fi

install_tree=$1
output_file=$2
app_root="${install_tree}/opt/lockstep-host"
private_lib="${app_root}/lib"

for command_name in file readelf ldd sha256sum dpkg-query; do
    command -v "${command_name}" >/dev/null 2>&1 || {
        echo "缺少依赖清单工具: ${command_name}" >&2
        exit 1
    }
done

[[ -x "${app_root}/bin/lockstep_ui_preview" ]] || {
    echo "安装树缺少 lockstep_ui_preview" >&2
    exit 1
}

mkdir -p "$(dirname "${output_file}")"
temporary_entries=$(mktemp)
trap 'rm -f "${temporary_entries}"' EXIT

json_escape()
{
    sed 's/\\/\\\\/g; s/"/\\"/g; s/\t/\\t/g'
}

append_entry()
{
    local path=$1
    local architecture=$2
    local soname=$3
    local version=$4
    local sha256=$5
    local license=$6
    local classification=$7
    local package_name=$8

    if [[ -s "${temporary_entries}" ]]; then
        printf ',\n' >>"${temporary_entries}"
    fi
    printf '    {"path":"%s","architecture":"%s","soname":"%s","source_version":"%s","sha256":"%s","license":"%s","classification":"%s","source_package":"%s"}' \
        "$(printf '%s' "${path}" | json_escape)" \
        "$(printf '%s' "${architecture}" | json_escape)" \
        "$(printf '%s' "${soname}" | json_escape)" \
        "$(printf '%s' "${version}" | json_escape)" \
        "${sha256}" \
        "$(printf '%s' "${license}" | json_escape)" \
        "${classification}" \
        "$(printf '%s' "${package_name}" | json_escape)" >>"${temporary_entries}"
}

while IFS= read -r -d '' candidate; do
    file_description=$(file -b "${candidate}")
    [[ ${file_description} == *ELF* ]] || continue
    relative_path="/${candidate#"${install_tree}/"}"
    soname=$(readelf -d "${candidate}" 2>/dev/null | sed -n 's/.*(SONAME).*\[\(.*\)\].*/\1/p' | head -n 1)
    base_name=$(basename "${candidate}")
    source_version="${LOCKSTEP_BUILD_VERSION:-unknown}"
    license="项目自有代码"
    source_package="lockstep-host"
    if [[ ${candidate} == "${app_root}/plugins/"* ]]; then
        source_version="${LOCKSTEP_QT_VERSION:-5.15.2}"
        license="LGPL-3.0-only OR GPL-2.0-or-later"
        source_package="Qt 5 plugin payload"
    else
        case "${base_name}" in
        libQt5*)
            source_version="${LOCKSTEP_QT_VERSION:-5.15.2}"
            license="LGPL-3.0-only OR GPL-2.0-or-later"
            source_package="qtbase5/qtserialport5"
            ;;
        libhidapi*)
            source_version="${LOCKSTEP_HIDAPI_VERSION:-0.14.x}"
            license="BSD-3-Clause OR GPL-3.0-only"
            source_package="hidapi"
            ;;
        libstdc++*|libgcc_s*)
            source_version="$(c++ -dumpfullversion -dumpversion 2>/dev/null || echo unknown)"
            license="GPL-3.0-or-later WITH GCC-exception-3.1"
            source_package="gcc"
            ;;
        esac
    fi
    append_entry \
        "${relative_path}" "${file_description}" "${soname}" "${source_version}" \
        "$(sha256sum "${candidate}" | awk '{print $1}')" "${license}" "bundled" "${source_package}"
done < <(find "${app_root}" -type f -print0 | sort -z)

system_paths=$(mktemp)
trap 'rm -f "${temporary_entries}" "${system_paths}"' EXIT
while IFS= read -r -d '' candidate; do
    file -b "${candidate}" | grep -q ELF || continue
    ldd "${candidate}" 2>/dev/null |
        awk '/=> \/|^\// { if ($2 == "=>") print $3; else print $1 }' >>"${system_paths}" || true
done < <(find "${app_root}/bin" "${app_root}/plugins" -type f -print0)

while IFS= read -r dependency_path; do
    [[ -f "${dependency_path}" ]] || continue
    case "${dependency_path}" in
        "${private_lib}"/*) continue ;;
    esac
    package_name=$(dpkg-query -S "${dependency_path}" 2>/dev/null | head -n 1 | cut -d: -f1 || true)
    if [[ -z "${package_name}" ]]; then
        package_name=$(dpkg-query -S "$(readlink -f "${dependency_path}")" 2>/dev/null |
            head -n 1 | cut -d: -f1 || true)
    fi
    [[ -n "${package_name}" ]] || {
        echo "无法确定系统依赖的来源软件包: ${dependency_path}" >&2
        exit 1
    }
    package_version=$(dpkg-query -W -f='${Version}' "${package_name}" 2>/dev/null) || {
        echo "无法确定系统依赖版本: ${package_name}" >&2
        exit 1
    }
    append_entry \
        "${dependency_path}" "$(file -b "${dependency_path}")" \
        "$(readelf -d "${dependency_path}" 2>/dev/null | sed -n 's/.*(SONAME).*\[\(.*\)\].*/\1/p' | head -n 1)" \
        "${package_version}" "$(sha256sum "${dependency_path}" | awk '{print $1}')" \
        "银河麒麟系统软件包许可证" "system-provided" "${package_name}"
done < <(sort -u "${system_paths}")

cat >"${output_file}" <<EOF
{
  "schema_version": "1.0",
  "target": "Desktop-V10-SP1-General-Release x86_64",
  "generated_at_utc": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "entries": [
$(cat "${temporary_entries}")
  ]
}
EOF
