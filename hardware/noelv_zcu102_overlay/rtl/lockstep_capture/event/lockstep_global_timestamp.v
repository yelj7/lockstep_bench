/**********************************************************
* 文件名: lockstep_global_timestamp.v
* 日期: 2026-07-19
* 版本: 1.0
* 更新记录: 新增 sample clock 域 64-bit 自由运行时间基准。
* 描述: 仅由硬复位清零，跨 ARM、STOP 和 capture 空闲期保持单调。
**********************************************************/

`timescale 1ns/1ps

module lockstep_global_timestamp (
  clk,
  rst_n,
  timestamp_o
);
  parameter UDLY = 1;

  input         clk;
  input         rst_n;
  output [63:0] timestamp_o;

  reg [63:0] timestamp_r;

  assign timestamp_o = timestamp_r;

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      timestamp_r <= #UDLY 64'd0;
    end else begin
      timestamp_r <= #UDLY timestamp_r + 64'd1;
    end
  end
endmodule
