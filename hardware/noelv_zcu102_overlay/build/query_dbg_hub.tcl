# /**********************************************************
# * 文件名: query_dbg_hub.tcl
# * 日期: 2026-07-18
# * 版本: 1.0
# * 更新记录: 新增综合网表黑盒属性只读诊断。
# * 描述: 打开已完成的综合运行并输出黑盒单元的来源与属性。
# **********************************************************/

if {$argc != 1} {
  puts stderr "用法: vivado -mode batch -source query_dbg_hub.tcl -tclargs <工程文件>"
  exit 2
}

open_project [file normalize [lindex $argv 0]]
open_run synth_1
set blackboxes [get_cells -quiet -hier -filter {IS_BLACKBOX == 1}]
puts "BLACKBOX_COUNT: [llength $blackboxes]"
foreach cell $blackboxes {
  puts "BLACKBOX_BEGIN: $cell"
  foreach property_name {NAME REF_NAME ORIG_REF_NAME IS_BLACKBOX IS_PRIMITIVE DONT_TOUCH KEEP_HIERARCHY} {
    set property_value "<UNAVAILABLE>"
    catch {set property_value [get_property $property_name $cell]}
    puts "BLACKBOX_PROPERTY: $property_name=$property_value"
  }
  puts "BLACKBOX_END: $cell"
}
close_design
close_project
exit 0
