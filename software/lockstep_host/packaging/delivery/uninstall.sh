#!/bin/sh
# /**********************************************************
# * 文件名: uninstall.sh
# * 日期: 2026-07-13
# * 版本: v1.0
# * 更新记录: 初版创建银河麒麟卸载脚本
# * 描述: 完整移除应用和设备规则，但保留所有用户工作区与测试数据
# **********************************************************/

set -eu

if [ "$(id -u)" -eq 0 ]; then
    privilege_command=""
else
    command -v sudo >/dev/null 2>&1 || {
        echo "错误：卸载需要 root 权限或 sudo。" >&2
        exit 1
    }
    privilege_command=sudo
fi

if dpkg-query -W -f='${Status}' lockstep-host 2>/dev/null | grep -q "install ok installed"; then
    ${privilege_command} dpkg --purge lockstep-host
else
    echo "lockstep-host 未安装。"
fi

echo "卸载完成；用户工作区和测试数据未被删除。"
