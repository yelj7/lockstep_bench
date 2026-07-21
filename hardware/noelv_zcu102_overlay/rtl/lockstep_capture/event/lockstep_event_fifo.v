/**********************************************************
* 文件名: lockstep_event_fifo.v
* 日期: 2026-07-19
* 版本: 1.2
* 更新记录: 隔离 RAM 读数据与旁路数据寄存器，确保 Vivado 推断 Block RAM。
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

  (* ram_style = "block" *) reg [DATA_WIDTH-1:0] memory_r [0:DEPTH-1];
  reg [ADDR_WIDTH-1:0] write_ptr_r;
  reg [ADDR_WIDTH-1:0] read_ptr_r;
  reg [ADDR_WIDTH:0] stored_count_r;
  reg                  output_valid_r;
  reg                  output_from_memory_r;
  reg [DATA_WIDTH-1:0] memory_data_r;
  reg [DATA_WIDTH-1:0] bypass_data_r;

  wire pop_w;
  wire space_w;
  wire accept_w;
  wire drop_w;
  wire bypass_push_w;
  wire memory_write_w;
  wire memory_read_w;
  wire [ADDR_WIDTH:0] total_count_w;

  assign total_count_w = stored_count_r + {{ADDR_WIDTH{1'b0}}, output_valid_r};
  assign valid_o = output_valid_r;
  assign pop_w = valid_o && ready_i;
  assign space_w = (total_count_w < DEPTH) || pop_w;
  assign accept_w = push_i && (clear_i || space_w);
  assign drop_w = push_i && !clear_i && !space_w;
  assign memory_read_w = pop_w && (stored_count_r != {ADDR_WIDTH+1{1'b0}});
  assign bypass_push_w = accept_w && (!output_valid_r ||
                         (pop_w && (stored_count_r == {ADDR_WIDTH+1{1'b0}})));
  assign memory_write_w = accept_w && !bypass_push_w;
  assign accept_o = accept_w;
  assign drop_o = drop_w;
  assign data_o = output_from_memory_r ? memory_data_r : bypass_data_r;
  assign level_o = total_count_w;

  // 输出寄存器保存当前头项；其余项写入同步读 Block RAM。
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      write_ptr_r <= #UDLY {ADDR_WIDTH{1'b0}};
      read_ptr_r <= #UDLY {ADDR_WIDTH{1'b0}};
      stored_count_r <= #UDLY {ADDR_WIDTH+1{1'b0}};
      output_valid_r <= #UDLY 1'b0;
      output_from_memory_r <= #UDLY 1'b0;
      bypass_data_r <= #UDLY {DATA_WIDTH{1'b0}};
    end else if (clear_i) begin
      write_ptr_r <= #UDLY {ADDR_WIDTH{1'b0}};
      read_ptr_r <= #UDLY {ADDR_WIDTH{1'b0}};
      stored_count_r <= #UDLY {ADDR_WIDTH+1{1'b0}};
      if (push_i) begin
        output_valid_r <= #UDLY 1'b1;
        output_from_memory_r <= #UDLY 1'b0;
        bypass_data_r <= #UDLY data_i;
      end else begin
        output_valid_r <= #UDLY 1'b0;
        output_from_memory_r <= #UDLY 1'b0;
      end
    end else begin
      if (memory_write_w) begin
        if (write_ptr_r == DEPTH - 1) begin
          write_ptr_r <= #UDLY {ADDR_WIDTH{1'b0}};
        end else begin
          write_ptr_r <= #UDLY write_ptr_r + {{ADDR_WIDTH-1{1'b0}}, 1'b1};
        end
      end
      if (memory_read_w) begin
        output_valid_r <= #UDLY 1'b1;
        output_from_memory_r <= #UDLY 1'b1;
        if (read_ptr_r == DEPTH - 1) begin
          read_ptr_r <= #UDLY {ADDR_WIDTH{1'b0}};
        end else begin
          read_ptr_r <= #UDLY read_ptr_r + {{ADDR_WIDTH-1{1'b0}}, 1'b1};
        end
      end else if (bypass_push_w) begin
        output_valid_r <= #UDLY 1'b1;
        output_from_memory_r <= #UDLY 1'b0;
        bypass_data_r <= #UDLY data_i;
      end else if (pop_w) begin
        output_valid_r <= #UDLY 1'b0;
      end
      case ({memory_write_w, memory_read_w})
        2'b10: stored_count_r <= #UDLY stored_count_r + {{ADDR_WIDTH{1'b0}}, 1'b1};
        2'b01: stored_count_r <= #UDLY stored_count_r - {{ADDR_WIDTH{1'b0}}, 1'b1};
        default: stored_count_r <= #UDLY stored_count_r;
      endcase
    end
  end

  // RAM 读写口只保留标准同步时序，不参与复位或旁路赋值。
  always @(posedge clk) begin
    if (memory_write_w) begin
      memory_r[write_ptr_r] <= data_i;
    end
    if (memory_read_w) begin
      memory_data_r <= memory_r[read_ptr_r];
    end
  end

endmodule
