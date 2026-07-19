/**********************************************************
* 文件名: lockstep_event_fifo.v
* 日期: 2026-07-19
* 版本: 1.0
* 更新记录: 新增协议事件独立同步 FIFO。
* 描述: 保存固定宽度事件，支持同周期写入和读出并提供满时丢失脉冲。
**********************************************************/

`timescale 1ns/1ps

module lockstep_event_fifo (
  clk,
  rst_n,
  clear_i,
  push_i,
  data_i,
  accept_o,
  drop_o,
  valid_o,
  ready_i,
  data_o,
  level_o
);
  parameter UDLY = 1;
  parameter integer DATA_WIDTH = 512;
  parameter integer DEPTH = 8;
  parameter integer ADDR_WIDTH = 3;

  input                   clk;
  input                   rst_n;
  input                   clear_i;
  input                   push_i;
  input  [DATA_WIDTH-1:0] data_i;
  output                  accept_o;
  output                  drop_o;
  output                  valid_o;
  input                   ready_i;
  output [DATA_WIDTH-1:0] data_o;
  output [ADDR_WIDTH:0]   level_o;

  reg [DATA_WIDTH-1:0] memory_r [0:DEPTH-1];
  reg [ADDR_WIDTH-1:0] write_ptr_r;
  reg [ADDR_WIDTH-1:0] read_ptr_r;
  reg [ADDR_WIDTH:0] count_r;

  wire pop_w;
  wire space_w;
  wire accept_w;
  wire drop_w;

  assign valid_o = (count_r != {ADDR_WIDTH+1{1'b0}});
  assign pop_w = valid_o && ready_i;
  assign space_w = (count_r < DEPTH) || pop_w;
  assign accept_w = push_i && (clear_i || space_w);
  assign drop_w = push_i && !clear_i && !space_w;
  assign accept_o = accept_w;
  assign drop_o = drop_w;
  assign data_o = memory_r[read_ptr_r];
  assign level_o = count_r;

  // 事件存储器和写指针；满且同周期弹出时允许无损写入。
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      write_ptr_r <= #UDLY {ADDR_WIDTH{1'b0}};
    end else if (clear_i) begin
      if (push_i) begin
        memory_r[0] <= #UDLY data_i;
        write_ptr_r <= #UDLY {{ADDR_WIDTH-1{1'b0}}, 1'b1};
      end else begin
        write_ptr_r <= #UDLY {ADDR_WIDTH{1'b0}};
      end
    end else if (accept_w) begin
      memory_r[write_ptr_r] <= #UDLY data_i;
      if (write_ptr_r == DEPTH - 1) begin
        write_ptr_r <= #UDLY {ADDR_WIDTH{1'b0}};
      end else begin
        write_ptr_r <= #UDLY write_ptr_r + {{ADDR_WIDTH-1{1'b0}}, 1'b1};
      end
    end
  end

  // 读指针仅在下游握手时推进。
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      read_ptr_r <= #UDLY {ADDR_WIDTH{1'b0}};
    end else if (clear_i) begin
      read_ptr_r <= #UDLY {ADDR_WIDTH{1'b0}};
    end else if (pop_w) begin
      if (read_ptr_r == DEPTH - 1) begin
        read_ptr_r <= #UDLY {ADDR_WIDTH{1'b0}};
      end else begin
        read_ptr_r <= #UDLY read_ptr_r + {{ADDR_WIDTH-1{1'b0}}, 1'b1};
      end
    end
  end

  // 计数器覆盖写、读和同周期写读三种情况。
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      count_r <= #UDLY {ADDR_WIDTH+1{1'b0}};
    end else if (clear_i) begin
      count_r <= #UDLY push_i ? {{ADDR_WIDTH{1'b0}}, 1'b1} : {ADDR_WIDTH+1{1'b0}};
    end else begin
      case ({accept_w, pop_w})
        2'b10: count_r <= #UDLY count_r + {{ADDR_WIDTH{1'b0}}, 1'b1};
        2'b01: count_r <= #UDLY count_r - {{ADDR_WIDTH{1'b0}}, 1'b1};
        default: count_r <= #UDLY count_r;
      endcase
    end
  end

endmodule
