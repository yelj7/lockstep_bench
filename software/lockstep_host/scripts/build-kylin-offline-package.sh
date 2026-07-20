#!/usr/bin/env bash
# /**********************************************************
# * 文件名: build-kylin-offline-package.sh
# * 日期: 2026-07-13
# * 版本: v1.2
# * 更新记录: 接入统一构建环境需求检查并保留生成器自动选择
# * 描述: 原生构建、测试、审计并生成自包含 DEB 和离线交付目录
# **********************************************************/

set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_dir=$(CDPATH= cd -- "${script_dir}/.." && pwd)
build_dir=${LOCKSTEP_BUILD_DIR:-"${source_dir}/build-kylin-release"}
dist_dir=${LOCKSTEP_DIST_DIR:-"${source_dir}/dist/kylin-v10-sp1-amd64"}

if [[ ${LOCKSTEP_ALLOW_UNSUPPORTED_BUILD:-0} == 1 ]]; then
    LOCKSTEP_SKIP_PLATFORM_CHECK=1 "${script_dir}/check-kylin-build-env.sh"
else
    "${script_dir}/check-kylin-build-env.sh"
fi

if command -v ninja >/dev/null 2>&1; then
    cmake_generator=Ninja
elif command -v make >/dev/null 2>&1; then
    cmake_generator="Unix Makefiles"
else
    echo "缺少构建工具：请离线安装 Ninja 或 GNU Make 其中之一" >&2
    exit 1
fi
echo "使用 CMake 生成器: ${cmake_generator}"

qt_version=$(qmake -query QT_VERSION)
hidapi_version=$(pkg-config --modversion hidapi-hidraw)

