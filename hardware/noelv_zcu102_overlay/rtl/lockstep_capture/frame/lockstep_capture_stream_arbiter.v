/**********************************************************
* 文件名: lockstep_capture_stream_arbiter.v
* 日期: 2026-07-19
* 版本: 1.0
* 更新记录: 新增连续窗口与稀疏事件的帧级轮询仲裁。
* 描述: 在完整帧请求边界交替服务 EVENT 与 SAMPLE，避免任一路饿死。
**********************************************************/

`timescale 1ns/1ps

module lockstep_capture_stream_arbiter (
  clk,
  rst_n,
  event_valid_i,
  event_ready_o,
  wide_valid_i,
  wide_ready_o,
  downstream_ready_i,
  stream_valid_o,
  event_select_o
);
  parameter UDLY = 1;

  input  clk;
  input  rst_n;
  input  event_valid_i;
  output event_ready_o;
  input  wide_valid_i;
  output wide_ready_o;
  input  downstream_ready_i;
  output stream_valid_o;
  output event_select_o;

  reg prefer_event_r;
  wire event_grant_w;
  wire wide_grant_w;
  wire stream_fire_w;

  assign event_grant_w = event_valid_i && (!wide_valid_i || prefer_event_r);
  assign wide_grant_w = wide_valid_i && (!event_valid_i || !prefer_event_r);
  assign event_ready_o = downstream_ready_i && event_grant_w;
  assign wide_ready_o = downstream_ready_i && wide_grant_w;
  assign stream_valid_o = event_valid_i || wide_valid_i;
  assign event_select_o = event_grant_w;
  assign stream_fire_w = downstream_ready_i && stream_valid_o;

  // 双路同时有效时，在每个完整帧请求握手后把优先权交给另一条流。
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      prefer_event_r <= #UDLY 1'b1;
    end else if (stream_fire_w) begin
      prefer_event_r <= #UDLY wide_grant_w;
    end
  end

endmodule
