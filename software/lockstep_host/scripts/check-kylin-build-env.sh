#!/usr/bin/env bash
# /**********************************************************
# * 文件名: check-kylin-build-env.sh
# * 日期: 2026-07-13
# * 版本: v1.1
# * 更新记录: 增加缺失包输出和逐项工具/库环境锁定
# * 描述: 按统一清单检查平台、命令能力和构建依赖版本
# **********************************************************/

set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_dir=$(CDPATH= cd -- "${script_dir}/.." && pwd)
requirements_file=${LOCKSTEP_BUILD_REQUIREMENTS:-"${source_dir}/packaging/kylin-env/build-requirements.psv"}
lock_file=
missing_packages_file=
platform_only=0

while (( $# > 0 )); do
    case "$1" in
        --write-lock)
            [[ $# -ge 2 && -n $2 ]] || {
                echo "用法: $0 [--write-lock <path>]" >&2
                exit 2
            }
            lock_file=$2
            shift 2
            ;;
        --write-missing-packages)
            [[ $# -ge 2 && -n $2 ]] || {
                echo "用法: $0 [--write-lock <path>] [--write-missing-packages <path>]" >&2
                exit 2
            }
            missing_packages_file=$2
            shift 2
            ;;
        --platform-only)
            platform_only=1
            shift
            ;;
        *)
            echo "未知参数: $1" >&2
            exit 2
            ;;
    esac
done

if [[ ${LOCKSTEP_SKIP_PLATFORM_CHECK:-0} != 1 ]]; then
    [[ $(uname -m) == x86_64 ]] || {
        echo "[PLATFORM ERROR] 仅支持 x86_64/amd64" >&2
        exit 2
    }
    [[ -r /etc/os-release ]] || {
        echo "[PLATFORM ERROR] 无法读取 /etc/os-release" >&2
        exit 2
    }
    . /etc/os-release
    [[ ${ID,,} == *kylin* || ${NAME,,} == *kylin* ]] || {
        echo "[PLATFORM ERROR] 当前系统不是银河麒麟" >&2
        exit 2
    }
    grep -Fq "Desktop-V10-SP1-General-Release" /etc/.kyinfo /etc/os-release 2>/dev/null || {
        echo "[PLATFORM ERROR] 当前系统不是 Desktop-V10-SP1-General-Release" >&2
        exit 2
    }
fi

if [[ ${platform_only} -eq 1 ]]; then
    echo "构建平台检查通过"
    exit 0
fi

[[ -r "${requirements_file}" ]] || {
    echo "[CONFIG ERROR] 无法读取需求清单: ${requirements_file}" >&2
    exit 2
}
[[ -z "${missing_packages_file}" ]] || rm -f "${missing_packages_file}"

version_matches()
{
    local actual=$1 expected=$2
    if [[ ${expected} == '>='* ]]; then
        local minimum=${expected#'>='}
        [[ $(printf '%s\n%s\n' "${minimum}" "${actual}" | sort -V | head -n 1) == "${minimum}" ]]
    elif [[ ${expected: -2} == '.*' ]]; then
        [[ ${actual} == "${expected:0:${#expected}-2}".* ]]
    else
        [[ ${actual} == "${expected}" ]]
    fi
}

probe_command()
{
    local command_name=$1
    command -v "${command_name}" >/dev/null 2>&1 &&
        "${command_name}" --version >/dev/null 2>&1
}

command_version()
{
    local command_name=$1 output
    output=$("${command_name}" --version 2>&1 | head -n 1 || true)
    printf '%s' "${output:-${command_name}}"
}

json_escape()
{
    local value=$1
    value=${value//\\/\\\\}
    value=${value//\"/\\\"}
    value=${value//$'\n'/\\n}
    printf '%s' "${value}"
}

failures=0
declare -a missing_packages=()
declare -a detected_ids=()
declare -A detected_probes=()
declare -A detected_constraints=()
declare -A detected_details=()
detected_cmake=
detected_qt=
detected_hidapi=
detected_generator=
while IFS='|' read -r requirement_id probe expected packages description; do
    [[ -n "${requirement_id}" && ${requirement_id} != \#* ]] || continue
    actual=
    detail=
    status=OK
    case "${probe}" in
        command)
            if probe_command "${expected}"; then
                actual=${expected}
                detail=$(command_version "${expected}")
            else
                status=MISSING
            fi
            ;;
        any-command)
            IFS=',' read -r -a candidates <<<"${expected}"
            for candidate in "${candidates[@]}"; do
                if probe_command "${candidate}"; then
                    actual=${candidate}
                    detail=$(command_version "${candidate}")
                    break
                fi
            done
            [[ -n "${actual}" ]] || status=MISSING
            ;;
        cmake)
            if actual=$(cmake --version 2>/dev/null | awk 'NR == 1 { print $3 }') &&
                [[ -n "${actual}" ]]; then
                version_matches "${actual}" "${expected}" || status="WRONG VERSION"
            else
                status=MISSING
            fi
            ;;
        qt)
            if actual=$(qmake -query QT_VERSION 2>/dev/null) && [[ -n "${actual}" ]]; then
                version_matches "${actual}" "${expected}" || status="WRONG VERSION"
            else
                status=MISSING
            fi
            ;;
        pkg-config:*)
            module=${probe#pkg-config:}
            if actual=$(pkg-config --modversion "${module}" 2>/dev/null) && [[ -n "${actual}" ]]; then
                version_matches "${actual}" "${expected}" || status="WRONG VERSION"
            else
                status=MISSING
            fi
            ;;
        *)
            echo "[CONFIG ERROR] 未知探测类型: ${probe}" >&2
            exit 2
            ;;
    esac

    if [[ ${status} == OK ]]; then
        [[ -n "${detail}" ]] || detail=${actual}
        echo "[OK] ${requirement_id}: ${actual}"
        detected_ids+=("${requirement_id}")
        detected_probes[${requirement_id}]=${probe}
        detected_constraints[${requirement_id}]=${expected}
        detected_details[${requirement_id}]=${detail}
        case "${requirement_id}" in
            cmake) detected_cmake=${actual} ;;
            qt) detected_qt=${actual} ;;
            hidapi) detected_hidapi=${actual} ;;
            generator) detected_generator=${actual} ;;
        esac
    else
        echo "[${status}] ${requirement_id}: 需要 ${expected}（包: ${packages}）"
        IFS=';' read -r -a current_packages <<<"${packages}"
        missing_packages+=("${current_packages[@]}")
        failures=$((failures + 1))
    fi
