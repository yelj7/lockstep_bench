#!/usr/bin/env bash
# /**********************************************************
# * 文件名: prepare-kylin-build-env-bundle.sh
# * 日期: 2026-07-13
# * 版本: v1.1
# * 更新记录: 增加 DEB 架构/版本锁定、依赖预演和原子输出
# * 描述: 校验 DEB 根包并生成本地仓库、部署脚本和完整性清单
# **********************************************************/

set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_dir=$(CDPATH= cd -- "${script_dir}/.." && pwd)
requirements_file="${source_dir}/packaging/kylin-env/build-requirements.psv"
deb_dir=
output_dir=

while (( $# > 0 )); do
    case "$1" in
        --requirements)
            [[ $# -ge 2 && -n $2 ]] || { echo "--requirements 缺少路径" >&2; exit 2; }
            requirements_file=$2
            shift 2
            ;;
        --deb-dir)
            [[ $# -ge 2 && -n $2 ]] || { echo "--deb-dir 缺少路径" >&2; exit 2; }
            deb_dir=$2
            shift 2
            ;;
        --output)
            [[ $# -ge 2 && -n $2 ]] || { echo "--output 缺少路径" >&2; exit 2; }
            output_dir=$2
            shift 2
            ;;
        *) echo "未知参数: $1" >&2; exit 2 ;;
    esac
done

[[ -n "${deb_dir}" && -n "${output_dir}" ]] || {
    echo "用法: $0 --deb-dir <deb-directory> --output <bundle-directory> [--requirements <file>]" >&2
    exit 2
}

LOCKSTEP_SKIP_PLATFORM_CHECK=${LOCKSTEP_SKIP_PLATFORM_CHECK:-0} \
    "${script_dir}/check-kylin-build-env.sh" --platform-only >/dev/null

for command_name in apt-get dpkg dpkg-deb dpkg-scanpackages gzip sha256sum realpath; do
    command -v "${command_name}" >/dev/null 2>&1 || {
        echo "缺少套件制作工具: ${command_name}" >&2
        exit 2
    }
done

requirements_file=$(realpath "${requirements_file}")
deb_dir=$(realpath "${deb_dir}")
output_dir=$(realpath -m "${output_dir}")
[[ -r "${requirements_file}" && -d "${deb_dir}" ]] || {
    echo "需求清单或 DEB 输入目录无效" >&2
    exit 2
}
[[ ! -e "${output_dir}" ]] || {
    echo "输出目录已存在，拒绝覆盖: ${output_dir}" >&2
    exit 2
}
[[ "${output_dir}" != / && "${output_dir}" != "${deb_dir}"/* ]] || {
    echo "输出目录位置不安全: ${output_dir}" >&2
    exit 2
}

output_parent=$(dirname "${output_dir}")
output_name=$(basename "${output_dir}")
mkdir -p "${output_parent}"
temporary_output=$(mktemp -d "${output_parent}/.${output_name}.tmp.XXXXXX")
trap 'rm -rf "${temporary_output}"' EXIT

mapfile -d '' -t deb_files < <(find "${deb_dir}" -maxdepth 1 -type f -name '*.deb' -print0 | sort -z)
(( ${#deb_files[@]} > 0 )) || { echo "DEB 输入目录为空" >&2; exit 2; }

declare -A provided_packages=()
declare -A provided_versions=()
declare -A provided_architectures=()
declare -A provided_hashes=()
for deb_file in "${deb_files[@]}"; do
    package_name=$(dpkg-deb -f "${deb_file}" Package 2>/dev/null)
    package_version=$(dpkg-deb -f "${deb_file}" Version 2>/dev/null)
    package_architecture=$(dpkg-deb -f "${deb_file}" Architecture 2>/dev/null)
    [[ -n "${package_name}" && -n "${package_version}" && -n "${package_architecture}" ]] || {
        echo "无法读取 DEB 元数据: ${deb_file}" >&2
        exit 2
    }
    [[ ${package_architecture} == amd64 || ${package_architecture} == all ]] || {
        echo "DEB 架构不受支持: ${package_name} ${package_architecture}" >&2
        exit 1
    }
    [[ -z ${provided_packages[${package_name}]:-} ]] || {
        echo "同名 DEB 存在多个版本: ${package_name}" >&2
        exit 2
    }
    provided_packages[${package_name}]=${deb_file}
    provided_versions[${package_name}]=${package_version}
    provided_architectures[${package_name}]=${package_architecture}
    provided_hashes[${package_name}]=$(sha256sum "${deb_file}" | awk '{ print $1 }')
done

declare -A required_packages=()
while IFS='|' read -r requirement_id probe expected package_list description; do
    [[ -n "${requirement_id}" && ${requirement_id} != \#* ]] || continue
    IFS=';' read -r -a current_packages <<<"${package_list}"
    for package_name in "${current_packages[@]}"; do
        [[ -z "${package_name}" ]] || required_packages[${package_name}]=1
    done
done <"${requirements_file}"

for package_name in "${!required_packages[@]}"; do
    [[ -n ${provided_packages[${package_name}]:-} ]] || {
        echo "离线 DEB 缺少根包: ${package_name}" >&2
        exit 1
    }
done
mapfile -t root_packages < <(printf '%s\n' "${!required_packages[@]}" | sort)

package_version_matches()
{
    local actual=${1#*:} expected=$2 minimum prefix
    if [[ ${expected} == '>='* ]]; then
        minimum=${expected#'>='}
        dpkg --compare-versions "${actual}" ge "${minimum}"
    elif [[ ${expected: -2} == '.*' ]]; then
        prefix=${expected:0:${#expected}-2}
        [[ ${actual} == "${prefix}".* ]]
    else
        [[ ${actual} == "${expected}" || ${actual} == "${expected}"+* ||
           ${actual} == "${expected}"-* || ${actual} == "${expected}"~* ]]
    fi
}

while IFS='|' read -r requirement_id probe expected package_list description; do
    [[ -n "${requirement_id}" && ${requirement_id} != \#* ]] || continue
    case "${probe}" in
        cmake|qt|pkg-config:*) ;;
        *) continue ;;
    esac
    IFS=';' read -r -a current_packages <<<"${package_list}"
    for package_name in "${current_packages[@]}"; do
        [[ -n "${package_name}" ]] || continue
        package_version=${provided_versions[${package_name}]}
        package_version_matches "${package_version}" "${expected}" || {
            echo "DEB 版本不满足 ${requirement_id}: ${package_name} ${package_version}，需要 ${expected}" >&2
            exit 1
        }
    done
done <"${requirements_file}"

mkdir -p "${temporary_output}/repo"
cp "${deb_files[@]}" "${temporary_output}/repo/"
cp "${requirements_file}" "${temporary_output}/build-requirements.psv"
cp "${script_dir}/check-kylin-build-env.sh" "${temporary_output}/"
cp "${script_dir}/bootstrap-kylin-build-env.sh" "${temporary_output}/"
if [[ -f "${source_dir}/docs/银河麒麟离线构建环境说明.md" ]]; then
    cp "${source_dir}/docs/银河麒麟离线构建环境说明.md" "${temporary_output}/使用说明.md"
fi
chmod +x "${temporary_output}/check-kylin-build-env.sh" "${temporary_output}/bootstrap-kylin-build-env.sh"

{
    echo '# package|version|architecture|sha256|filename'
    while IFS= read -r package_name; do
        printf '%s|%s|%s|%s|%s\n' \
            "${package_name}" \
            "${provided_versions[${package_name}]}" \
            "${provided_architectures[${package_name}]}" \
            "${provided_hashes[${package_name}]}" \
            "$(basename "${provided_packages[${package_name}]}")"
    done < <(printf '%s\n' "${!provided_packages[@]}" | sort)
} >"${temporary_output}/deb-packages.psv"

(
    cd "${temporary_output}/repo"
    dpkg-scanpackages . /dev/null | gzip -9c >Packages.gz
)

apt_state="${temporary_output}/.apt-state"
mkdir -p "${apt_state}/lists/partial" "${apt_state}/cache/archives/partial"
touch "${apt_state}/status"
printf 'deb [trusted=yes] file:%s ./\n' "${temporary_output}/repo" >"${apt_state}/sources.list"
apt_options=(
    -o "Dir::Etc::sourcelist=${apt_state}/sources.list"
    -o "Dir::Etc::sourceparts=-"
    -o "Dir::State::lists=${apt_state}/lists"
    -o "Dir::State::status=${apt_state}/status"
    -o "Dir::Cache=${apt_state}/cache"
    -o "APT::Get::List-Cleanup=0"
    -o "Acquire::Languages=none"
)
apt-get "${apt_options[@]}" update
apt-get "${apt_options[@]}" -s --reinstall --no-install-recommends install "${root_packages[@]}"
rm -rf "${apt_state}"

(
    cd "${temporary_output}"
    find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum >SHA256SUMS
)

mv "${temporary_output}" "${output_dir}"
trap - EXIT

echo "银河麒麟离线构建环境套件已生成: ${output_dir}"
