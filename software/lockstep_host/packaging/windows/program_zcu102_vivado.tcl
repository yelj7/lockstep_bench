# /**********************************************************
# * 文件名: program_zcu102_vivado.tcl
# * 日期: 2026-07-20
# * 版本: 1.1
# * 更新记录: 使用已验证的 xczu9 对象名匹配，避免未刷新 PART 属性误判。
# * 描述: 扫描唯一 Zynq UltraScale+ 器件并配置指定 bitstream。
# **********************************************************/

if {$argc != 2} {
    error "Usage: program_zcu102_vivado.tcl <bitstream> <hs2-serial>"
}
set bitstream [file normalize [lindex $argv 0]]
set hs2_serial [lindex $argv 1]
if {![file exists $bitstream]} {
    error "Bitstream does not exist: $bitstream"
}

open_hw_manager
connect_hw_server -allow_non_jtag
set matching_targets {}
foreach target [get_hw_targets -quiet] {
    if {[string match "*$hs2_serial*" [get_property NAME $target]]} {
        lappend matching_targets $target
    }
}
if {[llength $matching_targets] != 1} {
    error "Expected exactly one Vivado target for HS2 $hs2_serial, found [llength $matching_targets]"
}
set target [lindex $matching_targets 0]
current_hw_target $target
open_hw_target $target
set devices [get_hw_devices -quiet *xczu9*]
if {[llength $devices] != 1} {
    error "Expected exactly one xczu9eg device, found [llength $devices]"
}
set device [lindex $devices 0]
current_hw_device $device
refresh_hw_device -update_hw_probes false $device
set_property PROGRAM.FILE $bitstream $device
program_hw_devices $device
refresh_hw_device -update_hw_probes false $device
puts "LOCKSTEP_PROGRAM_SUCCESS device=[get_property NAME $device] part=[get_property PART $device]"
close_hw_target
disconnect_hw_server
close_hw_manager
