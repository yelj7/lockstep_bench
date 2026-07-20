# /**********************************************************
# * 文件名: scan_zcu102_vivado.tcl
# * 日期: 2026-07-20
# * 版本: 1.0
# * 更新记录: 新增 Vivado Hardware Manager 只读扫链脚本。
# * 描述: 列出硬件目标与器件，不配置 PL、不修改线缆驱动。
# **********************************************************/

open_hw_manager
connect_hw_server -allow_non_jtag
set targets [get_hw_targets -quiet]
puts "LOCKSTEP_HW_TARGET_COUNT=[llength $targets]"
foreach target $targets {
    puts "LOCKSTEP_HW_TARGET=[get_property NAME $target]"
}
if {[llength $targets] == 0} {
    disconnect_hw_server
    close_hw_manager
    error "No Vivado hardware target found"
}
open_hw_target [lindex $targets 0]
set devices [get_hw_devices -quiet]
puts "LOCKSTEP_HW_DEVICE_COUNT=[llength $devices]"
foreach device $devices {
    puts "LOCKSTEP_HW_DEVICE=[get_property NAME $device] PART=[get_property PART $device]"
}
close_hw_target
disconnect_hw_server
close_hw_manager
