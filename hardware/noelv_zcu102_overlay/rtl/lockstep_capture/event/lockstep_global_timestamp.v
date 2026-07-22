/**********************************************************
* 文件名: lockstep_global_timestamp.v
* 日期: 2026-07-19
* 版本: 1.1
* 更新记录: 改为扩展外部绝对采样索引，消除稀疏事件与连续窗口的周期偏移。
* 描述: 将当前有效样本的 32 位绝对索引扩展为跨回绕单调的 64 位时间戳。
**********************************************************/

`timescale 1ns/1ps

module lockstep_global_timestamp (
  clk,
  rst_n,
  sample_valid_i,
  sample_abs_index_i,
  timestamp_o
);
  parameter UDLY = 1;

  input         clk;
  input         rst_n;
  input         sample_valid_i;
  input  [31:0] sample_abs_index_i;
  output [63:0] timestamp_o;

  reg [31:0] epoch_r;
  reg [31:0] last_index_r;
  reg [63:0] last_timestamp_r;
  reg        index_valid_r;
  wire       wrap_w;
  wire [31:0] current_epoch_w;
  wire [63:0] current_timestamp_w;

  assign wrap_w = sample_valid_i && index_valid_r &&
                  (sample_abs_index_i < last_index_r);
  assign current_epoch_w = epoch_r + (wrap_w ? 32'd1 : 32'd0);
  assign current_timestamp_w = {current_epoch_w, sample_abs_index_i};
  assign timestamp_o = sample_valid_i ? current_timestamp_w : last_timestamp_r;

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      epoch_r <= #UDLY 32'd0;
      last_index_r <= #UDLY 32'd0;
      last_timestamp_r <= #UDLY 64'd0;
      index_valid_r <= #UDLY 1'b0;
    end else if (sample_valid_i) begin
      epoch_r <= #UDLY current_epoch_w;
      last_index_r <= #UDLY sample_abs_index_i;
      last_timestamp_r <= #UDLY current_timestamp_w;
      index_valid_r <= #UDLY 1'b1;
    end
  end
endmodule
