#!/bin/sh
# /**********************************************************
# * 文件名: install.sh
# * 日期: 2026-07-13
# * 版本: v1.0
# * 更新记录: 初版创建银河麒麟离线安装脚本
# * 描述: 校验离线套件、安装 DEB 并授权当前普通用户访问调试器和串口
# **********************************************************/

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
set -- "${script_dir}"/lockstep-host_*_amd64.deb
if [ "$#" -ne 1 ] || [ ! -f "$1" ]; then
    echo "错误：交付目录必须包含且只能包含一个 lockstep-host_*_amd64.deb。" >&2
    exit 1
fi
deb_file=$1

if [ "$(uname -m)" != "x86_64" ]; then
    echo "错误：本安装包仅支持 x86_64/amd64。" >&2
    exit 1
fi
if [ -f "${script_dir}/SHA256SUMS" ]; then
    (cd "${script_dir}" && sha256sum -c SHA256SUMS)
fi

install_user=${SUDO_USER:-$(id -un)}
if [ "${install_user}" = root ]; then
    install_user=${LOCKSTEP_USER:-}
fi

if [ "$(id -u)" -eq 0 ]; then
    privilege_command=""
else
    command -v sudo >/dev/null 2>&1 || {
        echo "错误：安装需要 root 权限或 sudo。" >&2
        exit 1
    }
    privilege_command=sudo
fi

${privilege_command} dpkg -i "${deb_file}"

if [ -n "${install_user}" ] && id "${install_user}" >/dev/null 2>&1; then
    ${privilege_command} usermod -a -G plugdev,dialout "${install_user}"
    echo "已将用户 ${install_user} 加入 plugdev 和 dialout。"
fi

if command -v udevadm >/dev/null 2>&1; then
    ${privilege_command} udevadm control --reload-rules || true
    ${privilege_command} udevadm trigger --subsystem-match=hidraw || true
fi

echo "安装完成。请重新登录后从应用菜单启动“锁步研发测试系统”，或运行 lockstep-host。"
