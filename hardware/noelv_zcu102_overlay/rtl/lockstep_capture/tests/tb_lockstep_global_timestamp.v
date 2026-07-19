/**********************************************************
* 文件名: tb_lockstep_global_timestamp.v
* 日期: 2026-07-19
* 版本: 1.0
* 更新记录: 新增跨采集空闲期的全局时间戳回归。
* 描述: 验证时间基准仅由硬复位清零，并在模拟 ARM/STOP 间隔持续递增。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_global_timestamp;
  localparam UDLY = 1;

  reg clk;
  reg rst_n;
  reg capture_active;
  wire [63:0] timestamp;
  reg [63:0] first_capture_timestamp;
  reg [63:0] second_capture_timestamp;

  lockstep_global_timestamp #(.UDLY(UDLY)) dut (
    .clk(clk),
    .rst_n(rst_n),
    .timestamp_o(timestamp)
  );

  always #5 clk = ~clk;

  task fail;
    input [8*80-1:0] message;
    begin
      $display("FAIL %0s", message);
      $finish;
    end
  endtask

  initial begin
    clk = 1'b0;
    rst_n = 1'b0;
    capture_active = 1'b0;
    first_capture_timestamp = 64'd0;
    second_capture_timestamp = 64'd0;

    repeat (3) @(posedge clk);
    rst_n = 1'b1;
    repeat (5) @(posedge clk);
    #2;
    capture_active = 1'b1;
    first_capture_timestamp = timestamp;
    repeat (4) @(posedge clk);
    capture_active = 1'b0;
    repeat (11) @(posedge clk);
    capture_active = 1'b1;
    #2;
    second_capture_timestamp = timestamp;

    if (first_capture_timestamp == 64'd0) fail("first capture timestamp did not advance");
    if (second_capture_timestamp <= first_capture_timestamp + 64'd11)
      fail("timestamp did not advance through capture idle interval");

    rst_n = 1'b0;
    #2;
    if (timestamp != 64'd0) fail("hard reset did not clear timestamp");
    $display("PASS tb_lockstep_global_timestamp");
    $finish;
  end

  wire unused_capture_active_w = capture_active;
endmodule
