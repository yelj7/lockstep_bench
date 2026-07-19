# /**********************************************************
# * 文件名: build_remote.tcl
# * 日期: 2026-07-17
# * 版本: 1.1
# * 更新记录: 精确允许综合阶段的 Xilinx dbg_hub 占位，并在优化后恢复零黑盒门禁。
# * 描述: 从干净 GRLIB/NOEL-V 源码工程生成可审计产物并执行实现后门禁。
# **********************************************************/

proc fail {message} {
  puts stderr "BUILD_GATE_ERROR: $message"
  exit 2
}

proc require_zero_legacy_names {} {
  set old_cells [concat \
    [get_cells -quiet -hier -regexp -nocase {.*zla.*}] \
    [get_cells -quiet -hier -regexp -nocase {.*stage1f.*}]]
  set old_nets [concat \
    [get_nets -quiet -hier -regexp -nocase {.*zla.*}] \
    [get_nets -quiet -hier -regexp -nocase {.*stage1f.*}]]
  if {[llength $old_cells] != 0 || [llength $old_nets] != 0} {
    fail "旧命名 zla|stage1f 非零命中"
  }
}

proc require_netlist_blackboxes {context allow_dbg_hub_placeholder} {
  set blackboxes [get_cells -quiet -hier -filter {IS_BLACKBOX == 1}]
  if {$allow_dbg_hub_placeholder} {
    if {[llength $blackboxes] != 1} {
      puts stderr [join $blackboxes "\n"]
      fail "$context 黑盒数量不是预期的 1: [llength $blackboxes]"
    }
    set cell [lindex $blackboxes 0]
    set name [get_property NAME $cell]
    set ref_name [get_property REF_NAME $cell]
    set orig_ref_name [get_property ORIG_REF_NAME $cell]
    set dont_touch [get_property DONT_TOUCH $cell]
    set is_primitive [get_property IS_PRIMITIVE $cell]
    if {$name ne "dbg_hub" ||
        $ref_name ne "dbg_hub_CV" ||
        $orig_ref_name ne "dbg_hub_CV" ||
        $dont_touch ne "1" ||
        $is_primitive ne "0"} {
      fail "$context 黑盒不是已确认的 Xilinx dbg_hub 占位: name=$name ref=$ref_name orig_ref=$orig_ref_name dont_touch=$dont_touch primitive=$is_primitive"
    }
    puts "BLACKBOX_GATE_PASS: context=$context name=$name ref=$ref_name orig_ref=$orig_ref_name"
  } elseif {[llength $blackboxes] != 0} {
    puts stderr [join $blackboxes "\n"]
    fail "$context 包含实现后黑盒"
  }
  require_zero_legacy_names
}

proc write_reports {out_dir label} {
  report_timing_summary -delay_type min_max -report_unconstrained -check_timing_verbose \
    -max_paths 100 -file [file join $out_dir "${label}_timing_summary.rpt"]
  report_utilization -hierarchical -file [file join $out_dir "${label}_utilization.rpt"]
  report_drc -ruledecks default -file [file join $out_dir "${label}_drc.rpt"]
  report_clock_interaction -delay_type min_max -file [file join $out_dir "${label}_clock_interaction.rpt"]
  report_cdc -details -file [file join $out_dir "${label}_cdc.rpt"]
  report_route_status -file [file join $out_dir "${label}_route_status.rpt"]
}

proc require_implementation_gate {label} {
  set unrouted [concat \
    [get_nets -quiet -hier -filter {ROUTE_STATUS == UNROUTED}] \
    [get_nets -quiet -hier -filter {ROUTE_STATUS == PARTIAL}]]
  if {[llength $unrouted] != 0} {
    fail "$label 存在 [llength $unrouted] 个未完整路由网络"
  }

  report_drc -name gate_drc -ruledecks default -quiet
  set severe_drc [get_drc_violations -quiet -filter {SEVERITY == Error || SEVERITY == {Critical Warning}}]
  if {[llength $severe_drc] != 0} {
    puts stderr [join $severe_drc "\n"]
    fail "$label 存在 Error 或 Critical Warning DRC"
  }

  set setup_path [get_timing_paths -quiet -delay_type max -max_paths 1]
  set hold_path [get_timing_paths -quiet -delay_type min -max_paths 1]
  if {[llength $setup_path] == 0 || [llength $hold_path] == 0} {
    fail "$label 无法取得 setup/hold 时序路径"
  }
  set wns [get_property SLACK $setup_path]
  set whs [get_property SLACK $hold_path]
  puts "BUILD_GATE_TIMING: label=$label WNS=$wns WHS=$whs"
  if {$wns < 0.0 || $whs < 0.0} {
    fail "$label 时序未收敛: WNS=$wns WHS=$whs"
  }
}