assert_safe_output_path()
{
    local output_path=$1
    case "${output_path}" in
        "${source_dir}"/*) ;;
        *) echo "输出目录必须位于源码根目录内: ${output_path}" >&2; exit 1 ;;
    esac
    [[ ${output_path} != "${source_dir}" && ${output_path} != / ]] || {
        echo "拒绝清理危险输出目录: ${output_path}" >&2
        exit 1
    }
}

build_dir=$(realpath -m "${build_dir}")
dist_dir=$(realpath -m "${dist_dir}")
assert_safe_output_path "${build_dir}"
assert_safe_output_path "${dist_dir}"

rm -rf "${build_dir}" "${dist_dir}"
mkdir -p "${build_dir}" "${dist_dir}"

cmake -S "${source_dir}" -B "${build_dir}" -G "${cmake_generator}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/ \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "${build_dir}/CMakeCache.txt")
[[ -n "${version}" ]] || { echo "无法从 CMake 项目读取版本" >&2; exit 1; }
cmake --build "${build_dir}" --parallel
ctest --test-dir "${build_dir}" --output-on-failure
cpack --config "${build_dir}/CPackConfig.cmake" -B "${build_dir}/packages"

generated_deb="${build_dir}/packages/lockstep-host_${version}_amd64.deb"
[[ -f "${generated_deb}" ]] || { echo "CPack 未生成预期 DEB" >&2; exit 1; }

package_tree="${build_dir}/final-package-tree"
rm -rf "${package_tree}"
dpkg-deb -R "${generated_deb}" "${package_tree}"

while IFS= read -r -d '' elf_file; do
    file -b "${elf_file}" | grep -q ELF || continue
    case "${elf_file}" in
        */opt/lockstep-host/bin/*) patchelf --set-rpath '$ORIGIN/../lib' "${elf_file}" ;;
        */opt/lockstep-host/plugins/*) patchelf --set-rpath '$ORIGIN/../../lib' "${elf_file}" ;;
        */opt/lockstep-host/lib/*) patchelf --set-rpath '$ORIGIN' "${elf_file}" ;;
    esac
done < <(find "${package_tree}/opt/lockstep-host" -type f -print0)

export LOCKSTEP_BUILD_VERSION=${version}
export LOCKSTEP_QT_VERSION=${qt_version}
export LOCKSTEP_HIDAPI_VERSION=${hidapi_version}
dependency_manifest="${package_tree}/opt/lockstep-host/share/doc/runtime-dependencies.json"
"${script_dir}/generate-runtime-dependencies.sh" "${package_tree}" "${dependency_manifest}"
"${script_dir}/audit-linux-runtime.sh" "${package_tree}"

package_for_path()
{
    local dependency_path=$1
    local package_name
    package_name=$(dpkg-query -S "${dependency_path}" 2>/dev/null | head -n 1 | cut -d: -f1 || true)
    if [[ -z "${package_name}" && -e "${dependency_path}" ]]; then
        package_name=$(dpkg-query -S "$(readlink -f "${dependency_path}")" 2>/dev/null |
            head -n 1 | cut -d: -f1 || true)
    fi
    printf '%s' "${package_name}"
}

system_dependency_paths="${build_dir}/system-dependency-paths.txt"
system_packages_file="${build_dir}/system-package-dependencies.txt"
: >"${system_dependency_paths}"
while IFS= read -r -d '' elf_file; do
    file -b "${elf_file}" | grep -q ELF || continue
    ldd "${elf_file}" 2>/dev/null |
        awk '/=> \/|^\// { if ($2 == "=>") print $3; else print $1 }' >>"${system_dependency_paths}" || true
done < <(find "${package_tree}/opt/lockstep-host" -type f -print0)

: >"${system_packages_file}"
while IFS= read -r dependency_path; do
    [[ -f "${dependency_path}" ]] || continue
    case "${dependency_path}" in
        "${package_tree}/opt/lockstep-host/lib/"*) continue ;;
    esac
    package_name=$(package_for_path "${dependency_path}")
    [[ -n "${package_name}" ]] || {
        echo "无法确定系统依赖 ${dependency_path} 的软件包" >&2
        exit 1
    }
    echo "${package_name}" >>"${system_packages_file}"
done < <(sort -u "${system_dependency_paths}")
echo udev >>"${system_packages_file}"
sort -u -o "${system_packages_file}" "${system_packages_file}"
debian_depends=$(awk 'BEGIN { first = 1 } { if (!first) printf ", "; printf "%s", $0; first = 0 }' \
    "${system_packages_file}")
[[ -n "${debian_depends}" ]] || { echo "系统依赖软件包清单为空" >&2; exit 1; }
sed -i "s/^Depends:.*/Depends: ${debian_depends}/" "${package_tree}/DEBIAN/control"

license_dir="${package_tree}/opt/lockstep-host/share/doc/licenses"
mkdir -p "${license_dir}"
cp /usr/share/common-licenses/LGPL-3 "${license_dir}/LGPL-3.txt"
cp /usr/share/common-licenses/GPL-3 "${license_dir}/GPL-3.txt"
cp /usr/share/common-licenses/GPL-2 "${license_dir}/GPL-2.txt"

copy_package_copyright()
{
    local package_name=$1
    local output_name=$2
    local copyright_file
    copyright_file=$(dpkg-query -L "${package_name}" 2>/dev/null | awk '/\/copyright$/ { print; exit }')
    [[ -n "${copyright_file}" && -f "${copyright_file}" ]] || {
        echo "无法找到 ${package_name} 的版权文件" >&2
        exit 1
    }
    cp "${copyright_file}" "${license_dir}/${output_name}"
}

license_packages_file="${build_dir}/license-source-packages.txt"
cp "${system_packages_file}" "${license_packages_file}"
qt_package=$(package_for_path "$(qmake -query QT_INSTALL_LIBS)/libQt5Core.so.5")
qt_serial_package=$(package_for_path "$(qmake -query QT_INSTALL_LIBS)/libQt5SerialPort.so.5")
hidapi_library=$(pkg-config --variable=libdir hidapi-hidraw)/libhidapi-hidraw.so
hidapi_package=$(package_for_path "${hidapi_library}")
libstdcpp_package=$(package_for_path "$(c++ -print-file-name=libstdc++.so.6)")
libgcc_package=$(package_for_path "$(c++ -print-file-name=libgcc_s.so.1)")
[[ -n "${qt_package}" && -n "${qt_serial_package}" && -n "${hidapi_package}" &&
   -n "${libstdcpp_package}" && -n "${libgcc_package}" ]] || {
    echo "无法确定 Qt、hidapi 或 GCC 运行库的来源软件包" >&2
    exit 1
}
printf '%s\n' "${qt_package}" "${qt_serial_package}" "${hidapi_package}" \
    "${libstdcpp_package}" "${libgcc_package}" >>"${license_packages_file}"
installed_plugin_root="${package_tree}/opt/lockstep-host/plugins"
qt_plugin_root=$(qmake -query QT_INSTALL_PLUGINS)
while IFS= read -r installed_plugin_path; do
    plugin_relative_path=${installed_plugin_path#"${installed_plugin_root}/"}
    plugin_package=$(package_for_path "${qt_plugin_root}/${plugin_relative_path}")
    [[ -z "${plugin_package}" ]] || echo "${plugin_package}" >>"${license_packages_file}"
done < <(find "${installed_plugin_root}" -type f -name '*.so' -print)
sort -u -o "${license_packages_file}" "${license_packages_file}"
while IFS= read -r package_name; do
    safe_package_name=$(printf '%s' "${package_name}" | tr ':/' '__')
    copy_package_copyright "${package_name}" "${safe_package_name}-copyright"
done <"${license_packages_file}"

cat >"${package_tree}/opt/lockstep-host/share/doc/build-environment.json" <<EOF
{
  "target_os": "Desktop-V10-SP1-General-Release",
  "architecture": "amd64",
  "project_version": "${version}",
  "qt_version": "${qt_version}",
  "hidapi_version": "${hidapi_version}",
  "cmake_version": "$(cmake --version | head -n 1 | awk '{print $3}')",
  "cmake_generator": "${cmake_generator}",
  "compiler": "$(c++ --version | head -n 1)",
  "source_commit": "$(git -C "${source_dir}" rev-parse HEAD 2>/dev/null || echo unknown)"
}
EOF

final_deb="${dist_dir}/lockstep-host_${version}_amd64.deb"
(
    cd "${package_tree}"
    find opt usr etc -type f -print0 | sort -z | xargs -0 md5sum >DEBIAN/md5sums
    installed_size=$(du -sk opt usr etc | awk '{ total += $1 } END { print total }')
    sed -i "s/^Installed-Size:.*/Installed-Size: ${installed_size}/" DEBIAN/control
)
dpkg-deb --root-owner-group --build "${package_tree}" "${final_deb}"
cp "${source_dir}/packaging/delivery/install.sh" "${dist_dir}/"
cp "${source_dir}/packaging/delivery/uninstall.sh" "${dist_dir}/"
cp "${source_dir}/packaging/delivery/verify-install.sh" "${dist_dir}/"
cp "${source_dir}/docs/银河麒麟离线安装说明.md" "${dist_dir}/安装说明.md"
cp "${source_dir}/docs/第三方依赖与许可证.md" "${dist_dir}/第三方依赖与许可证.md"
cp "${dependency_manifest}" "${dist_dir}/runtime-dependencies.json"
cp "${package_tree}/opt/lockstep-host/share/doc/build-environment.json" "${dist_dir}/build-environment.json"
chmod +x "${dist_dir}"/*.sh
(
    cd "${dist_dir}"
    sha256sum lockstep-host_${version}_amd64.deb install.sh uninstall.sh verify-install.sh \
        runtime-dependencies.json build-environment.json >SHA256SUMS
)

echo "离线安装套件已生成: ${dist_dir}"
