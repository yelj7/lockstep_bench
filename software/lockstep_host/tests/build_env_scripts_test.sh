#!/usr/bin/env bash
# /**********************************************************
# * 文件名: build_env_scripts_test.sh
# * 日期: 2026-07-13
# * 版本: v1.1
# * 更新记录: 增加最小安装、预演失败、架构、原子输出和锁文件回归测试
# * 描述: 通过命令适配器验证构建环境检查和部署脚本的公开行为
# **********************************************************/

set -euo pipefail

source_root=$1
test_root=$(mktemp -d)
trap 'rm -rf "${test_root}"' EXIT
mock_bin="${test_root}/bin"
mkdir -p "${mock_bin}"
export LOCKSTEP_SKIP_PLATFORM_CHECK=1

cat >"${mock_bin}/mock-command" <<'EOF'
#!/usr/bin/env bash
name=$(basename "$0")
if [[ ${MOCK_MISSING:-} == "${name}" && ! -f ${MOCK_INSTALLED_STATE:-/nonexistent} ]]; then
    exit 127
fi
case "${name}" in
    cmake) echo "cmake version ${MOCK_CMAKE_VERSION:-3.16.0}" ;;
    qmake) echo "${MOCK_QT_VERSION:-5.15.2}" ;;
    pkg-config)
        [[ -n ${MOCK_PKG_CONFIG_MISSING:-} && ${2:-} == "${MOCK_PKG_CONFIG_MISSING}" ]] && exit 1
        case "${2:-}" in
            Qt5*) echo "${MOCK_QT_VERSION:-5.15.2}" ;;
            *) echo "${MOCK_HIDAPI_VERSION:-0.14.0}" ;;
        esac
        ;;
    gcc|g++) echo "${name} (GCC) 10.3.0" ;;
    uname) echo x86_64 ;;
    realpath) exec /usr/bin/realpath "$@" ;;
    sha256sum) exec /usr/bin/sha256sum "$@" ;;
    sudo) exec "$@" ;;
    apt-get)
        echo "apt-get $*" >>"${MOCK_APT_LOG:-/dev/null}"
        if [[ ${MOCK_APT_SIMULATE_FAIL:-0} == 1 && " $* " == *" -s "* ]]; then
            exit 10
        fi
        [[ " $* " == *" install "* && " $* " != *" -s "* ]] && touch "${MOCK_INSTALLED_STATE}"
        ;;
    dpkg-deb)
        if [[ ${1:-} == -f ]]; then
            base=$(basename "$2")
            metadata=${base%.deb}
            package_name=${metadata%%_*}
            remainder=${metadata#*_}
            package_version=${remainder%%_*}
            package_architecture=${metadata##*_}
            case "${3:-}" in
                Package) echo "${package_name}" ;;
                Version) echo "${package_version}" ;;
                Architecture) echo "${package_architecture}" ;;
            esac
        fi
        ;;
    dpkg) exit 0 ;;
    dpkg-scanpackages)
        [[ ${MOCK_SCAN_FAIL:-0} == 1 ]] && exit 12
        for package_file in ./*.deb; do
            base=$(basename "${package_file}")
            echo "Package: ${base%%_*}"
            echo "Version: 1.0"
            echo "Architecture: amd64"
            echo "Filename: ${package_file}"
            echo
        done
        ;;
esac
exit 0
EOF
chmod +x "${mock_bin}/mock-command"

for command_name in gcc g++ cmake cpack ctest make dpkg dpkg-deb file readelf ldd lddtree \
    sha256sum md5sum patchelf qmake pkg-config realpath uname; do
    ln -s mock-command "${mock_bin}/${command_name}"
done
ln -s mock-command "${mock_bin}/sudo"
ln -s mock-command "${mock_bin}/apt-get"
ln -s mock-command "${mock_bin}/dpkg-scanpackages"

output=$(PATH="${mock_bin}:/usr/bin:/bin" \
    LOCKSTEP_SKIP_PLATFORM_CHECK=1 \
    "${source_root}/scripts/check-kylin-build-env.sh")

grep -q '^\[OK\] cmake: 3.16.0$' <<<"${output}"
grep -q '^\[OK\] qt: 5.15.2$' <<<"${output}"
grep -q '^\[OK\] hidapi: 0.14.0$' <<<"${output}"
grep -q '^构建环境检查通过$' <<<"${output}"

expect_check_failure()
{
    local expected_text=$1
    shift
    local failure_output status
    set +e
    failure_output=$(PATH="${mock_bin}:/usr/bin:/bin" \
        LOCKSTEP_SKIP_PLATFORM_CHECK=1 "$@" \
        "${source_root}/scripts/check-kylin-build-env.sh" 2>&1)
    status=$?
    set -e
    [[ ${status} -eq 1 ]]
    grep -q "${expected_text}" <<<"${failure_output}"
}

expect_check_failure '^\[MISSING\] cmake:' env MOCK_MISSING=cmake
expect_check_failure '^\[WRONG VERSION\] qt:' env MOCK_QT_VERSION=5.12.8
expect_check_failure '^\[WRONG VERSION\] qt:' env MOCK_QT_VERSION=5.15.9
expect_check_failure '^\[WRONG VERSION\] hidapi:' env MOCK_HIDAPI_VERSION=0.13.1
expect_check_failure '^\[MISSING\] qt_serialport:' env MOCK_PKG_CONFIG_MISSING=Qt5SerialPort
expect_check_failure '^\[MISSING\] generator:' env MOCK_MISSING=make

missing_packages_output="${test_root}/missing-packages.txt"
set +e
PATH="${mock_bin}:/usr/bin:/bin" MOCK_MISSING=patchelf \
    "${source_root}/scripts/check-kylin-build-env.sh" \
    --write-missing-packages "${missing_packages_output}" >/dev/null 2>&1
missing_packages_status=$?
set -e
[[ ${missing_packages_status} -eq 1 ]]
grep -qx patchelf "${missing_packages_output}"
PATH="${mock_bin}:/usr/bin:/bin" \
    "${source_root}/scripts/check-kylin-build-env.sh" \
    --write-missing-packages "${missing_packages_output}" >/dev/null
[[ ! -e "${missing_packages_output}" ]]

lock_file="${test_root}/build-environment.lock.json"
PATH="${mock_bin}:/usr/bin:/bin" LOCKSTEP_SKIP_PLATFORM_CHECK=1 \
    "${source_root}/scripts/check-kylin-build-env.sh" --write-lock "${lock_file}" >/dev/null
[[ -s "${lock_file}" ]]
if command -v python3 >/dev/null 2>&1; then
    python3 -c 'import json, sys; json.load(open(sys.argv[1], encoding="utf-8"))' "${lock_file}"
elif [[ -x /ucrt64/bin/python.exe ]]; then
    /ucrt64/bin/python.exe -c \
        'import json, sys; json.load(open(sys.argv[1], encoding="utf-8"))' "${lock_file}"
fi
grep -q '"cmake_version": "3.16.0"' "${lock_file}"
grep -q '"qt_version": "5.15.2"' "${lock_file}"
grep -q '"hidapi_version": "0.14.0"' "${lock_file}"
grep -q '"generator": "make"' "${lock_file}"
for requirement_id in gcc gxx cmake cpack ctest generator apt_get dpkg_deb \
    dpkg_scanpackages gzip file readelf ldd \
    lddtree sha256sum md5sum patchelf realpath qt qt_core qt_gui qt_widgets \
    qt_network qt_serialport hidapi pkg_config; do
    grep -q "\"${requirement_id}\": {" "${lock_file}"
done

failed_lock="${test_root}/failed.lock.json"
set +e
PATH="${mock_bin}:/usr/bin:/bin" LOCKSTEP_SKIP_PLATFORM_CHECK=1 MOCK_QT_VERSION=5.12.8 \
    "${source_root}/scripts/check-kylin-build-env.sh" --write-lock "${failed_lock}" >/dev/null 2>&1
failed_lock_status=$?
set -e
[[ ${failed_lock_status} -eq 1 && ! -e "${failed_lock}" ]]

bundle_root="${test_root}/bundle"
mkdir -p "${bundle_root}/repo"
touch "${bundle_root}/repo/Packages.gz" "${bundle_root}/repo/fixture.deb"
apt_log="${test_root}/apt.log"
installed_state="${test_root}/installed.state"
bootstrap_lock="${test_root}/bootstrap.lock.json"

missing_checksum_bundle="${test_root}/missing-checksum-bundle"
mkdir -p "${missing_checksum_bundle}/repo"
touch "${missing_checksum_bundle}/repo/Packages.gz" "${missing_checksum_bundle}/repo/fixture.deb"
: >"${apt_log}"
set +e
PATH="${mock_bin}:/usr/bin:/bin" \
    LOCKSTEP_SKIP_PLATFORM_CHECK=1 \
    MOCK_MISSING=patchelf \
    MOCK_APT_LOG="${apt_log}" \
    "${source_root}/scripts/bootstrap-kylin-build-env.sh" \
    --bundle "${missing_checksum_bundle}" >/dev/null 2>&1
missing_checksum_status=$?
set -e
[[ ${missing_checksum_status} -eq 2 ]]
! grep -Eq ' (update|install)' "${apt_log}"

tampered_bundle="${test_root}/tampered-bundle"
tampered_marker="${test_root}/tampered-check-ran"
mkdir -p "${tampered_bundle}/repo"
touch "${tampered_bundle}/repo/Packages.gz" "${tampered_bundle}/repo/fixture.deb"
cp "${source_root}/packaging/kylin-env/build-requirements.psv" \
    "${tampered_bundle}/build-requirements.psv"
cp "${source_root}/scripts/check-kylin-build-env.sh" "${tampered_bundle}/check-kylin-build-env.sh"
chmod +x "${tampered_bundle}/check-kylin-build-env.sh"
(
    cd "${tampered_bundle}"
    /usr/bin/sha256sum repo/Packages.gz repo/fixture.deb build-requirements.psv \
        check-kylin-build-env.sh >SHA256SUMS
)
cat >"${tampered_bundle}/check-kylin-build-env.sh" <<EOF
#!/usr/bin/env bash
touch "${tampered_marker}"
exit 0
EOF
chmod +x "${tampered_bundle}/check-kylin-build-env.sh"
set +e
PATH="${mock_bin}:/usr/bin:/bin" \
    "${source_root}/scripts/bootstrap-kylin-build-env.sh" \
    --bundle "${tampered_bundle}" >/dev/null 2>&1
tampered_status=$?
set -e
[[ ${tampered_status} -ne 0 && ! -e "${tampered_marker}" ]]

(
    cd "${bundle_root}"
    /usr/bin/sha256sum repo/Packages.gz repo/fixture.deb >SHA256SUMS
)
PATH="${mock_bin}:/usr/bin:/bin" \
    LOCKSTEP_SKIP_PLATFORM_CHECK=1 \
    LOCKSTEP_BUILD_ENV_LOCK="${bootstrap_lock}" \
    MOCK_MISSING=patchelf \
    MOCK_INSTALLED_STATE="${installed_state}" \
    MOCK_APT_LOG="${apt_log}" \
    "${source_root}/scripts/bootstrap-kylin-build-env.sh" --bundle "${bundle_root}" >/dev/null
[[ -s "${bootstrap_lock}" ]]
grep -q 'apt-get .* update' "${apt_log}"
grep -Eq 'apt-get .* install patchelf$' "${apt_log}"
! grep -Eq ' install .*(coreutils|dpkg|libc-bin)' "${apt_log}"
! grep -Eq 'https?://' "${apt_log}"

simulate_state="${test_root}/simulate-installed.state"
simulate_log="${test_root}/simulate-apt.log"
set +e
PATH="${mock_bin}:/usr/bin:/bin" \
    LOCKSTEP_SKIP_PLATFORM_CHECK=1 \
    MOCK_MISSING=patchelf \
    MOCK_INSTALLED_STATE="${simulate_state}" \
    MOCK_APT_LOG="${simulate_log}" \
    MOCK_APT_SIMULATE_FAIL=1 \
    "${source_root}/scripts/bootstrap-kylin-build-env.sh" --bundle "${bundle_root}" >/dev/null 2>&1
simulate_status=$?
set -e
[[ ${simulate_status} -eq 10 && ! -e "${simulate_state}" ]]

: >"${apt_log}"
ready_lock="${test_root}/ready.lock.json"
PATH="${mock_bin}:/usr/bin:/bin" \
    LOCKSTEP_SKIP_PLATFORM_CHECK=1 \
    LOCKSTEP_BUILD_ENV_LOCK="${ready_lock}" \
    MOCK_APT_LOG="${apt_log}" \
    "${source_root}/scripts/bootstrap-kylin-build-env.sh" --bundle "${bundle_root}" >/dev/null
[[ -s "${ready_lock}" ]]
! grep -Eq ' (update|install)' "${apt_log}"

platform_bundle="${test_root}/platform-bundle"
mkdir -p "${platform_bundle}/repo"
cp "${source_root}/packaging/kylin-env/build-requirements.psv" \
    "${platform_bundle}/build-requirements.psv"
touch "${platform_bundle}/repo/Packages.gz" "${platform_bundle}/repo/fixture.deb"
cat >"${platform_bundle}/check-kylin-build-env.sh" <<'EOF'
#!/usr/bin/env bash
echo "[PLATFORM ERROR] wrong platform" >&2
exit 2
EOF
chmod +x "${platform_bundle}/check-kylin-build-env.sh"
(
    cd "${platform_bundle}"
    /usr/bin/sha256sum repo/Packages.gz repo/fixture.deb build-requirements.psv \
        check-kylin-build-env.sh >SHA256SUMS
)
: >"${apt_log}"
set +e
PATH="${mock_bin}:/usr/bin:/bin" MOCK_APT_LOG="${apt_log}" \
    "${source_root}/scripts/bootstrap-kylin-build-env.sh" \
    --bundle "${platform_bundle}" >/dev/null 2>&1
platform_status=$?
set -e
[[ ${platform_status} -eq 2 && ! -s "${apt_log}" ]]

prepare_requirements="${test_root}/prepare-requirements.tsv"
cat >"${prepare_requirements}" <<'EOF'
# id|probe|expected|packages|description
alpha|command|alpha|alpha|Alpha
beta|command|beta|beta|Beta
EOF
deb_input="${test_root}/debs"
prepared_bundle="${test_root}/prepared-bundle"
prepare_apt_log="${test_root}/prepare-apt.log"
mkdir -p "${deb_input}"
touch "${deb_input}/alpha_1.0_amd64.deb" "${deb_input}/beta_1.0_amd64.deb"
PATH="${mock_bin}:/usr/bin:/bin" MOCK_APT_LOG="${prepare_apt_log}" \
    "${source_root}/scripts/prepare-kylin-build-env-bundle.sh" \
    --requirements "${prepare_requirements}" \
    --deb-dir "${deb_input}" \
    --output "${prepared_bundle}" >/dev/null
[[ -s "${prepared_bundle}/repo/Packages.gz" ]]
[[ -s "${prepared_bundle}/SHA256SUMS" ]]
[[ -s "${prepared_bundle}/deb-packages.psv" ]]
[[ -x "${prepared_bundle}/check-kylin-build-env.sh" ]]
[[ -x "${prepared_bundle}/bootstrap-kylin-build-env.sh" ]]
grep -q 'repo/alpha_1.0_amd64.deb' "${prepared_bundle}/SHA256SUMS"
grep -Eq '^alpha\|1\.0\|amd64\|[0-9a-f]{64}\|alpha_1\.0_amd64\.deb$' \
    "${prepared_bundle}/deb-packages.psv"
grep -Eq 'Dir::State::status=.*\.apt-state/status' "${prepare_apt_log}"

incomplete_debs="${test_root}/incomplete-debs"
incomplete_output="${test_root}/incomplete-output"
mkdir -p "${incomplete_debs}"
touch "${incomplete_debs}/alpha_1.0_amd64.deb"
set +e
PATH="${mock_bin}:/usr/bin:/bin" \
    "${source_root}/scripts/prepare-kylin-build-env-bundle.sh" \
    --requirements "${prepare_requirements}" \
    --deb-dir "${incomplete_debs}" \
    --output "${incomplete_output}" >/dev/null 2>&1
incomplete_status=$?
set -e
[[ ${incomplete_status} -eq 1 && ! -e "${incomplete_output}" ]]

wrong_arch_debs="${test_root}/wrong-arch-debs"
wrong_arch_output="${test_root}/wrong-arch-output"
mkdir -p "${wrong_arch_debs}"
touch "${wrong_arch_debs}/alpha_1.0_amd64.deb" "${wrong_arch_debs}/beta_1.0_arm64.deb"
set +e
PATH="${mock_bin}:/usr/bin:/bin" \
    "${source_root}/scripts/prepare-kylin-build-env-bundle.sh" \
    --requirements "${prepare_requirements}" \
    --deb-dir "${wrong_arch_debs}" \
    --output "${wrong_arch_output}" >/dev/null 2>&1
wrong_arch_status=$?
set -e
[[ ${wrong_arch_status} -eq 1 && ! -e "${wrong_arch_output}" ]]

wrong_version_requirements="${test_root}/wrong-version-requirements.psv"
cat >"${wrong_version_requirements}" <<'EOF'
# id|probe|expected|packages|description
qt|qt|5.15.2|qtbase5-dev|Qt
EOF
wrong_version_debs="${test_root}/wrong-version-debs"
wrong_version_output="${test_root}/wrong-version-output"
mkdir -p "${wrong_version_debs}"
touch "${wrong_version_debs}/qtbase5-dev_5.15.9_amd64.deb"
set +e
PATH="${mock_bin}:/usr/bin:/bin" \
    "${source_root}/scripts/prepare-kylin-build-env-bundle.sh" \
    --requirements "${wrong_version_requirements}" \
    --deb-dir "${wrong_version_debs}" \
    --output "${wrong_version_output}" >/dev/null 2>&1
wrong_version_status=$?
set -e
[[ ${wrong_version_status} -eq 1 && ! -e "${wrong_version_output}" ]]

scan_failure_output="${test_root}/scan-failure-output"
set +e
PATH="${mock_bin}:/usr/bin:/bin" MOCK_SCAN_FAIL=1 \
    "${source_root}/scripts/prepare-kylin-build-env-bundle.sh" \
    --requirements "${prepare_requirements}" \
    --deb-dir "${deb_input}" \
    --output "${scan_failure_output}" >/dev/null 2>&1
scan_failure_status=$?
set -e
[[ ${scan_failure_status} -eq 12 && ! -e "${scan_failure_output}" ]]

dependency_failure_output="${test_root}/dependency-failure-output"
set +e
PATH="${mock_bin}:/usr/bin:/bin" MOCK_APT_SIMULATE_FAIL=1 \
    "${source_root}/scripts/prepare-kylin-build-env-bundle.sh" \
    --requirements "${prepare_requirements}" \
    --deb-dir "${deb_input}" \
    --output "${dependency_failure_output}" >/dev/null 2>&1
dependency_failure_status=$?
set -e
[[ ${dependency_failure_status} -eq 10 && ! -e "${dependency_failure_output}" ]]

wrong_platform_output="${test_root}/wrong-platform-output"
set +e
PATH="${mock_bin}:/usr/bin:/bin" LOCKSTEP_SKIP_PLATFORM_CHECK=0 \
    "${source_root}/scripts/prepare-kylin-build-env-bundle.sh" \
    --requirements "${prepare_requirements}" \
    --deb-dir "${deb_input}" \
    --output "${wrong_platform_output}" >/dev/null 2>&1
wrong_platform_status=$?
set -e
[[ ${wrong_platform_status} -eq 2 && ! -e "${wrong_platform_output}" ]]
