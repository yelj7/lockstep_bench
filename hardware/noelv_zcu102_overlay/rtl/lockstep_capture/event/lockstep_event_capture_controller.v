/**********************************************************
* 文件名: lockstep_event_capture_controller.v
* 日期: 2026-07-19
* 版本: 1.4
* 更新记录: 首次事件溢出立即结束采集并保留 overflow 原因。
* 描述: 从全局触发开始计时，按 program_done、STOP、watchdog 或硬超时结束。
**********************************************************/

`timescale 1ns/1ps

module lockstep_event_capture_controller (
  clk,
  rst_n,
  capture_start_i,
  stop_i,
  program_done_i,
  overflow_i,
  activity_i,
  protocol_busy_i,
  watchdog_ticks_i,
  hard_timeout_ticks_i,
  capture_active_o,
  draining_o,
  capture_done_pulse_o,
  end_reason_o
);
  parameter UDLY = 1;
  parameter integer QUIET_GUARD_TICKS = 120000;

  input         clk;
  input         rst_n;
  input         capture_start_i;
  input         stop_i;
  input         program_done_i;
  input         overflow_i;
  input         activity_i;
  input         protocol_busy_i;
  input  [31:0] watchdog_ticks_i;
  input  [31:0] hard_timeout_ticks_i;
  output        capture_active_o;
  output        draining_o;
  output        capture_done_pulse_o;
  output [31:0] end_reason_o;

  reg capture_active_r;
  reg draining_r;
  reg capture_done_pulse_r;
  reg [31:0] idle_ticks_r;
  reg [31:0] elapsed_ticks_r;
  reg [31:0] quiet_ticks_r;
  reg [31:0] end_reason_r;
  wire watchdog_fire_w;
  wire hard_timeout_fire_w;
  wire capture_activity_w;

  assign capture_active_o = capture_active_r;
  assign draining_o = draining_r;
  assign capture_done_pulse_o = capture_done_pulse_r;
  assign end_reason_o = end_reason_r;
  assign capture_activity_w = activity_i || protocol_busy_i;
  assign watchdog_fire_w = capture_active_r && (watchdog_ticks_i != 32'd0) &&
                           !capture_activity_w &&
                           (idle_ticks_r >= (watchdog_ticks_i - 32'd1));
  assign hard_timeout_fire_w = capture_active_r && (hard_timeout_ticks_i != 32'd0) &&
                               (elapsed_ticks_r >= (hard_timeout_ticks_i - 32'd1));

  // 事件生命周期、超时和 program_done 静默排空保护。
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      capture_active_r <= #UDLY 1'b0;
      draining_r <= #UDLY 1'b0;
      capture_done_pulse_r <= #UDLY 1'b0;
      idle_ticks_r <= #UDLY 32'd0;
      elapsed_ticks_r <= #UDLY 32'd0;
      quiet_ticks_r <= #UDLY 32'd0;
      end_reason_r <= #UDLY 32'd0;
    end else begin
      capture_done_pulse_r <= #UDLY 1'b0;
      if (capture_start_i) begin
        capture_active_r <= #UDLY 1'b1;
        draining_r <= #UDLY 1'b0;
        idle_ticks_r <= #UDLY 32'd0;
        elapsed_ticks_r <= #UDLY 32'd0;
        quiet_ticks_r <= #UDLY 32'd0;
        end_reason_r <= #UDLY 32'd0;
      end else if (capture_active_r) begin
        elapsed_ticks_r <= #UDLY elapsed_ticks_r + 32'd1;

        if (stop_i) begin
          capture_active_r <= #UDLY 1'b0;
          draining_r <= #UDLY 1'b0;
          capture_done_pulse_r <= #UDLY 1'b1;
          end_reason_r <= #UDLY 32'd1;
        end else if (overflow_i) begin
          capture_active_r <= #UDLY 1'b0;
          draining_r <= #UDLY 1'b1;
          capture_done_pulse_r <= #UDLY 1'b1;
          end_reason_r <= #UDLY 32'd3;
        end else if (hard_timeout_fire_w) begin
          capture_active_r <= #UDLY 1'b0;
          draining_r <= #UDLY 1'b0;
          capture_done_pulse_r <= #UDLY 1'b1;
          end_reason_r <= #UDLY 32'd4;
        end else if (draining_r) begin
          if (capture_activity_w) begin
            quiet_ticks_r <= #UDLY 32'd0;
          end else if (quiet_ticks_r >= (QUIET_GUARD_TICKS - 1)) begin
            capture_active_r <= #UDLY 1'b0;
            draining_r <= #UDLY 1'b0;
            capture_done_pulse_r <= #UDLY 1'b1;
          end else begin
            quiet_ticks_r <= #UDLY quiet_ticks_r + 32'd1;
          end
        end else begin
          if (capture_activity_w) begin
            idle_ticks_r <= #UDLY 32'd0;
          end else begin
            idle_ticks_r <= #UDLY idle_ticks_r + 32'd1;
          end
          if (program_done_i) begin
            draining_r <= #UDLY 1'b1;
            quiet_ticks_r <= #UDLY 32'd0;
            end_reason_r <= #UDLY 32'd0;
          end else if (watchdog_fire_w) begin
            capture_active_r <= #UDLY 1'b0;
            capture_done_pulse_r <= #UDLY 1'b1;
            end_reason_r <= #UDLY 32'd2;
          end
        end
      end
    end
  end

endmodule
