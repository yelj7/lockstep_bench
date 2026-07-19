/**********************************************************
* 文件名: lockstep_event_capture_core.v
* 日期: 2026-07-19
* 版本: 1.0
* 更新记录: 新增九协议独立 FIFO、统计和公平输出核心。
* 描述: 接收九路事件脉冲，分别缓存并通过轮询接口输出固定事件记录。
**********************************************************/

`timescale 1ns/1ps

module lockstep_event_capture_core (
  clk,
  rst_n,
  capture_start_i,
  source_enable_mask_i,
  source_push_i,
  source_record_i,
  source_accept_o,
  source_drop_o,
  event_valid_o,
  event_ready_i,
  event_record_o,
  event_source_o,
  overflow_mask_o,
  accepted_count_o,
  emitted_count_o,
  dropped_count_o
);
  parameter UDLY = 1;
  parameter integer DATA_WIDTH = 512;
  parameter integer FIFO_DEPTH = 8;
  parameter integer FIFO_ADDR_WIDTH = 3;

  input                     clk;
  input                     rst_n;
  input                     capture_start_i;
  input  [8:0]              source_enable_mask_i;
  input  [8:0]              source_push_i;
  input  [9*DATA_WIDTH-1:0] source_record_i;
  output [8:0]              source_accept_o;
  output [8:0]              source_drop_o;
  output                    event_valid_o;
  input                     event_ready_i;
  output [DATA_WIDTH-1:0]   event_record_o;
  output [3:0]              event_source_o;
  output [8:0]              overflow_mask_o;
  output [9*32-1:0]         accepted_count_o;
  output [9*32-1:0]         emitted_count_o;
  output [9*32-1:0]         dropped_count_o;

  wire [8:0] fifo_push_w;
  wire [8:0] fifo_accept_w;
  wire [8:0] fifo_drop_w;
  wire [8:0] fifo_valid_w;
  wire [8:0] fifo_ready_w;
  wire [9*DATA_WIDTH-1:0] fifo_data_w;
  wire event_fire_w;
  reg [8:0] overflow_mask_r;

  assign fifo_push_w = source_push_i & source_enable_mask_i;
  assign source_accept_o = fifo_accept_w;
  assign source_drop_o = fifo_drop_w | (source_push_i & !source_enable_mask_i);
  assign overflow_mask_o = overflow_mask_r;
  assign event_fire_w = event_valid_o && event_ready_i;

  genvar source_index;
  generate
    for (source_index = 0; source_index < 9; source_index = source_index + 1) begin : g_source_fifo
      wire [FIFO_ADDR_WIDTH:0] fifo_level_w;
      reg [31:0] accepted_count_r;
      reg [31:0] emitted_count_r;
      reg [31:0] dropped_count_r;

      lockstep_event_fifo #(
        .DATA_WIDTH (DATA_WIDTH),
        .DEPTH      (FIFO_DEPTH),
        .ADDR_WIDTH (FIFO_ADDR_WIDTH)
      ) u_event_fifo (
        .clk      (clk),
        .rst_n    (rst_n),
        .clear_i  (capture_start_i),
        .push_i   (fifo_push_w[source_index]),
        .data_i   (source_record_i[(source_index+1)*DATA_WIDTH-1:source_index*DATA_WIDTH]),
        .accept_o (fifo_accept_w[source_index]),
        .drop_o   (fifo_drop_w[source_index]),
        .valid_o  (fifo_valid_w[source_index]),
        .ready_i  (fifo_ready_w[source_index]),
        .data_o   (fifo_data_w[(source_index+1)*DATA_WIDTH-1:source_index*DATA_WIDTH]),
        .level_o  (fifo_level_w)
      );

      assign accepted_count_o[(source_index+1)*32-1:source_index*32] = accepted_count_r;
      assign emitted_count_o[(source_index+1)*32-1:source_index*32] = emitted_count_r;
      assign dropped_count_o[(source_index+1)*32-1:source_index*32] = dropped_count_r;

      // 每次采集开始清零独立计数，便于与 EVENT_END 精确核对。
      always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
          accepted_count_r <= #UDLY 32'd0;
          emitted_count_r <= #UDLY 32'd0;
          dropped_count_r <= #UDLY 32'd0;
        end else if (capture_start_i) begin
          accepted_count_r <= #UDLY (fifo_accept_w[source_index] ? 32'd1 : 32'd0);
          emitted_count_r <= #UDLY 32'd0;
          dropped_count_r <= #UDLY 32'd0;
        end else begin
          if (fifo_accept_w[source_index]) begin
            accepted_count_r <= #UDLY accepted_count_r + 32'd1;
          end
          if (event_fire_w && (event_source_o == source_index)) begin
            emitted_count_r <= #UDLY emitted_count_r + 32'd1;
          end
          if (fifo_drop_w[source_index]) begin
            dropped_count_r <= #UDLY dropped_count_r + 32'd1;
          end
        end
      end
    end
  endgenerate

  lockstep_event_round_robin_arbiter #(
    .DATA_WIDTH (DATA_WIDTH)
  ) u_event_arbiter (
    .clk            (clk),
    .rst_n          (rst_n),
    .source_valid_i (fifo_valid_w),
    .source_data_i  (fifo_data_w),
    .source_ready_o (fifo_ready_w),
    .event_valid_o  (event_valid_o),
    .event_ready_i  (event_ready_i),
    .event_data_o   (event_record_o),
    .event_source_o (event_source_o)
  );

  // overflow 粘滞到下一次采集开始，禁止软件遗漏瞬时满事件。
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      overflow_mask_r <= #UDLY 9'd0;
    end else if (capture_start_i) begin
      overflow_mask_r <= #UDLY 9'd0;
    end else begin
      overflow_mask_r <= #UDLY overflow_mask_r | fifo_drop_w;
    end
  end

endmodule