done <"${requirements_file}"

if (( failures > 0 )); then
    if [[ -n "${missing_packages_file}" ]]; then
        missing_directory=$(dirname "${missing_packages_file}")
        mkdir -p "${missing_directory}"
        temporary_missing=$(mktemp "${missing_packages_file}.tmp.XXXXXX")
        printf '%s\n' "${missing_packages[@]}" | awk 'NF' | sort -u >"${temporary_missing}"
        mv "${temporary_missing}" "${missing_packages_file}"
    fi
    echo "构建环境检查失败: ${failures} 项不满足" >&2
    exit 1
fi

echo "构建环境检查通过"

if [[ -n "${lock_file}" ]]; then
    lock_directory=$(dirname "${lock_file}")
    mkdir -p "${lock_directory}"
    temporary_lock=$(mktemp "${lock_file}.tmp.XXXXXX")
    trap 'rm -f "${temporary_lock}"' EXIT
    requirements_sha256=$(sha256sum "${requirements_file}" | awk '{ print $1 }')
    compiler_text=$(json_escape "${detected_details[gcc]}")
    cxx_compiler_text=$(json_escape "${detected_details[gxx]}")
    {
        echo '{'
        echo '  "schema_version": "1.1",'
        echo '  "target_os": "Desktop-V10-SP1-General-Release",'
        echo '  "architecture": "x86_64",'
        printf '  "requirements_sha256": "%s",\n' "${requirements_sha256}"
        printf '  "cmake_version": "%s",\n' "$(json_escape "${detected_cmake}")"
        printf '  "qt_version": "%s",\n' "$(json_escape "${detected_qt}")"
        printf '  "hidapi_version": "%s",\n' "$(json_escape "${detected_hidapi}")"
        printf '  "generator": "%s",\n' "$(json_escape "${detected_generator}")"
        printf '  "compiler": "%s",\n' "${compiler_text}"
        printf '  "cxx_compiler": "%s",\n' "${cxx_compiler_text}"
        echo '  "requirements": {'
        for index in "${!detected_ids[@]}"; do
            requirement_id=${detected_ids[${index}]}
            (( index == 0 )) || echo ','
            printf '    "%s": {"probe": "%s", "constraint": "%s", "detected": "%s"}' \
                "$(json_escape "${requirement_id}")" \
                "$(json_escape "${detected_probes[${requirement_id}]}")" \
                "$(json_escape "${detected_constraints[${requirement_id}]}")" \
                "$(json_escape "${detected_details[${requirement_id}]}")"
        done
        echo
        echo '  },'
        printf '  "generated_at_utc": "%s"\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        echo '}'
    } >"${temporary_lock}"
    mv "${temporary_lock}" "${lock_file}"
    trap - EXIT
    echo "环境锁定文件已生成: ${lock_file}"
fi
