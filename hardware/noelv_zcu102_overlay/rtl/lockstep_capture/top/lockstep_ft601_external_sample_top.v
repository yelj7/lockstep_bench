/**********************************************************
* 文件名: lockstep_ft601_external_sample_top.v
* 日期: 2026-07-17
* 版本: 1.0
* 更新记录: 固化独立 FT601 上电复位并接入连续 burst adapter。
* 描述: 接收 1024-bit 外部宽样本，复用锁步采集协议流核心和 FT601 245 adapter。
**********************************************************/

`timescale 1ns/1ps

module lockstep_ft601_external_sample_top (
  ft_clk,
  rst_n,
  sample_clk,
  sample_rst_n,
  sample_valid_i,
  sample_i,
  sample_abs_index_i,
  ft_txe_n_i,
  ft_rxf_n_i,
  ft_data_io,
  ft_be_io,
  ft_oe_n_o,
  ft_rd_n_o,
  ft_wr_n_o,
  ft_siwu_n_o,
  ft_wakeup_n_o,
  debug_ft601_state_o
);
  parameter UDLY = 1;
  parameter FT_BE_PAD_ACTIVE_LOW = 0;
  parameter TURNAROUND_CYCLES = 1;
  parameter integer PROBE_SAMPLE_BITS = 1024;
  parameter integer MAX_PROBE_SAMPLE_BITS = 1024;
  parameter integer PROBE_LANE_BITS = 128;
  parameter integer PROTOCOL_COUNT = 9;
  parameter integer SAMPLE_ADDR_WIDTH = 12;
  parameter integer LANE_INDEX_BITS = 4;

  localparam RESET_SHIFT_WIDTH = 4;
  localparam [RESET_SHIFT_WIDTH-1:0] RESET_ASSERT_VALUE = 4'hf;
  localparam [RESET_SHIFT_WIDTH-1:0] RESET_DONE_VALUE = 4'h0;

  input                         ft_clk;
  input                         rst_n;
  input                         sample_clk;
  input                         sample_rst_n;
  input                         sample_valid_i;
  input  [PROBE_SAMPLE_BITS-1:0] sample_i;
  input  [31:0]                 sample_abs_index_i;
  input                         ft_txe_n_i;
  input                         ft_rxf_n_i;
  inout  [31:0]                 ft_data_io;
  inout  [3:0]                  ft_be_io;
  output                        ft_oe_n_o;
  output                        ft_rd_n_o;
  output                        ft_wr_n_o;
  output                        ft_siwu_n_o;
  output                        ft_wakeup_n_o;
  output [31:0]                 debug_ft601_state_o;

  reg [RESET_SHIFT_WIDTH-1:0] ft_reset_shift_r = RESET_ASSERT_VALUE;
  (* ASYNC_REG = "TRUE" *) reg source_enable_sample_d1_r;
  (* ASYNC_REG = "TRUE" *) reg source_enable_sample_d2_r;
  reg                         source_enable_sample_prev_r;
  reg [31:0]                  local_sample_abs_index_r;

  wire                        ft_rst_n_w;
  wire                        source_enable_w;
  wire                        source_enable_sample_w;
  wire                        core_sample_valid_w;
  wire [31:0]                 core_sample_abs_index_w;
  wire                        core_rx_word_valid_w;
  wire                        core_rx_word_ready_w;
  wire [31:0]                 core_rx_word_data_w;
  wire [3:0]                  core_rx_be_valid_w;
  wire                        core_tx_word_valid_w;
  wire                        core_tx_word_ready_w;
  wire [31:0]                 core_tx_word_data_w;
  wire [31:0]                 debug_cfg_trigger_value_w;
  wire                        debug_cfg_valid_w;
  wire [31:0]                 debug_device_state_w;
  wire [31:0]                 debug_capture_id_w;
  wire [31:0]                 debug_parser_state_w;
  wire [31:0]                 debug_command_state_w;
  wire [31:0]                 debug_capture_state_w;
  wire [31:0]                 debug_sequencer_state_w;
  wire [31:0]                 debug_generator_state_w;
  wire [31:0]                 debug_ft601_state_w;
  wire [31:0]                 debug_wide_capture_state_w;
  wire [31:0]                 debug_wide_capture_metadata_w;
  wire [31:0]                 debug_wide_capture_samples_seen_w;

  assign ft_rst_n_w = (ft_reset_shift_r == RESET_DONE_VALUE);
  assign source_enable_sample_w = source_enable_sample_d2_r;
  assign core_sample_valid_w = source_enable_sample_w && sample_valid_i;
  assign core_sample_abs_index_w = local_sample_abs_index_r;

  assign ft_siwu_n_o = 1'b1;
  assign ft_wakeup_n_o = 1'b1;

  // FT601 时钟域复位同步释放，输入 rst_n 可来自 NOEL-V 板级复位树。
  always @(posedge ft_clk or negedge rst_n) begin
    if (!rst_n) begin
      ft_reset_shift_r <= #UDLY RESET_ASSERT_VALUE;
    end else if (ft_reset_shift_r != RESET_DONE_VALUE) begin
      ft_reset_shift_r <= #UDLY {ft_reset_shift_r[RESET_SHIFT_WIDTH-2:0], 1'b0};
    end else begin
      ft_reset_shift_r <= #UDLY ft_reset_shift_r;
    end
  end

  // FT601 域下发的采集使能同步到 sample_clk 域，并生成 arm 后本地样本 index。
  always @(posedge sample_clk or negedge sample_rst_n) begin
    if (!sample_rst_n) begin
      source_enable_sample_d1_r    <= #UDLY 1'b0;
      source_enable_sample_d2_r    <= #UDLY 1'b0;
      source_enable_sample_prev_r  <= #UDLY 1'b0;
      local_sample_abs_index_r     <= #UDLY 32'd0;
    end else begin
      source_enable_sample_d1_r   <= #UDLY source_enable_w;
      source_enable_sample_d2_r   <= #UDLY source_enable_sample_d1_r;
      source_enable_sample_prev_r <= #UDLY source_enable_sample_w;

      if (!source_enable_sample_w) begin
        local_sample_abs_index_r <= #UDLY 32'd0;
      end else if (!source_enable_sample_prev_r) begin
        local_sample_abs_index_r <= #UDLY (sample_valid_i ? 32'd1 : 32'd0);
      end else if (sample_valid_i) begin
        local_sample_abs_index_r <= #UDLY local_sample_abs_index_r + 32'd1;
      end else begin
        local_sample_abs_index_r <= #UDLY local_sample_abs_index_r;
      end
    end
  end

  lockstep_ft601_245_adapter #(
    .FT_BE_PAD_ACTIVE_LOW (FT_BE_PAD_ACTIVE_LOW),
    .TURNAROUND_CYCLES    (TURNAROUND_CYCLES)
  ) u_ft601_245_adapter (
    .ft_clk             (ft_clk),
    .rst_n              (ft_rst_n_w),
    .tx_axis_valid_i    (core_tx_word_valid_w),
    .tx_axis_ready_o    (core_tx_word_ready_w),
    .tx_axis_data_i     (core_tx_word_data_w),
    .tx_axis_be_valid_i (4'hf),
    .rx_word_valid_o    (core_rx_word_valid_w),
    .rx_word_ready_i    (core_rx_word_ready_w),
    .rx_word_data_o     (core_rx_word_data_w),
    .rx_word_be_valid_o (core_rx_be_valid_w),
    .ft_txe_n_i         (ft_txe_n_i),
    .ft_rxf_n_i         (ft_rxf_n_i),
    .ft_data_io         (ft_data_io),
    .ft_be_io           (ft_be_io),
    .ft_oe_n_o          (ft_oe_n_o),
    .ft_rd_n_o          (ft_rd_n_o),
    .ft_wr_n_o          (ft_wr_n_o),
    .debug_state_o      (debug_ft601_state_w)
  );

  lockstep_capture_protocol_stream_core #(
    .PROBE_SAMPLE_BITS     (PROBE_SAMPLE_BITS),
    .MAX_PROBE_SAMPLE_BITS (MAX_PROBE_SAMPLE_BITS),
    .PROBE_LANE_BITS       (PROBE_LANE_BITS),
    .PROTOCOL_COUNT        (PROTOCOL_COUNT),
    .SAMPLE_ADDR_WIDTH     (SAMPLE_ADDR_WIDTH),
    .LANE_INDEX_BITS       (LANE_INDEX_BITS)
  ) u_capture_protocol_stream_core (
    .clk                            (ft_clk),
    .rst_n                          (ft_rst_n_w),
    .sample_clk                     (sample_clk),
    .sample_rst_n                   (sample_rst_n),
    .rx_word_valid_i                (core_rx_word_valid_w),
    .rx_word_ready_o                (core_rx_word_ready_w),
    .rx_word_data_i                 (core_rx_word_data_w),
    .rx_be_valid_i                  (core_rx_be_valid_w),
    .wide_sample_valid_i            (core_sample_valid_w),
    .wide_sample_i                  (sample_i),
    .wide_sample_abs_index_i        (core_sample_abs_index_w),
    .capture_external_status_flags_i(32'd0),
    .capture_external_debug_state_i (debug_ft601_state_w),
    .tx_word_valid_o                (core_tx_word_valid_w),
    .tx_word_ready_i                (core_tx_word_ready_w),
    .tx_word_data_o                 (core_tx_word_data_w),
    .tx_frame_start_o               (),
    .tx_frame_end_o                 (),
    .tx_frame_type_o                (),
    .tx_frame_seq_o                 (),
    .source_enable_o                (source_enable_w),
    .debug_device_state_o           (debug_device_state_w),
    .debug_capture_id_o             (debug_capture_id_w),
    .debug_parser_state_o           (debug_parser_state_w),
    .debug_command_state_o          (debug_command_state_w),
    .debug_capture_state_o          (debug_capture_state_w),
    .debug_sequencer_state_o        (debug_sequencer_state_w),
    .debug_generator_state_o        (debug_generator_state_w),
    .debug_cfg_trigger_value_o      (debug_cfg_trigger_value_w),
    .debug_cfg_valid_o              (debug_cfg_valid_w),
    .debug_wide_capture_state_o     (debug_wide_capture_state_w),
    .debug_wide_capture_metadata_o  (debug_wide_capture_metadata_w),
    .debug_wide_capture_samples_seen_o(debug_wide_capture_samples_seen_w),
    .debug_wide_capture_flags_o       (),
    .debug_wide_pretrigger_samples_o  (),
    .debug_wide_posttrigger_samples_o (),
    .debug_frame_source_state_o       (),
    .debug_tx_bytes_o                 ()
  );

  assign debug_ft601_state_o = debug_ft601_state_w;

endmodule
