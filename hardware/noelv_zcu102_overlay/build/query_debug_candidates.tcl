# /**********************************************************
# * 文件名: query_debug_candidates.tcl
# * 日期: 2026-07-18
# * 版本: 1.0
# * 更新记录: 新增综合 DCP 调试网络和 FT601 fabric 时钟候选查询。
# * 描述: 只读枚举固定 ILA 探针候选、MARK_DEBUG 属性及寄存器时钟网络。
# **********************************************************/

if {$argc != 1} {
  puts stderr "用法: vivado -mode batch -source query_debug_candidates.tcl -tclargs <综合 DCP>"
  exit 2
}

open_checkpoint [file normalize [lindex $argv 0]]

proc report_register_group {label cell_pattern} {
  set cells [lsort -dictionary [get_cells -quiet -hier -filter "NAME =~ $cell_pattern && REF_NAME =~ FD*"]]
  set nets [list]
  foreach cell $cells {
    foreach pin [get_pins -quiet -of_objects $cell -filter {REF_PIN_NAME == Q}] {
      foreach net [get_nets -quiet -of_objects $pin] {
        lappend nets [get_property NAME $net]
      }
    }
  }
  set nets [lsort -unique -dictionary $nets]
  puts "REGISTER_GROUP: label=$label cells=[llength $cells] nets=[llength $nets]"
  foreach cell $cells {
    puts "REGISTER_CELL: label=$label name=[get_property NAME $cell] ref=[get_property REF_NAME $cell]"
  }
  foreach net $nets {
    puts "REGISTER_NET: label=$label name=$net"
  }
  return $cells
}

report_register_group device_state {*u_command_state_machine/device_state_r_reg*}
report_register_group capture_id {*u_command_state_machine/capture_id_r_reg*}
report_register_group parser_state {*u_rx_command_parser/*state*}
report_register_group command_state {*u_command_state_machine/FSM_onehot_cur_state_reg*}
report_register_group capture_state {*u_wide_capture_frame_source/*state*}
report_register_group wide_metadata {*u_wide_capture_window/meta_*_reg*}
report_register_group wide_samples_seen {*u_wide_capture_window/samples_seen_o_reg*}
set ft_state_cells [report_register_group ft601_state {*u_ft601_245_adapter/*state*}]

set ft_clock_nets [list]
foreach cell $ft_state_cells {
  foreach pin [get_pins -quiet -of_objects $cell -filter {REF_PIN_NAME == C}] {
    foreach net [get_nets -quiet -of_objects $pin] {
      lappend ft_clock_nets [get_property NAME $net]
    }
  }
}
set ft_clock_nets [lsort -unique -dictionary $ft_clock_nets]
puts "FT_FABRIC_CLOCK_COUNT: [llength $ft_clock_nets]"
foreach net $ft_clock_nets {
  puts "FT_FABRIC_CLOCK_NET: $net"
}

close_design
exit 0
