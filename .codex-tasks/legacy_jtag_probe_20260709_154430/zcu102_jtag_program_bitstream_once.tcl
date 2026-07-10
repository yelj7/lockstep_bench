if {![info exists ::env(ZCU102_BITSTREAM_FILE)]} {
  error "ZCU102_BITSTREAM_FILE is required."
}

set bit_file [file normalize $::env(ZCU102_BITSTREAM_FILE)]
if {![file exists $bit_file]} {
  error "Bitstream file does not exist: $bit_file"
}

set status_file ""
if {[info exists ::env(ZCU102_PROGRAM_STATUS_FILE)]} {
  set status_file [file normalize $::env(ZCU102_PROGRAM_STATUS_FILE)]
}

open_hw_manager
connect_hw_server -allow_non_jtag

set targets [get_hw_targets *]
if {[llength $targets] == 0} {
  error "No Vivado hardware targets found for bitstream programming."
}

open_hw_target [lindex $targets 0]

set devices [get_hw_devices *xczu9*]
if {[llength $devices] == 0} {
  error "No xczu9 hardware device found for bitstream programming."
}

set device [lindex $devices 0]
current_hw_device $device
refresh_hw_device $device
set_property PROGRAM.FILE $bit_file $device
program_hw_devices $device
refresh_hw_device $device

set is_programmed "UNAVAILABLE"
if {[lsearch -exact [list_property $device] "IS_PROGRAMMED"] >= 0} {
  set is_programmed [get_property IS_PROGRAMMED $device]
}

set status_line "JTAG_BITSTREAM_PROGRAMMED device=$device bitstream=$bit_file is_programmed=$is_programmed"
puts $status_line

if {$status_file ne ""} {
  set out_dir [file dirname $status_file]
  file mkdir $out_dir
  set fh [open $status_file w]
  puts $fh $status_line
  close $fh
}

close_hw_target
close_hw_manager