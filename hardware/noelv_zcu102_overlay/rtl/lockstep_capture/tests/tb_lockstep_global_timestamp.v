/**********************************************************
* 文件名: tb_lockstep_global_timestamp.v
* 日期: 2026-07-19
* 版本: 1.1
* 更新记录: 改为验证外部绝对索引对齐、32 位回绕扩展和无效样本保持。
* 描述: 验证稀疏事件时间戳与连续窗口使用同一条全局采样时间轴。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_global_timestamp;
  localparam UDLY = 1;

  reg clk;
  reg rst_n;
  reg sample_valid;
  reg [31:0] sample_abs_index;
  wire [63:0] timestamp;

  lockstep_global_timestamp #(.UDLY(UDLY)) dut (
    .clk(clk),
    .rst_n(rst_n),
    .sample_valid_i(sample_valid),
    .sample_abs_index_i(sample_abs_index),
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
    sample_valid = 1'b0;
    sample_abs_index = 32'd0;

    repeat (3) @(posedge clk);
    rst_n = 1'b1;
    @(negedge clk);
    sample_valid = 1'b1;
    sample_abs_index = 32'h12345678;
    @(posedge clk);
    #2;
    if (timestamp != 64'h0000000012345678)
      fail("nonzero absolute index did not seed timestamp");

    @(negedge clk);
    sample_abs_index = 32'hfffffffe;
    @(posedge clk);
    #2;
    if (timestamp != 64'h00000000fffffffe)
      fail("timestamp did not follow pre-wrap absolute index");

    @(negedge clk);
    sample_abs_index = 32'hffffffff;
    @(posedge clk);
    #2;
    if (timestamp != 64'h00000000ffffffff)
      fail("timestamp lost final pre-wrap sample");

    @(negedge clk);
    sample_abs_index = 32'h00000000;
    @(posedge clk);
    #2;
    if (timestamp != 64'h0000000100000000)
      fail("timestamp epoch did not advance across 32-bit wrap");

    @(negedge clk);
    sample_abs_index = 32'h00000001;
    @(posedge clk);
    #2;
    if (timestamp != 64'h0000000100000001)
      fail("timestamp did not continue after wrap");

    @(negedge clk);
    sample_valid = 1'b0;
    sample_abs_index = 32'h00000055;
    repeat (3) @(posedge clk);
    #2;
    if (timestamp != 64'h0000000100000001)
      fail("invalid samples changed the global timestamp");

    rst_n = 1'b0;
    #2;
    if (timestamp != 64'd0) fail("hard reset did not clear timestamp");
    $display("PASS tb_lockstep_global_timestamp");
    $finish;
  end
endmodule