proc get_register_nets {probe_name cell_pattern} {
  set cells [lsort -dictionary [get_cells -quiet -hier -filter "NAME =~ $cell_pattern && REF_NAME =~ FD*"]]
  set probe_nets [list]
  foreach cell $cells {
    foreach pin [get_pins -quiet -of_objects $cell -filter {REF_PIN_NAME == Q}] {
      foreach net [get_nets -quiet -of_objects $pin] {
        lappend probe_nets $net
      }
    }
  }
  set probe_nets [lsort -unique -dictionary $probe_nets]
  if {[llength $probe_nets] == 0} {
    fail "固定 ILA 探针 $probe_name 未匹配到寄存器网络: $cell_pattern"
  }
  return $probe_nets
}

proc add_fixed_ila {} {
  set specs [list \
    device_state {*u_command_state_machine/device_state_r_reg*} \
    capture_id {*u_command_state_machine/capture_id_r_reg*} \
    parser_state {*u_rx_command_parser/*state*} \
    command_state {*u_command_state_machine/FSM_onehot_cur_state_reg*} \
    capture_state {*u_wide_capture_frame_source/*state*} \
    wide_capture_state {*u_wide_capture_window/sample_state_r_reg*} \
    wide_capture_metadata {*u_wide_capture_window/meta_*_reg*} \
    wide_samples_seen {*u_wide_capture_window/samples_seen_o_reg*} \
    ft601_state {*u_ft601_245_adapter/*state*}]

  set ft_state_cells [get_cells -quiet -hier -filter {NAME =~ *u_ft601_245_adapter/*state* && REF_NAME =~ FD*}]
  set clock_nets [list]
  foreach cell $ft_state_cells {
    foreach pin [get_pins -quiet -of_objects $cell -filter {REF_PIN_NAME == C}] {
      foreach net [get_nets -quiet -of_objects $pin] {
        lappend clock_nets $net
      }
    }
  }
  set clock_nets [lsort -unique -dictionary $clock_nets]
  if {[llength $clock_nets] != 1} {
    fail "固定 ILA 的 FT601 fabric 时钟匹配数量不是 1: [llength $clock_nets]"
  }
  puts "FIXED_ILA_CLOCK: [get_property NAME [lindex $clock_nets 0]]"

  create_debug_core lockstep_capture_ila ila
  set_property C_DATA_DEPTH 4096 [get_debug_cores lockstep_capture_ila]
  set_property C_INPUT_PIPE_STAGES 1 [get_debug_cores lockstep_capture_ila]
  set_property port_width 1 [get_debug_ports lockstep_capture_ila/clk]
  connect_debug_port lockstep_capture_ila/clk $clock_nets

  set probe_index 0
  foreach {probe_name cell_pattern} $specs {
    set probe_nets [get_register_nets $probe_name $cell_pattern]
    if {$probe_index > 0} {
      create_debug_port lockstep_capture_ila probe
    }
    set probe_port [get_debug_ports "lockstep_capture_ila/probe${probe_index}"]
    set_property port_width [llength $probe_nets] $probe_port
    connect_debug_port $probe_port $probe_nets
    puts "FIXED_ILA_PROBE: index=$probe_index name=$probe_name width=[llength $probe_nets]"
    incr probe_index
  }
}

proc implement_variant {synth_dcp out_dir label with_ila} {
  file mkdir $out_dir
  open_checkpoint $synth_dcp
  require_netlist_blackboxes "${label} 优化前" 1
  if {$with_ila} {
    add_fixed_ila
  }
  opt_design -directive Explore
  require_netlist_blackboxes "${label} 优化后" 0
  place_design -directive ExtraNetDelay_high
  phys_opt_design -directive AggressiveExplore
  route_design -directive Explore
  phys_opt_design -directive AggressiveExplore
  write_reports $out_dir $label
  require_implementation_gate $label
  write_checkpoint -force [file join $out_dir "${label}_routed.dcp"]
  if {$with_ila} {
    write_debug_probes -force [file join $out_dir noelvmp_zcu102_1024_debug.ltx]
    write_bitstream -force [file join $out_dir noelvmp_zcu102_1024_debug.bit]
  } else {
    write_bitstream -force [file join $out_dir noelvmp_zcu102_1024.bit]
  }
  close_design
}

if {$argc != 3 && $argc != 4} {
  fail "用法: vivado -mode batch -source build_remote.tcl -tclargs <source_root> <output_root> <source_digest> ?resume_implementation?"
}

set source_root [file normalize [lindex $argv 0]]
set output_root [file normalize [lindex $argv 1]]
set source_digest [lindex $argv 2]
set resume_implementation [expr {$argc == 4 && [lindex $argv 3] eq "resume_implementation"}]
if {$argc == 4 && !$resume_implementation} {
  fail "未知构建模式: [lindex $argv 3]"
}
set digest_file [file normalize [file join [file dirname [info script]] .. manifests source_digest.txt]]
if {![file exists $digest_file]} {
  fail "source digest 文件不存在: $digest_file"
}
set digest_handle [open $digest_file r]
set expected_digest [string trim [read $digest_handle]]
close $digest_handle
if {$source_digest ne $expected_digest} {
  fail "source digest 不一致: expected=$expected_digest actual=$source_digest"
}

file mkdir $output_root
file mkdir [file join $output_root common]
set synth_dcp [file join $output_root common noelvmp_synth.dcp]

if {!$resume_implementation} {
  set design_dir [file join $source_root designs noelv-xilinx-zcu102]
  set project_script [file join $design_dir vivado noelvmp_vivado.tcl]
  set fixed_probe_xdc [file normalize [file join [file dirname [info script]] fixed_probes.xdc]]
  if {![file exists $project_script] || ![file exists $fixed_probe_xdc]} {
    fail "工程生成脚本或固定探针约束缺失"
  }

  cd $design_dir

  # 只执行工程创建和 IP 生成部分，明确跳过旧脚本末尾的综合、实现与比特流命令。
  set input_handle [open $project_script r]
  set project_prefix ""
  while {[gets $input_handle line] >= 0} {
    if {[regexp {^[[:space:]]*synth_design[[:space:]]} $line]} {
      break
    }
    append project_prefix $line "\n"
  }
  close $input_handle
  if {$project_prefix eq ""} {
    fail "无法提取干净工程创建脚本"
  }
  eval $project_prefix

  set expected_part xczu9eg-ffvb1156-2-e
  set actual_part [get_property PART [current_project]]
  if {$actual_part ne $expected_part} {
    fail "器件型号不一致: expected=$expected_part actual=$actual_part"
  }
  puts "BUILD_IDENTITY: vivado=[version -short] part=$actual_part source_digest=$source_digest"

  set synth_run [get_runs synth_1]
  set synth_more_options [get_property STEPS.SYNTH_DESIGN.ARGS.MORE_OPTIONS $synth_run]
  if {[regexp -nocase {out_of_context} $synth_more_options]} {
    fail "synth_1 意外包含 out_of_context: $synth_more_options"
  }
  add_files -fileset constrs_1 -norecurse $fixed_probe_xdc
  set_property USED_IN_SYNTHESIS true [get_files $fixed_probe_xdc]
  set_property USED_IN_IMPLEMENTATION false [get_files $fixed_probe_xdc]
  update_compile_order -fileset sources_1
  launch_runs synth_1 -jobs 12
  wait_on_run synth_1
  set synth_status [get_property STATUS $synth_run]
  puts "SYNTH_RUN_STATUS: $synth_status"
  if {![string match -nocase "*Complete*" $synth_status]} {
    fail "顶层综合失败: $synth_status"
  }

  open_run synth_1
  require_netlist_blackboxes "普通顶层综合" 1
  write_checkpoint -force $synth_dcp
  report_utilization -hierarchical -file [file join $output_root common synth_utilization.rpt]
  close_design
  close_project
} else {
  if {![file exists $synth_dcp]} {
    fail "续跑综合 DCP 不存在: $synth_dcp"
  }
  puts "RESUME_IMPLEMENTATION: vivado=[version -short] synth_dcp=$synth_dcp"
}

# 固定探针调试版必须先于无 ILA 发布版生成。
implement_variant $synth_dcp [file join $output_root debug] noelvmp_zcu102_1024_debug 1
implement_variant $synth_dcp [file join $output_root release] noelvmp_zcu102_1024 0
puts "BUILD_GATE_PASS: source_digest=$source_digest"
exit 0
