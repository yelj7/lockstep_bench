/**********************************************************
* 文件名: tb_lockstep_capture_stream_arbiter.v
* 日期: 2026-07-19
* 版本: 1.0
* 更新记录: 新增 EVENT/SAMPLE 帧级公平性与回退路径回归。
* 描述: 验证双流持续请求时严格交替，单流请求时保持可前进。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_capture_stream_arbiter;
  reg clk;
  reg rst_n;
  reg event_valid;
  reg wide_valid;
  reg downstream_ready;
  wire event_ready;
  wire wide_ready;
  wire stream_valid;
  wire event_select;
  integer index;

  lockstep_capture_stream_arbiter dut (
    .clk(clk), .rst_n(rst_n), .event_valid_i(event_valid),
    .event_ready_o(event_ready), .wide_valid_i(wide_valid),
    .wide_ready_o(wide_ready), .downstream_ready_i(downstream_ready),
    .stream_valid_o(stream_valid), .event_select_o(event_select)
  );

  always #5 clk = !clk;

  task fail;
    input [1023:0] message;
    begin
      $display("FAIL time=%0t message=%0s", $time, message);
      $finish;
    end
  endtask

  initial begin
    clk = 1'b0;
    rst_n = 1'b0;
    event_valid = 1'b0;
    wide_valid = 1'b0;
    downstream_ready = 1'b0;
    repeat (3) @(posedge clk);
    rst_n = 1'b1;
    event_valid = 1'b1;
    wide_valid = 1'b1;
    downstream_ready = 1'b1;

    for (index = 0; index < 6; index = index + 1) begin
      @(negedge clk);
      if (!stream_valid || event_ready == wide_ready) fail("双流仲裁授权必须 one-hot");
      if (event_select != ((index % 2) == 0)) fail("双流持续请求未按帧交替");
    end

    event_valid = 1'b0;
    repeat (2) begin
      @(negedge clk);
      if (!wide_ready || event_ready || event_select) fail("仅窗口流有效时未直接授权");
    end
    event_valid = 1'b1;
    wide_valid = 1'b0;
    repeat (2) begin
      @(negedge clk);
      if (!event_ready || wide_ready || !event_select) fail("仅事件流有效时未直接授权");
    end

    downstream_ready = 1'b0;
    @(negedge clk);
    if (event_ready || wide_ready || !stream_valid) fail("下游反压传播错误");

    $display("PASS tb_lockstep_capture_stream_arbiter");
    $finish;
  end
endmodule
