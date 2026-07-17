# /**********************************************************
# * 文件名: board_targets.tcl
# * 日期: 2026-07-15
# * 版本: v1.0
# * 更新记录: 上板连接枚举脚本
# * 描述: 连接 hw_server 并输出 JTAG targets。
# **********************************************************/
if {[catch {connect -url TCP:127.0.0.1:3121} message]} {
  puts "CONNECT_ERROR=$message"
  exit 2
}
puts "TARGETS_BEGIN"
targets
puts "TARGETS_END"
disconnect
exit
