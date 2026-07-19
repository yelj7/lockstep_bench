# /**********************************************************
# * 文件名: fixed_probes.xdc
# * 日期: 2026-07-18
# * 版本: 1.1
# * 更新记录: 将循环约束展开为 Vivado XDC 支持的显式约束。
# * 描述: 在调试版综合期间保留 FT601 与采集恢复状态总线，供固定 ILA 使用。
# **********************************************************/

set_property MARK_DEBUG TRUE [get_nets -quiet -hier -regexp {.*debug_device_state_w(\[[0-9]+\])?}]
set_property MARK_DEBUG TRUE [get_nets -quiet -hier -regexp {.*debug_capture_id_w(\[[0-9]+\])?}]
set_property MARK_DEBUG TRUE [get_nets -quiet -hier -regexp {.*debug_parser_state_w(\[[0-9]+\])?}]
set_property MARK_DEBUG TRUE [get_nets -quiet -hier -regexp {.*debug_command_state_w(\[[0-9]+\])?}]
set_property MARK_DEBUG TRUE [get_nets -quiet -hier -regexp {.*debug_capture_state_w(\[[0-9]+\])?}]
set_property MARK_DEBUG TRUE [get_nets -quiet -hier -regexp {.*debug_wide_capture_state_w(\[[0-9]+\])?}]
set_property MARK_DEBUG TRUE [get_nets -quiet -hier -regexp {.*debug_wide_capture_metadata_w(\[[0-9]+\])?}]
set_property MARK_DEBUG TRUE [get_nets -quiet -hier -regexp {.*debug_wide_capture_samples_seen_w(\[[0-9]+\])?}]
set_property MARK_DEBUG TRUE [get_nets -quiet -hier -regexp {.*debug_ft601_state_w(\[[0-9]+\])?}]
