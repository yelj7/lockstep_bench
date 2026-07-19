#!/usr/bin/env bash
# /**********************************************************
# * 文件名: verify-install.sh
# * 日期: 2026-07-13
# * 版本: v1.0
# * 更新记录: 初版创建银河麒麟安装验收脚本
# * 描述: 验证包状态、文件布局、ELF 架构、动态依赖和设备权限配置
# **********************************************************/

set -euo pipefail

app_root=/opt/lockstep-host
failed=0

check_file()
{
    if [ ! -e "$1" ]; then
        echo "缺失：$1" >&2
        failed=1
    fi
}

dpkg-query -W -f='${Status}\n' lockstep-host 2>/dev/null | grep -q "install ok installed" || {
    echo "DEB 未处于已安装状态。" >&2
    failed=1
}

check_file "${app_root}/bin/lockstep_ui_preview"
check_file "${app_root}/plugins/platforms/libqxcb.so"
check_file "${app_root}/resources/manifest.json"
check_file "${app_root}/share/doc/runtime-dependencies.json"
check_file /usr/bin/lockstep-host
check_file /usr/share/applications/lockstep-host.desktop
check_file /etc/udev/rules.d/99-lockstep-cmsis-dap.rules
check_file /etc/udev/rules.d/99-lockstep-ft601.rules

while IFS= read -r candidate; do
    file_description=$(file -b "${candidate}")
    [[ ${file_description} == *ELF* ]] || continue
    if [[ ${file_description} != *x86-64* ]]; then
        echo "ELF 架构错误：${candidate}" >&2
        failed=1
    fi
    dependencies=$(ldd "${candidate}" 2>&1 || true)
    if grep -q "not found" <<<"${dependencies}"; then
        echo "动态库缺失：${candidate}" >&2
        echo "${dependencies}" >&2
        failed=1
    fi
done < <(find "${app_root}" -type f -print)

if grep -R -q --include='*.json' 'targetDebugToolPath' "${app_root}/resources"; then
    echo "资源清单仍引用已删除的独立调试服务。" >&2
    failed=1
fi

ft601_present=0
for vendor_file in /sys/bus/usb/devices/*/idVendor; do
    [[ -f ${vendor_file} ]] || continue
    device_root=${vendor_file%/idVendor}
    [[ $(cat "${vendor_file}") == 0403 ]] || continue
    [[ -f "${device_root}/idProduct" && $(cat "${device_root}/idProduct") == 601f ]] || continue
    ft601_present=1
    break
done
if [[ ${ft601_present} -eq 1 ]]; then
    if [[ $(id -u) -eq 0 ]]; then
        verify_user=${LOCKSTEP_VERIFY_USER:-${SUDO_USER:-}}
        if [[ -z ${verify_user} || ${verify_user} == root ]] || ! id "${verify_user}" >/dev/null 2>&1; then
            echo "FT601 已连接；root 验收必须通过 LOCKSTEP_VERIFY_USER 或 SUDO_USER 指定普通用户。" >&2
            failed=1
            usb_status_output=""
        else
            usb_status_output=$(runuser -u "${verify_user}" -- lockstep-host --usb-status 2>&1) || {
                echo "FT601 已连接，但普通用户 ${verify_user} 的 libusb 枚举/claim 验收失败：" >&2
                echo "${usb_status_output}" >&2
                failed=1
            }
        fi
    else
        usb_status_output=$(lockstep-host --usb-status 2>&1) || {
            echo "FT601 已连接，但当前普通用户的 libusb 枚举/claim 验收失败：" >&2
            echo "${usb_status_output}" >&2
            failed=1
        }
    fi
else
    echo "未连接 FT601，跳过普通用户 --usb-status 真机验收。"
fi

if [ "${failed}" -ne 0 ]; then
    echo "安装验收失败。" >&2
    exit 1
fi

echo "安装基础验收通过。正式采集、烧写、回读、运行和串口收发仍需硬件联调。"
