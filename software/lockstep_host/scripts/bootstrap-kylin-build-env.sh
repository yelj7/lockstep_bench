#!/usr/bin/env bash
# /**********************************************************
# * 文件名: bootstrap-kylin-build-env.sh
# * 日期: 2026-07-13
# * 版本: v1.1
# * 更新记录: 增加强制完整性校验、最小安装集和安装前预演
# * 描述: 仅从本地 DEB 仓库安装构建依赖，复检后生成环境锁文件
# **********************************************************/

set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_dir=$(CDPATH= cd -- "${script_dir}/.." && pwd)
bundle_root=

while (( $# > 0 )); do
    case "$1" in
        --bundle)
            [[ $# -ge 2 && -n $2 ]] || {
                echo "用法: $0 --bundle <offline-build-env>" >&2
                exit 2
            }
            bundle_root=$2
            shift 2
            ;;
        *) echo "未知参数: $1" >&2; exit 2 ;;
    esac
done

[[ -n "${bundle_root}" ]] || bundle_root=${LOCKSTEP_BUILD_ENV_BUNDLE:-"${script_dir}"}
[[ -d "${bundle_root}" ]] || { echo "离线套件目录不存在: ${bundle_root}" >&2; exit 2; }
bundle_root=$(CDPATH= cd -- "${bundle_root}" && pwd -P)
repo_root="${bundle_root}/repo"
requirements_file="${bundle_root}/build-requirements.psv"
check_script="${bundle_root}/check-kylin-build-env.sh"
lock_file=${LOCKSTEP_BUILD_ENV_LOCK:-"${PWD}/build-environment.lock.json"}

[[ -f "${repo_root}/Packages.gz" ]] || {
    echo "离线套件缺少 repo/Packages.gz" >&2
    exit 2
}
compgen -G "${repo_root}/*.deb" >/dev/null || {
    echo "离线套件 repo/ 中没有 DEB 文件" >&2
    exit 2
}
command -v sha256sum >/dev/null 2>&1 || {
    echo "系统基础环境缺少 sha256sum，无法安全部署" >&2
    exit 2
}
[[ -f "${bundle_root}/SHA256SUMS" ]] || {
    echo "离线套件缺少 SHA256SUMS，拒绝安装" >&2
    exit 2
}
(cd "${bundle_root}" && sha256sum -c SHA256SUMS)

[[ -r "${requirements_file}" ]] ||
    requirements_file="${source_dir}/packaging/kylin-env/build-requirements.psv"
[[ -x "${check_script}" ]] || check_script="${script_dir}/check-kylin-build-env.sh"

[[ -x "${check_script}" && -r "${requirements_file}" ]] || {
    echo "离线套件缺少检查脚本或需求清单" >&2
    exit 2
}

run_check()
{
    LOCKSTEP_BUILD_REQUIREMENTS="${requirements_file}" "${check_script}" "$@"
}

bootstrap_state=$(mktemp -d)
privilege=()
cleanup_bootstrap_state()
{
    [[ -n "${bootstrap_state}" && -d "${bootstrap_state}" ]] || return 0
    rm -rf "${bootstrap_state}" 2>/dev/null || {
        (( ${#privilege[@]} > 0 )) && "${privilege[@]}" rm -rf "${bootstrap_state}"
    }
}
trap cleanup_bootstrap_state EXIT
missing_packages_file="${bootstrap_state}/missing-packages.txt"

if run_check --write-missing-packages "${missing_packages_file}"; then
    run_check --write-lock "${lock_file}" >/dev/null
    echo "构建环境已经满足要求，无需安装"
    exit 0
else
    check_status=$?
    if [[ ${check_status} -ne 1 ]]; then
        echo "平台或需求清单无效，拒绝修改系统" >&2
        exit "${check_status}"
    fi
fi

command -v apt-get >/dev/null 2>&1 || { echo "系统缺少 apt-get" >&2; exit 2; }
if [[ $(id -u) -eq 0 ]]; then
    privilege=()
else
    command -v sudo >/dev/null 2>&1 || { echo "部署需要 root 权限或 sudo" >&2; exit 2; }
    privilege=(sudo)
fi

mapfile -t packages <"${missing_packages_file}"
(( ${#packages[@]} > 0 )) || { echo "检查脚本未输出待安装 DEB 包" >&2; exit 2; }

apt_state="${bootstrap_state}/apt"
mkdir -p "${apt_state}/lists/partial"
printf 'deb [trusted=yes] file:%s ./\n' "${repo_root}" >"${apt_state}/sources.list"
apt_options=(
    -o "Dir::Etc::sourcelist=${apt_state}/sources.list"
    -o "Dir::Etc::sourceparts=-"
    -o "Dir::State::lists=${apt_state}/lists"
    -o "APT::Get::List-Cleanup=0"
    -o "Acquire::Languages=none"
)

"${privilege[@]}" apt-get "${apt_options[@]}" update
"${privilege[@]}" env DEBIAN_FRONTEND=noninteractive \
    apt-get "${apt_options[@]}" -s -y --no-install-recommends install "${packages[@]}"
"${privilege[@]}" env DEBIAN_FRONTEND=noninteractive \
    apt-get "${apt_options[@]}" -y --no-install-recommends install "${packages[@]}"

run_check --write-lock "${lock_file}"
echo "银河麒麟离线构建环境部署完成"
