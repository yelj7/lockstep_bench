/**********************************************************
* 文件名: lockstep_event_async_fifo.v
* 日期: 2026-07-19
* 版本: 1.3
* 更新记录: 增加包含预取寄存器在内的读域显式 empty 输出。
* 描述: 使用 Gray 指针同步和读域输出寄存器跨域传递固定宽度事件。
**********************************************************/

`timescale 1ns/1ps

module lockstep_event_async_fifo (
  write_clk,
  write_rst_n,
  write_valid_i,
  write_ready_o,
  write_data_i,
  write_drop_o,
  read_clk,
  read_rst_n,
  read_valid_o,
  read_empty_o,
  read_ready_i,
  read_data_o
);
  parameter UDLY = 1;
  parameter integer DATA_WIDTH = 512;
  parameter integer ADDR_WIDTH = 4;
  parameter integer DEPTH = 16;

  input                   write_clk;
  input                   write_rst_n;
  input                   write_valid_i;
  output                  write_ready_o;
  input  [DATA_WIDTH-1:0] write_data_i;
  output                  write_drop_o;
  input                   read_clk;
  input                   read_rst_n;
  output                  read_valid_o;
  output                  read_empty_o;
  input                   read_ready_i;
  output [DATA_WIDTH-1:0] read_data_o;

  (* ram_style = "block" *) reg [DATA_WIDTH-1:0] memory_r [0:DEPTH-1];
  reg [ADDR_WIDTH:0] write_binary_r;
  reg [ADDR_WIDTH:0] write_gray_r;
  reg [ADDR_WIDTH:0] read_binary_r;
  reg [ADDR_WIDTH:0] read_gray_r;
  (* ASYNC_REG = "TRUE" *) reg [ADDR_WIDTH:0] read_gray_write_d1_r;
  (* ASYNC_REG = "TRUE" *) reg [ADDR_WIDTH:0] read_gray_write_d2_r;
  (* ASYNC_REG = "TRUE" *) reg [ADDR_WIDTH:0] write_gray_read_d1_r;
  (* ASYNC_REG = "TRUE" *) reg [ADDR_WIDTH:0] write_gray_read_d2_r;
  reg                      read_valid_r;
  reg [DATA_WIDTH-1:0]     read_data_r;
  reg                      write_drop_r;

  wire [ADDR_WIDTH:0] write_binary_next_w;
  wire [ADDR_WIDTH:0] write_gray_next_w;
  wire [ADDR_WIDTH:0] read_binary_next_w;
  wire [ADDR_WIDTH:0] read_gray_next_w;
  wire [ADDR_WIDTH:0] full_compare_w;
  wire write_full_w;
  wire write_fire_w;
  wire memory_empty_w;
  wire read_load_w;

  assign write_binary_next_w = write_binary_r + {{ADDR_WIDTH{1'b0}}, 1'b1};
  assign write_gray_next_w = (write_binary_next_w >> 1) ^ write_binary_next_w;
  assign read_binary_next_w = read_binary_r + {{ADDR_WIDTH{1'b0}}, 1'b1};
  assign read_gray_next_w = (read_binary_next_w >> 1) ^ read_binary_next_w;
  assign full_compare_w = {~read_gray_write_d2_r[ADDR_WIDTH:ADDR_WIDTH-1],
                           read_gray_write_d2_r[ADDR_WIDTH-2:0]};
  assign write_full_w = (write_gray_r == full_compare_w);
  assign write_ready_o = !write_full_w;
  assign write_fire_w = write_valid_i && write_ready_o;
  assign write_drop_o = write_drop_r;
  assign memory_empty_w = (read_gray_r == write_gray_read_d2_r);
  assign read_load_w = (!read_valid_r || read_ready_i) && !memory_empty_w;
  assign read_valid_o = read_valid_r;
  assign read_empty_o = memory_empty_w && !read_valid_r;
  assign read_data_o = read_data_r;

  // 写域双口存储器写入与 Gray 写指针推进。
  always @(posedge write_clk or negedge write_rst_n) begin
    if (!write_rst_n) begin
      write_binary_r <= #UDLY {ADDR_WIDTH+1{1'b0}};
      write_gray_r <= #UDLY {ADDR_WIDTH+1{1'b0}};
      write_drop_r <= #UDLY 1'b0;
    end else begin
      write_drop_r <= #UDLY write_valid_i && !write_ready_o;
      if (write_fire_w) begin
        write_binary_r <= #UDLY write_binary_next_w;
        write_gray_r <= #UDLY write_gray_next_w;
      end
    end
  end

  // RAM 本体不参与异步复位，确保 Vivado 推断为 Block RAM。
  always @(posedge write_clk) begin
    if (write_fire_w) begin
      memory_r[write_binary_r[ADDR_WIDTH-1:0]] <= #UDLY write_data_i;
    end
  end

  // 读指针同步到写域，用于满判断。
  always @(posedge write_clk or negedge write_rst_n) begin
    if (!write_rst_n) begin
      read_gray_write_d1_r <= #UDLY {ADDR_WIDTH+1{1'b0}};
      read_gray_write_d2_r <= #UDLY {ADDR_WIDTH+1{1'b0}};
    end else begin
      read_gray_write_d1_r <= #UDLY read_gray_r;
      read_gray_write_d2_r <= #UDLY read_gray_write_d1_r;
    end
  end

  // 写指针同步到读域，用于空判断。
  always @(posedge read_clk or negedge read_rst_n) begin
    if (!read_rst_n) begin
      write_gray_read_d1_r <= #UDLY {ADDR_WIDTH+1{1'b0}};
      write_gray_read_d2_r <= #UDLY {ADDR_WIDTH+1{1'b0}};
    end else begin
      write_gray_read_d1_r <= #UDLY write_gray_r;
      write_gray_read_d2_r <= #UDLY write_gray_read_d1_r;
    end
  end

  // 读域预取到输出寄存器，反压期间 valid/data 保持稳定。
  always @(posedge read_clk or negedge read_rst_n) begin
    if (!read_rst_n) begin
      read_binary_r <= #UDLY {ADDR_WIDTH+1{1'b0}};
      read_gray_r <= #UDLY {ADDR_WIDTH+1{1'b0}};
      read_valid_r <= #UDLY 1'b0;
      read_data_r <= #UDLY {DATA_WIDTH{1'b0}};
    end else if (read_load_w) begin
      read_data_r <= #UDLY memory_r[read_binary_r[ADDR_WIDTH-1:0]];
      read_valid_r <= #UDLY 1'b1;
      read_binary_r <= #UDLY read_binary_next_w;
      read_gray_r <= #UDLY read_gray_next_w;
    end else if (read_valid_r && read_ready_i) begin
      read_valid_r <= #UDLY 1'b0;
    end
  end

endmodule
