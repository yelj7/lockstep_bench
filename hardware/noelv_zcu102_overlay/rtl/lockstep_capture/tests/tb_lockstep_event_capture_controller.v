/**********************************************************
* 文件名: tb_lockstep_event_capture_controller.v
* 日期: 2026-07-19
* 版本: 1.4
* 更新记录: overflow 结束时必须暴露 DRAINING，直到外层 FIFO 排空完成。
* 描述: 验证 watchdog、硬超时、STOP、overflow 和 program_done 排空原因。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_event_capture_controller;
  reg clk;
  reg rst_n;
  reg capture_start;
  reg stop;
  reg program_done;
  reg overflow;
  reg activity;
  reg protocol_busy;
  reg [31:0] watchdog_ticks;
  reg [31:0] hard_timeout_ticks;
  wire capture_active;
  wire draining;
  wire capture_done_pulse;
  wire [31:0] end_reason;

  lockstep_event_capture_controller #(
    .QUIET_GUARD_TICKS(3)
  ) dut (
    .clk(clk), .rst_n(rst_n), .capture_start_i(capture_start),
    .stop_i(stop), .program_done_i(program_done), .activity_i(activity),
    .overflow_i(overflow),
    .protocol_busy_i(protocol_busy),
    .watchdog_ticks_i(watchdog_ticks), .hard_timeout_ticks_i(hard_timeout_ticks),
    .capture_active_o(capture_active), .draining_o(draining),
    .capture_done_pulse_o(capture_done_pulse), .end_reason_o(end_reason)
  );

  always #5 clk = !clk;

  task fail;
    input [1023:0] message;
    begin
      $display("FAIL time=%0t message=%0s", $time, message);
      $finish;
    end
  endtask

  task start_capture;
    begin
      capture_start = 1'b1;
      @(posedge clk);
      #2;
      capture_start = 1'b0;
    end
  endtask

  initial begin
    clk = 1'b0;
    rst_n = 1'b0;
    capture_start = 1'b0;
    stop = 1'b0;
    program_done = 1'b0;
    overflow = 1'b0;
    activity = 1'b0;
    protocol_busy = 1'b0;
    watchdog_ticks = 32'd4;
    hard_timeout_ticks = 32'd20;
    repeat (3) @(posedge clk);
    rst_n = 1'b1;

    start_capture;
    repeat (3) @(posedge clk);
    activity = 1'b1;
    @(posedge clk);
    #2;
    activity = 1'b0;
    repeat (3) @(posedge clk);
    if (!capture_active) fail("活动后 watchdog 提前结束");
    @(posedge clk);
    #2;
    if (capture_active || !capture_done_pulse || end_reason != 32'd2) fail("watchdog 结束原因错误");

    watchdog_ticks = 32'd0;
    hard_timeout_ticks = 32'd3;
    start_capture;
    repeat (3) @(posedge clk);
    #2;
    if (capture_active || !capture_done_pulse || end_reason != 32'd4) fail("硬超时结束原因错误");

    hard_timeout_ticks = 32'd100;
    start_capture;
    stop = 1'b1;
    @(posedge clk);
    #2;
    stop = 1'b0;
    if (capture_active || !capture_done_pulse || end_reason != 32'd1) fail("STOP 结束原因错误");

    start_capture;
    overflow = 1'b1;
    @(posedge clk);
    #2;
    overflow = 1'b0;
    if (capture_active || !draining || !capture_done_pulse || end_reason != 32'd3)
      fail("overflow 未立即进入 DRAINING 或原因错误");

    start_capture;
    program_done = 1'b1;
    @(posedge clk);
    #2;
    program_done = 1'b0;
    if (!capture_active || !draining || capture_done_pulse) fail("program_done 未进入 DRAINING");
    protocol_busy = 1'b1;
    repeat (4) @(posedge clk);
    #2;
    if (!capture_active || !draining || capture_done_pulse) fail("协议 busy 时 quiet guard 提前结束");
    protocol_busy = 1'b0;
    activity = 1'b1;
    repeat (2) @(posedge clk);
    #2;
    activity = 1'b0;
    if (!capture_active || !draining) fail("DRAINING 活动未重置 quiet guard");
    repeat (2) @(posedge clk);
    #2;
    if (!capture_active || !draining || capture_done_pulse) fail("quiet guard 提前结束");
    @(posedge clk);
    #2;
    if (capture_active || draining || !capture_done_pulse || end_reason != 32'd0)
      fail("program_done quiet guard 结束原因错误");

    $display("PASS tb_lockstep_event_capture_controller");
    $finish;
  end

endmodule
