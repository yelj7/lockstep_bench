/**********************************************************
* 文件名: lockstep_frame_request_arbiter.v
* 日期: 2026-06-03
* 版本: 0.2
* 更新记录:
*   0.1 初始版：采集优先的帧请求仲裁器。
*   0.2 添加注释。
* 描述: LOCKSTEP 帧请求仲裁器——采集流（cap）与命令响应（cmd）两路帧请求
*       之间的固定优先级仲裁，命令响应优先。纯组合逻辑通路，无 FSM、
*       无流水线延迟。
**********************************************************/

`timescale 1ns/1ps

module lockstep_frame_request_arbiter (
  clk,
  rst_n,
  cmd_frame_valid_i,
  cmd_frame_ready_o,
  cmd_frame_type_i,
  cmd_frame_capture_id_i,
  cmd_frame_flags_i,
  cmd_payload_word_count_i,
  cmd_payload0_i,
  cmd_payload1_i,
  cmd_payload2_i,
  cmd_payload3_i,
  cmd_payload4_i,
  cmd_payload5_i,
  cmd_payload6_i,
  cmd_payload7_i,
  cmd_payload8_i,
  cmd_payload9_i,
  cmd_payload10_i,
  cmd_payload11_i,
  cmd_payload12_i,
  cmd_payload13_i,
  cmd_payload14_i,
  cmd_payload15_i,
  cap_frame_valid_i,
  cap_frame_ready_o,
  cap_frame_type_i,
  cap_frame_capture_id_i,
  cap_frame_flags_i,
  cap_payload_word_count_i,
  cap_payload0_i,
  cap_payload1_i,
  cap_payload2_i,
  cap_payload3_i,
  cap_payload4_i,
  cap_payload5_i,
  cap_payload6_i,
  cap_payload7_i,
  cap_payload8_i,
  cap_payload9_i,
  cap_payload10_i,
  cap_payload11_i,
  cap_payload12_i,
  cap_payload13_i,
  cap_payload14_i,
  cap_payload15_i,
  frame_valid_o,
  frame_ready_i,
  frame_type_o,
  frame_capture_id_o,
  frame_flags_o,
  payload_word_count_o,
  payload0_o,
  payload1_o,
  payload2_o,
  payload3_o,
  payload4_o,
  payload5_o,
  payload6_o,
  payload7_o,
  payload8_o,
  payload9_o,
  payload10_o,
  payload11_o,
  payload12_o,
  payload13_o,
  payload14_o,
  payload15_o,
  debug_grant_o
);
  parameter UDLY = 1;

  input         clk;
  input         rst_n;
  input         cmd_frame_valid_i;
  output        cmd_frame_ready_o;
  input  [15:0] cmd_frame_type_i;
  input  [31:0] cmd_frame_capture_id_i;
  input  [31:0] cmd_frame_flags_i;
  input  [31:0] cmd_payload_word_count_i;
  input  [31:0] cmd_payload0_i;
  input  [31:0] cmd_payload1_i;
  input  [31:0] cmd_payload2_i;
  input  [31:0] cmd_payload3_i;
  input  [31:0] cmd_payload4_i;
  input  [31:0] cmd_payload5_i;
  input  [31:0] cmd_payload6_i;
  input  [31:0] cmd_payload7_i;
  input  [31:0] cmd_payload8_i;
  input  [31:0] cmd_payload9_i;
  input  [31:0] cmd_payload10_i;
  input  [31:0] cmd_payload11_i;
  input  [31:0] cmd_payload12_i;
  input  [31:0] cmd_payload13_i;
  input  [31:0] cmd_payload14_i;
  input  [31:0] cmd_payload15_i;
  input         cap_frame_valid_i;
  output        cap_frame_ready_o;
  input  [15:0] cap_frame_type_i;
  input  [31:0] cap_frame_capture_id_i;
  input  [31:0] cap_frame_flags_i;
  input  [31:0] cap_payload_word_count_i;
  input  [31:0] cap_payload0_i;
  input  [31:0] cap_payload1_i;
  input  [31:0] cap_payload2_i;
  input  [31:0] cap_payload3_i;
  input  [31:0] cap_payload4_i;
  input  [31:0] cap_payload5_i;
  input  [31:0] cap_payload6_i;
  input  [31:0] cap_payload7_i;
  input  [31:0] cap_payload8_i;
  input  [31:0] cap_payload9_i;
  input  [31:0] cap_payload10_i;
  input  [31:0] cap_payload11_i;
  input  [31:0] cap_payload12_i;
  input  [31:0] cap_payload13_i;
  input  [31:0] cap_payload14_i;
  input  [31:0] cap_payload15_i;
  output        frame_valid_o;
  input         frame_ready_i;
  output [15:0] frame_type_o;
  output [31:0] frame_capture_id_o;
  output [31:0] frame_flags_o;
  output [31:0] payload_word_count_o;
  output [31:0] payload0_o;
  output [31:0] payload1_o;
  output [31:0] payload2_o;
  output [31:0] payload3_o;
  output [31:0] payload4_o;
  output [31:0] payload5_o;
  output [31:0] payload6_o;
  output [31:0] payload7_o;
  output [31:0] payload8_o;
  output [31:0] payload9_o;
  output [31:0] payload10_o;
  output [31:0] payload11_o;
  output [31:0] payload12_o;
  output [31:0] payload13_o;
  output [31:0] payload14_o;
  output [31:0] payload15_o;
  output [31:0] debug_grant_o;

  // 授权信号：命令响应固定优先，避免 ARM/STATUS 被事件流饿死。
  wire cap_grant_w;
  wire cmd_grant_w;

  assign cmd_grant_w = cmd_frame_valid_i;
  assign cap_grant_w = !cmd_frame_valid_i && cap_frame_valid_i;

  // 反压就绪：各自授权且下游就绪时拉高
  assign cap_frame_ready_o = cap_grant_w && frame_ready_i;
  assign cmd_frame_ready_o = cmd_grant_w && frame_ready_i;

  // 输出有效：任意一路有请求即有效
  assign frame_valid_o = cap_frame_valid_i || cmd_frame_valid_i;

  // 帧头与 payload 多路选择：采集优先，命令兜底
  assign frame_type_o        = cap_grant_w ? cap_frame_type_i        : cmd_frame_type_i;
  assign frame_capture_id_o  = cap_grant_w ? cap_frame_capture_id_i  : cmd_frame_capture_id_i;
  assign frame_flags_o       = cap_grant_w ? cap_frame_flags_i       : cmd_frame_flags_i;
  assign payload_word_count_o = cap_grant_w ? cap_payload_word_count_i : cmd_payload_word_count_i;

  assign payload0_o  = cap_grant_w ? cap_payload0_i  : cmd_payload0_i;
  assign payload1_o  = cap_grant_w ? cap_payload1_i  : cmd_payload1_i;
  assign payload2_o  = cap_grant_w ? cap_payload2_i  : cmd_payload2_i;
  assign payload3_o  = cap_grant_w ? cap_payload3_i  : cmd_payload3_i;
  assign payload4_o  = cap_grant_w ? cap_payload4_i  : cmd_payload4_i;
  assign payload5_o  = cap_grant_w ? cap_payload5_i  : cmd_payload5_i;
  assign payload6_o  = cap_grant_w ? cap_payload6_i  : cmd_payload6_i;
  assign payload7_o  = cap_grant_w ? cap_payload7_i  : cmd_payload7_i;
  assign payload8_o  = cap_grant_w ? cap_payload8_i  : cmd_payload8_i;
  assign payload9_o  = cap_grant_w ? cap_payload9_i  : cmd_payload9_i;
  assign payload10_o = cap_grant_w ? cap_payload10_i : cmd_payload10_i;
  assign payload11_o = cap_grant_w ? cap_payload11_i : cmd_payload11_i;
  assign payload12_o = cap_grant_w ? cap_payload12_i : cmd_payload12_i;
  assign payload13_o = cap_grant_w ? cap_payload13_i : cmd_payload13_i;
  assign payload14_o = cap_grant_w ? cap_payload14_i : cmd_payload14_i;
  assign payload15_o = cap_grant_w ? cap_payload15_i : cmd_payload15_i;

  // 调试：bit0=采集授权, bit1=命令授权
  assign debug_grant_o = {30'd0, cmd_grant_w, cap_grant_w};

endmodule
