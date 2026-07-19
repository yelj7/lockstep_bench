/**********************************************************
* 文件名: lockstep_ft601_245_adapter.v
* 日期: 2026-07-16
* 版本: 1.0
* 更新记录: 重写读写状态机，发送方向改为连续 burst。
* 描述: FT601 245 同步 FIFO 双向适配器，支持换向、背压和连续帧传输。
**********************************************************/

`timescale 1ns/1ps

module lockstep_ft601_245_adapter (
  input         ft_clk,
  input         rst_n,
  input         tx_axis_valid_i,
  output        tx_axis_ready_o,
  input  [31:0] tx_axis_data_i,
  input  [3:0]  tx_axis_be_valid_i,
  output reg    rx_word_valid_o,
  input         rx_word_ready_i,
  output reg [31:0] rx_word_data_o,
  output reg [3:0]  rx_word_be_valid_o,
  input         ft_txe_n_i,
  input         ft_rxf_n_i,
  inout  [31:0] ft_data_io,
  inout  [3:0]  ft_be_io,
  output        ft_oe_n_o,
  output        ft_rd_n_o,
  output        ft_wr_n_o,
  output [31:0] debug_state_o
);
  parameter UDLY = 1;
  parameter FT_BE_PAD_ACTIVE_LOW = 0;
  parameter TURNAROUND_CYCLES = 1;

  localparam [2:0] ST_IDLE         = 3'd0;
  localparam [2:0] ST_READ_SETUP   = 3'd1;
  localparam [2:0] ST_READ_STROBE  = 3'd2;
  localparam [2:0] ST_WRITE_SETUP  = 3'd3;
  localparam [2:0] ST_WRITE_STREAM = 3'd4;
  localparam [2:0] ST_TURNAROUND   = 3'd5;

  reg [2:0] state_r;
  reg [2:0] state_next_r;
  reg [31:0] tx_data_r;
  reg [3:0] tx_be_r;
  reg [31:0] turnaround_count_r;
  (* keep = "true", equivalent_register_removal = "no", max_fanout = 4 *)
  reg [31:0] ft_data_t_r;
  (* keep = "true", equivalent_register_removal = "no", max_fanout = 4 *)
  reg [3:0] ft_be_t_r;
  reg ft_oe_n_r;
  reg ft_rd_n_r;
  reg ft_wr_n_r;

  wire read_start_w;
  wire write_start_w;
  wire tx_stream_ready_w;
  wire tx_handshake_w;
  wire [3:0] ft_be_write_w;
  wire [3:0] ft_be_read_w;
  genvar bit_index;

  assign read_start_w = (state_r == ST_IDLE) && !rx_word_valid_o &&
                        rx_word_ready_i && !ft_rxf_n_i;
  assign write_start_w = (state_r == ST_IDLE) && !read_start_w &&
                         tx_axis_valid_i && !ft_txe_n_i;
  assign tx_stream_ready_w = (state_r == ST_WRITE_STREAM) && !ft_txe_n_i;
  assign tx_axis_ready_o = write_start_w || tx_stream_ready_w;
  assign tx_handshake_w = tx_axis_valid_i && tx_axis_ready_o;

  assign ft_oe_n_o = ft_oe_n_r;
  assign ft_rd_n_o = ft_rd_n_r;
  assign ft_wr_n_o = ft_wr_n_r;
  assign debug_state_o = {24'd0, ft_txe_n_i, ft_rxf_n_i,
                          rx_word_valid_o, tx_axis_valid_i, 1'b0, state_r};

  generate
    for (bit_index = 0; bit_index < 32; bit_index = bit_index + 1) begin : data_io
      assign ft_data_io[bit_index] = ft_data_t_r[bit_index] ? 1'bz : tx_data_r[bit_index];
    end
  endgenerate

  generate
    for (bit_index = 0; bit_index < 4; bit_index = bit_index + 1) begin : be_io
      assign ft_be_io[bit_index] = ft_be_t_r[bit_index] ? 1'bz : ft_be_write_w[bit_index];
    end
  endgenerate

  lockstep_ft601_be_polarity #(
    .FT_BE_PAD_ACTIVE_LOW(FT_BE_PAD_ACTIVE_LOW)
  ) be_polarity (
    .be_valid_i(tx_be_r),
    .be_pad_o(ft_be_write_w),
    .be_pad_i(ft_be_io),
    .be_valid_o(ft_be_read_w)
  );

  always @(*) begin
    state_next_r = state_r;
    case (state_r)
      ST_IDLE: begin
        if (read_start_w)
          state_next_r = ST_READ_SETUP;
        else if (write_start_w)
          state_next_r = ST_WRITE_SETUP;
      end
      ST_READ_SETUP: state_next_r = ST_READ_STROBE;
      ST_READ_STROBE: state_next_r = ST_TURNAROUND;
      ST_WRITE_SETUP: state_next_r = ST_WRITE_STREAM;
      ST_WRITE_STREAM: begin
        if (tx_axis_valid_i && !ft_txe_n_i)
          state_next_r = ST_WRITE_STREAM;
        else
          state_next_r = ST_TURNAROUND;
      end
      ST_TURNAROUND: begin
        if (turnaround_count_r >= (TURNAROUND_CYCLES - 1))
          state_next_r = ST_IDLE;
      end
      default: state_next_r = ST_IDLE;
    endcase
  end

  always @(posedge ft_clk or negedge rst_n) begin
    if (!rst_n) begin
      state_r <= #UDLY ST_IDLE;
      tx_data_r <= #UDLY 32'd0;
      tx_be_r <= #UDLY 4'hf;
      rx_word_valid_o <= #UDLY 1'b0;
      rx_word_data_o <= #UDLY 32'd0;
      rx_word_be_valid_o <= #UDLY 4'd0;
      turnaround_count_r <= #UDLY 32'd0;
      ft_data_t_r <= #UDLY 32'hffffffff;
      ft_be_t_r <= #UDLY 4'hf;
      ft_oe_n_r <= #UDLY 1'b1;
      ft_rd_n_r <= #UDLY 1'b1;
      ft_wr_n_r <= #UDLY 1'b1;
    end else begin
      state_r <= #UDLY state_next_r;

      if (tx_handshake_w) begin
        tx_data_r <= #UDLY tx_axis_data_i;
        tx_be_r <= #UDLY tx_axis_be_valid_i;
      end

      if (state_r == ST_READ_STROBE) begin
        rx_word_data_o <= #UDLY ft_data_io;
        rx_word_be_valid_o <= #UDLY ft_be_read_w;
        rx_word_valid_o <= #UDLY 1'b1;
      end else if (rx_word_valid_o && rx_word_ready_i) begin
        rx_word_valid_o <= #UDLY 1'b0;
      end

      if ((state_r == ST_READ_STROBE) || (state_r == ST_WRITE_STREAM))
        turnaround_count_r <= #UDLY 32'd0;
      else if (state_r == ST_TURNAROUND)
        turnaround_count_r <= #UDLY turnaround_count_r + 32'd1;

      ft_data_t_r <= #UDLY (((state_next_r == ST_WRITE_SETUP) ||
                             (state_next_r == ST_WRITE_STREAM)) ? 32'd0 : 32'hffffffff);
      ft_be_t_r <= #UDLY (((state_next_r == ST_WRITE_SETUP) ||
                           (state_next_r == ST_WRITE_STREAM)) ? 4'd0 : 4'hf);
      ft_oe_n_r <= #UDLY !((state_next_r == ST_READ_SETUP) ||
                           (state_next_r == ST_READ_STROBE));
      ft_rd_n_r <= #UDLY !(state_next_r == ST_READ_STROBE);
      ft_wr_n_r <= #UDLY !(state_next_r == ST_WRITE_STREAM);
    end
  end
endmodule

