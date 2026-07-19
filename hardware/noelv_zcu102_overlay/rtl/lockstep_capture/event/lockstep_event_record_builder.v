/**********************************************************
* 文件名: lockstep_event_record_builder.v
* 日期: 2026-07-19
* 版本: 1.0
* 更新记录: 新增长时间协议事件固定记录打包器。
* 描述: 将事件字段组合为 64 字节小端字序记录。
**********************************************************/

`timescale 1ns/1ps

module lockstep_event_record_builder (
  timestamp_i,
  capture_id_i,
  local_sequence_i,
  protocol_id_i,
  event_type_i,
  source_kind_i,
  flags_i,
  event_reason_mask_i,
  payload_length_i,
  payload_i,
  record_o
);
  input  [63:0]  timestamp_i;
  input  [31:0]  capture_id_i;
  input  [31:0]  local_sequence_i;
  input  [7:0]   protocol_id_i;
  input  [7:0]   event_type_i;
  input  [7:0]   source_kind_i;
  input  [7:0]   flags_i;
  input  [8:0]   event_reason_mask_i;
  input  [31:0]  payload_length_i;
  input  [255:0] payload_i;
  output [511:0] record_o;

  assign record_o[63:0]    = timestamp_i;
  assign record_o[95:64]   = capture_id_i;
  assign record_o[127:96]  = local_sequence_i;
  assign record_o[135:128] = protocol_id_i;
  assign record_o[143:136] = event_type_i;
  assign record_o[151:144] = source_kind_i;
  assign record_o[159:152] = flags_i;
  assign record_o[191:160] = {23'd0, event_reason_mask_i};
  assign record_o[223:192] = payload_length_i;
  assign record_o[255:224] = 32'd0;
  assign record_o[511:256] = payload_i;

endmodule
