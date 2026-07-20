# /**********************************************************
# * 文件名: scan_zcu102_vivado.tcl
# * 日期: 2026-07-20
# * 版本: 1.1
# * 更新记录: 增加按 HS2 序列号选择唯一 target，避免扫描其他 FTDI 线缆。
# * 描述: 列出硬件目标与器件，不配置 PL、不修改线缆驱动。
# **********************************************************/

open_hw_manager
connect_hw_server -allow_non_jtag
set hs2_serial [expr {$argc == 1 ? [lindex $argv 0] : ""}]
set targets {}
foreach target [get_hw_targets -quiet] {
    if {$hs2_serial eq "" || [string match "*$hs2_serial*" [get_property NAME $target]]} {
        lappend targets $target
    }
}
puts "LOCKSTEP_HW_TARGET_COUNT=[llength $targets]"
foreach target $targets {
    puts "LOCKSTEP_HW_TARGET=[get_property NAME $target]"
}
if {[llength $targets] != 1} {
    disconnect_hw_server
    close_hw_manager
    error "Expected exactly one Vivado hardware target, found [llength $targets]"
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
