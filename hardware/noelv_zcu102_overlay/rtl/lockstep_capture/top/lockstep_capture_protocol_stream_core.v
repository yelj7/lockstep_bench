/**********************************************************
* 文件名: lockstep_capture_protocol_stream_core.v
* 日期: 2026-07-17
* 版本: 2.1
* 更新记录: 复用采样域就绪同步链作为事件 FIFO 读域复位。
* 描述: 复用命令解析、帧仲裁和发送器，交错上传连续窗口与稀疏事件。
**********************************************************/

`timescale 1ns/1ps

module lockstep_capture_protocol_stream_core (
  clk,
  rst_n,
  sample_clk,
  sample_rst_n,
  rx_word_valid_i,
  rx_word_ready_o,
  rx_word_data_i,
  rx_be_valid_i,
  wide_sample_valid_i,
  wide_sample_i,
  wide_sample_abs_index_i,
  capture_external_status_flags_i,
  capture_external_debug_state_i,
  tx_word_valid_o,
  tx_word_ready_i,
  tx_word_data_o,
  tx_frame_start_o,
  tx_frame_end_o,
  tx_frame_type_o,
  tx_frame_seq_o,
  source_enable_o,
  debug_device_state_o,
  debug_capture_id_o,
  debug_parser_state_o,
  debug_command_state_o,
  debug_capture_state_o,
  debug_sequencer_state_o,
  debug_generator_state_o,
  debug_cfg_trigger_value_o,
  debug_cfg_valid_o,
  debug_wide_capture_state_o,
  debug_wide_capture_metadata_o,
  debug_wide_capture_samples_seen_o,
  debug_wide_capture_flags_o,
  debug_wide_pretrigger_samples_o,
  debug_wide_posttrigger_samples_o,
  debug_frame_source_state_o,
  debug_tx_bytes_o
);
  parameter UDLY                    = 1;
  parameter integer PROBE_SAMPLE_BITS     = 384;
  parameter integer MAX_PROBE_SAMPLE_BITS = 1024;
  parameter integer PROBE_LANE_BITS       = 128;
  parameter integer PROTOCOL_COUNT        = 8;
  parameter integer SAMPLE_ADDR_WIDTH     = 12;
  parameter integer LANE_INDEX_BITS       = 4;
  parameter integer AHB_TRIGGER_MODE      = 1;

  input                             clk;
  input                             rst_n;
  input                             sample_clk;
  input                             sample_rst_n;
  input                             rx_word_valid_i;
  output                            rx_word_ready_o;
  input  [31:0]                     rx_word_data_i;
  input  [3:0]                      rx_be_valid_i;
  input                             wide_sample_valid_i;
  input  [PROBE_SAMPLE_BITS-1:0]    wide_sample_i;
  input  [31:0]                     wide_sample_abs_index_i;
  input  [31:0]                     capture_external_status_flags_i;
  input  [31:0]                     capture_external_debug_state_i;
  output                            tx_word_valid_o;
  input                             tx_word_ready_i;
  output [31:0]                     tx_word_data_o;
  output                            tx_frame_start_o;
  output                            tx_frame_end_o;
  output [15:0]                     tx_frame_type_o;
  output [31:0]                     tx_frame_seq_o;
  output                            source_enable_o;
  output [31:0]                     debug_device_state_o;
  output [31:0]                     debug_capture_id_o;
  output [31:0]                     debug_parser_state_o;
  output [31:0]                     debug_command_state_o;
  output [31:0]                     debug_capture_state_o;
  output [31:0]                     debug_sequencer_state_o;
  output [31:0]                     debug_generator_state_o;
  output [31:0]                     debug_cfg_trigger_value_o;
  output                            debug_cfg_valid_o;
  output [31:0]                     debug_wide_capture_state_o;
  output [31:0]                     debug_wide_capture_metadata_o;
  output [31:0]                     debug_wide_capture_samples_seen_o;
  output [31:0]                     debug_wide_capture_flags_o;
  output [31:0]                     debug_wide_pretrigger_samples_o;
  output [31:0]                     debug_wide_posttrigger_samples_o;
  output [31:0]                     debug_frame_source_state_o;
  output [31:0]                     debug_tx_bytes_o;

  `ifdef LOCKSTEP_CAPTURE_PROTOCOL_V2_VH
  `undef LOCKSTEP_CAPTURE_PROTOCOL_V2_VH
  `endif
  `include "lockstep_capture_protocol_v2.vh"

  wire        cmd_valid_w;
  wire        cmd_ready_w;
  wire [15:0] cmd_type_w;
  wire [31:0] cmd_seq_w;
  wire [31:0] cmd_capture_id_w;
  wire [31:0] cmd_payload_words_w;
  wire [31:0] cmd_payload0_w;
  wire [31:0] cmd_payload1_w;
  wire [31:0] cmd_payload2_w;
  wire [31:0] cmd_payload3_w;
  wire [31:0] cmd_payload4_w;
  wire [31:0] cmd_payload5_w;
  wire [31:0] cmd_payload6_w;
  wire [31:0] cmd_payload7_w;
  wire [31:0] cmd_payload8_w;
  wire [31:0] cmd_payload9_w;
  wire [31:0] cmd_payload10_w;
  wire [31:0] cmd_payload11_w;
  wire [31:0] cmd_payload12_w;
  wire        parser_error_valid_w;
  wire        parser_error_ready_w;
  wire [31:0] parser_error_code_w;
  wire [15:0] parser_error_type_w;
  wire [31:0] parser_error_seq_w;
  wire [31:0] parser_error_detail0_w;
  wire [31:0] parser_error_detail1_w;

  wire        cfg_valid_w;
  wire        cfg_ready_w;
  wire [31:0] cfg_sample_rate_hz_w;
  wire [31:0] cfg_sample_count_w;
  wire [31:0] cfg_pretrigger_count_w;
  wire [31:0] cfg_posttrigger_count_w;
  wire [31:0] cfg_channel_mask_w;
  wire [31:0] cfg_input_invert_mask_w;
  wire [15:0] cfg_physical_channels_w;
  wire [15:0] cfg_sample_word_bits_w;
  wire [31:0] cfg_trigger_mask_w;
  wire [31:0] cfg_trigger_value_w;
  wire [31:0] cfg_trigger_edge_rise_w;
  wire [31:0] cfg_trigger_edge_fall_w;
  wire [31:0] cfg_mode_w;
  wire [31:0] cfg_trigger_timeout_samples_w;
  wire [31:0] cfg_event_enable_mask_w;
  wire [31:0] cfg_event_limit_w;
  wire [31:0] cfg_event_watchdog_ticks_w;
  wire [31:0] cfg_event_hard_timeout_ticks_w;
  wire        cfg_error_valid_w;
  wire [31:0] cfg_error_code_w;
  wire [31:0] cfg_error_detail0_w;
  wire [31:0] cfg_error_detail1_w;
  wire        arm_w;
  wire        event_stream_start_w;
  wire        stop_w;
  wire [31:0] capture_id_w;
  wire [31:0] device_state_w;
  wire [31:0] last_error_code_w;

  wire        wide_arm_ready_w;
  wire        wide_arm_accepted_w;
  wire        wide_busy_w;
  wire        wide_done_w;
  wire        wide_done_pulse_w;
  wire        wide_upload_allowed_w;
  wire        wide_param_error_w;
  wire        wide_pretrigger_ready_w;
  wire        wide_meta_valid_w;
  wire [PROTOCOL_COUNT-1:0] wide_meta_protocol_enable_w;
  wire [31:0] wide_meta_protocol_count_w;
  wire [31:0] wide_meta_sample_bits_w;
  wire [31:0] wide_meta_lane_count_w;
  wire [31:0] wide_meta_max_lane_count_w;
  wire [31:0] wide_meta_window_sample_count_w;
  wire [31:0] wide_meta_pretrigger_count_w;
  wire [31:0] wide_meta_trigger_count_w;
  wire [31:0] wide_meta_post_count_w;
  wire [31:0] wide_meta_window_start_w;
  wire [31:0] wide_meta_trigger_abs_index_w;
  wire [31:0] wide_meta_trigger_sample_index_w;
  wire [31:0] wide_samples_seen_w;
  wire [31:0] wide_post_seen_w;
  wire        wide_read_req_w;
  wire [SAMPLE_ADDR_WIDTH-1:0] wide_read_sample_index_w;
  wire [LANE_INDEX_BITS-1:0]   wide_read_lane_index_w;
  wire        wide_read_ready_w;
  wire        wide_read_valid_w;
  wire        wide_read_error_w;
  wire [PROBE_LANE_BITS-1:0] wide_read_data_w;
  wire        wide_read_last_lane_w;
  wire        wide_read_last_sample_w;
  wire [3:0]  wide_capture_state_w;
  wire        wide_abort_pulse_w;
  wire        wide_trigger_seen_w;
  wire        wide_watchdog_expired_w;
  wire        wide_watchdog_pulse_w;
  wire        wide_trigger_pulse_sample_w;
  wire        window_stop_w;
  wire        frame_stop_w;
  wire [PROTOCOL_COUNT-1:0] protocol_enable_w;

  wire [8:0] event_enable_w;
  (* ASYNC_REG = "TRUE" *) reg [8:0] event_enable_sample_d1_r;
  (* ASYNC_REG = "TRUE" *) reg [8:0] event_enable_sample_d2_r;
  (* ASYNC_REG = "TRUE" *) reg [31:0] capture_id_sample_d1_r;
  (* ASYNC_REG = "TRUE" *) reg [31:0] capture_id_sample_d2_r;
  (* ASYNC_REG = "TRUE" *) reg [31:0] cfg_event_watchdog_sample_d1_r;
  (* ASYNC_REG = "TRUE" *) reg [31:0] cfg_event_watchdog_sample_d2_r;
  (* ASYNC_REG = "TRUE" *) reg [31:0] cfg_event_hard_timeout_sample_d1_r;
  (* ASYNC_REG = "TRUE" *) reg [31:0] cfg_event_hard_timeout_sample_d2_r;
  reg stop_toggle_clk_r;
  (* ASYNC_REG = "TRUE" *) reg stop_toggle_sample_d1_r;
  (* ASYNC_REG = "TRUE" *) reg stop_toggle_sample_d2_r;
  (* ASYNC_REG = "TRUE" *) reg stop_toggle_sample_d3_r;
  reg [31:0] cfg_event_watchdog_active_sample_r;
  reg [31:0] cfg_event_hard_timeout_active_sample_r;
  (* ASYNC_REG = "TRUE" *) reg capture_domain_ready_d1_r;
  (* ASYNC_REG = "TRUE" *) reg capture_domain_ready_d2_r;
  (* ASYNC_REG = "TRUE" *) reg event_fifo_write_reset_d1_r;
  (* ASYNC_REG = "TRUE" *) reg event_fifo_write_reset_d2_r;
  (* ASYNC_REG = "TRUE" *) reg event_fifo_read_ready_sample_d1_r;
  (* ASYNC_REG = "TRUE" *) reg event_fifo_read_ready_sample_d2_r;
  (* ASYNC_REG = "TRUE" *) reg event_fifo_write_ready_read_d1_r;
  (* ASYNC_REG = "TRUE" *) reg event_fifo_write_ready_read_d2_r;
  wire event_fifo_write_rst_n_w;
  wire event_fifo_read_rst_n_w;
  wire event_fifo_domains_ready_sample_w;
  wire event_fifo_domains_ready_read_w;
  wire stop_sample_pulse_w;
  wire event_capture_active_sample_w;
  wire event_draining_sample_w;
  wire event_controller_done_sample_w;
  wire [63:0] event_global_timestamp_sample_w;
  wire [31:0] event_end_reason_sample_w;
  wire [8:0] event_source_push_w;
  wire [9*512-1:0] event_source_record_w;
  wire [8:0] event_source_drop_w;
  wire event_protocol_busy_sample_w;
  wire event_arb_valid_w;
  wire event_arb_ready_w;
  wire event_fifo_write_ready_w;
  wire [511:0] event_arb_record_w;
  wire [3:0] event_arb_source_w;
  wire [8:0] event_overflow_sample_w;
  wire [9*32-1:0] event_dropped_sample_w;
  reg event_controller_ended_sample_r;
  reg event_done_toggle_sample_r;
  reg [31:0] event_end_reason_stable_sample_r;
  reg [8:0] event_overflow_stable_sample_r;
  reg [9*32-1:0] event_dropped_stable_sample_r;
  (* ASYNC_REG = "TRUE" *) reg event_done_toggle_read_d1_r;
  (* ASYNC_REG = "TRUE" *) reg event_done_toggle_read_d2_r;
  (* ASYNC_REG = "TRUE" *) reg event_done_toggle_read_d3_r;
  (* ASYNC_REG = "TRUE" *) reg event_draining_read_d1_r;
  (* ASYNC_REG = "TRUE" *) reg event_draining_read_d2_r;
  reg event_end_pending_read_r;
  reg event_collection_done_read_r;
  reg [31:0] event_end_reason_read_r;
  reg [8:0] event_overflow_read_r;
  reg [9*32-1:0] event_dropped_read_r;
  wire event_done_pulse_read_w;
  wire event_pipeline_draining_read_w;
  wire event_async_valid_w;
  wire event_fifo_read_valid_w;
  wire event_async_empty_w;
  wire event_async_ready_w;
  wire [511:0] event_async_record_w;
  wire event_async_drop_w;
  wire event_payload_done_w;
  wire event_frame_done_w;
  reg event_stream_released_r;

  wire wide_frame_valid_w;
  wire wide_frame_ready_w;
  wire [15:0] wide_frame_type_w;
  wire [31:0] wide_frame_capture_id_w;
  wire [31:0] wide_frame_flags_w;
  wire [31:0] wide_payload_word_count_w;
  wire [31:0] wide_payload0_w;
  wire [31:0] wide_payload1_w;
  wire [31:0] wide_payload2_w;
  wire [31:0] wide_payload3_w;
  wire [31:0] wide_payload4_w;
  wire [31:0] wide_payload5_w;
  wire [31:0] wide_payload6_w;
  wire [31:0] wide_payload7_w;
  wire [31:0] wide_payload8_w;
  wire [31:0] wide_payload9_w;
  wire [31:0] wide_payload10_w;
  wire [31:0] wide_payload11_w;
  wire [31:0] wide_payload12_w;
  wire [31:0] wide_payload13_w;
  wire [31:0] wide_payload14_w;
  wire [31:0] wide_payload15_w;
  wire event_frame_valid_w;
  wire event_frame_ready_w;
  wire [15:0] event_frame_type_w;
  wire [31:0] event_frame_capture_id_w;
  wire [31:0] event_frame_flags_w;
  wire [31:0] event_payload_word_count_w;
  wire [31:0] event_payload0_w;
  wire [31:0] event_payload1_w;
  wire [31:0] event_payload2_w;
  wire [31:0] event_payload3_w;
  wire [31:0] event_payload4_w;
  wire [31:0] event_payload5_w;
  wire [31:0] event_payload6_w;
  wire [31:0] event_payload7_w;
  wire [31:0] event_payload8_w;
  wire [31:0] event_payload9_w;
  wire [31:0] event_payload10_w;
  wire [31:0] event_payload11_w;
  wire [31:0] event_payload12_w;
  wire [31:0] event_payload13_w;
  wire [31:0] event_payload14_w;
  wire [31:0] event_payload15_w;

  wire        cap_frame_valid_w;
  wire        cap_frame_ready_w;
  wire        cap_event_select_w;
  wire [15:0] cap_frame_type_w;
  wire [31:0] cap_frame_capture_id_w;
  wire [31:0] cap_frame_flags_w;
  wire [31:0] cap_payload_word_count_w;
  wire [31:0] cap_payload0_w;
  wire [31:0] cap_payload1_w;
  wire [31:0] cap_payload2_w;
  wire [31:0] cap_payload3_w;
  wire [31:0] cap_payload4_w;
  wire [31:0] cap_payload5_w;
  wire [31:0] cap_payload6_w;
  wire [31:0] cap_payload7_w;
  wire [31:0] cap_payload8_w;
  wire [31:0] cap_payload9_w;
  wire [31:0] cap_payload10_w;
  wire [31:0] cap_payload11_w;
  wire [31:0] cap_payload12_w;
  wire [31:0] cap_payload13_w;
  wire [31:0] cap_payload14_w;
  wire [31:0] cap_payload15_w;
  wire        cap_seq_done_w;
  wire [31:0] cap_samples_uploaded_w;
  wire [31:0] cap_device_status_flags_w;
  wire [31:0] cap_frame_source_state_w;

  wire        cmd_frame_valid_w;
  wire        cmd_frame_ready_w;
  wire [15:0] cmd_frame_type_w;
  wire [31:0] cmd_frame_capture_id_w;
  wire [31:0] cmd_frame_flags_w;
  wire [31:0] cmd_payload_word_count_w;
  wire [31:0] cmd_frame_payload0_w;
  wire [31:0] cmd_frame_payload1_w;
  wire [31:0] cmd_frame_payload2_w;
  wire [31:0] cmd_frame_payload3_w;
  wire [31:0] cmd_frame_payload4_w;
  wire [31:0] cmd_frame_payload5_w;
  wire [31:0] cmd_frame_payload6_w;
  wire [31:0] cmd_frame_payload7_w;
  wire [31:0] cmd_frame_payload8_w;
  wire [31:0] cmd_frame_payload9_w;
  wire [31:0] cmd_frame_payload10_w;
  wire [31:0] cmd_frame_payload11_w;
  wire [31:0] cmd_frame_payload12_w;
  wire [31:0] cmd_frame_payload13_w;
  wire [31:0] cmd_frame_payload14_w;
  wire [31:0] cmd_frame_payload15_w;

  wire        arb_frame_valid_w;
  wire        arb_frame_ready_w;
  wire [15:0] arb_frame_type_w;
  wire [31:0] arb_frame_capture_id_w;
  wire [31:0] arb_frame_flags_w;
  wire [31:0] arb_payload_word_count_w;
  wire [31:0] arb_payload0_w;
  wire [31:0] arb_payload1_w;
  wire [31:0] arb_payload2_w;
  wire [31:0] arb_payload3_w;
  wire [31:0] arb_payload4_w;
  wire [31:0] arb_payload5_w;
  wire [31:0] arb_payload6_w;
  wire [31:0] arb_payload7_w;
  wire [31:0] arb_payload8_w;
  wire [31:0] arb_payload9_w;
  wire [31:0] arb_payload10_w;
  wire [31:0] arb_payload11_w;
  wire [31:0] arb_payload12_w;
  wire [31:0] arb_payload13_w;
  wire [31:0] arb_payload14_w;
  wire [31:0] arb_payload15_w;
  wire        source_enable_next_w;
  reg         source_enable_o;

  assign protocol_enable_w = (cfg_channel_mask_w[PROTOCOL_COUNT-1:0] == {PROTOCOL_COUNT{1'b0}}) ?
                             {PROTOCOL_COUNT{1'b1}} :
                             cfg_channel_mask_w[PROTOCOL_COUNT-1:0];
  assign event_enable_w = cfg_event_enable_mask_w[8:0] & 9'h19f;
  assign window_stop_w = (stop_w && !wide_meta_valid_w) || cap_seq_done_w;
  assign frame_stop_w = stop_w || wide_abort_pulse_w;
  assign source_enable_next_w = (device_state_w == LOCKSTEP_STATE_ARMED) ||
                                (device_state_w == LOCKSTEP_STATE_CAPTURING_PRETRIGGER) ||
                                (device_state_w == LOCKSTEP_STATE_CAPTURING_POSTTRIGGER) ||
                                (device_state_w == LOCKSTEP_STATE_DRAINING);
  assign event_done_pulse_read_w = event_done_toggle_read_d2_r ^ event_done_toggle_read_d3_r;
  assign event_fifo_write_rst_n_w = event_fifo_write_reset_d2_r;
  assign event_fifo_read_rst_n_w = capture_domain_ready_d2_r;
  assign event_fifo_domains_ready_sample_w = event_fifo_write_rst_n_w &&
                                             event_fifo_read_ready_sample_d2_r;
  assign event_fifo_domains_ready_read_w = event_fifo_read_rst_n_w &&
                                           event_fifo_write_ready_read_d2_r;
  assign event_arb_ready_w = event_fifo_write_ready_w && event_fifo_domains_ready_sample_w;
  assign event_async_valid_w = event_fifo_read_valid_w && event_fifo_domains_ready_read_w;
  assign stop_sample_pulse_w = stop_toggle_sample_d2_r ^ stop_toggle_sample_d3_r;
  assign event_pipeline_draining_read_w =
         (event_draining_read_d2_r || event_end_pending_read_r) &&
         !event_collection_done_read_r;

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      stop_toggle_clk_r <= #UDLY 1'b0;
    end else if (stop_w) begin
      stop_toggle_clk_r <= #UDLY !stop_toggle_clk_r;
    end
  end

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      capture_domain_ready_d1_r <= #UDLY 1'b0;
      capture_domain_ready_d2_r <= #UDLY 1'b0;
    end else begin
      capture_domain_ready_d1_r <= #UDLY sample_rst_n;
      capture_domain_ready_d2_r <= #UDLY capture_domain_ready_d1_r;
    end
  end

  // 两侧复位均同步释放后才允许异步 FIFO 传输。
  always @(posedge sample_clk or negedge sample_rst_n) begin
    if (!sample_rst_n) begin
      event_fifo_read_ready_sample_d1_r <= #UDLY 1'b0;
      event_fifo_read_ready_sample_d2_r <= #UDLY 1'b0;
    end else begin
      event_fifo_read_ready_sample_d1_r <= #UDLY event_fifo_read_rst_n_w;
      event_fifo_read_ready_sample_d2_r <= #UDLY event_fifo_read_ready_sample_d1_r;
    end
  end

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      event_fifo_write_ready_read_d1_r <= #UDLY 1'b0;
      event_fifo_write_ready_read_d2_r <= #UDLY 1'b0;
    end else begin
      event_fifo_write_ready_read_d1_r <= #UDLY event_fifo_write_rst_n_w;
      event_fifo_write_ready_read_d2_r <= #UDLY event_fifo_write_ready_read_d1_r;
    end
  end

  always @(posedge sample_clk or negedge sample_rst_n) begin
    if (!sample_rst_n) begin
      event_fifo_write_reset_d1_r <= #UDLY 1'b0;
      event_fifo_write_reset_d2_r <= #UDLY 1'b0;
    end else begin
      event_fifo_write_reset_d1_r <= #UDLY rst_n;
      event_fifo_write_reset_d2_r <= #UDLY event_fifo_write_reset_d1_r;
    end
  end

  // 事件 DRAINING 状态同步到命令/状态响应时钟域。
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      event_draining_read_d1_r <= #UDLY 1'b0;
      event_draining_read_d2_r <= #UDLY 1'b0;
    end else begin
      event_draining_read_d1_r <= #UDLY event_draining_sample_w;
      event_draining_read_d2_r <= #UDLY event_draining_read_d1_r;
    end
  end
  lockstep_global_timestamp #(.UDLY(UDLY)) u_event_global_timestamp (
    .clk                (sample_clk),
    .rst_n              (sample_rst_n),
    .sample_valid_i     (wide_sample_valid_i),
    .sample_abs_index_i (wide_sample_abs_index_i),
    .timestamp_o        (event_global_timestamp_sample_w)
  );

  // 每轮 ARM 清除释放状态；主机确认 ARM ACK 后显式允许事件帧上传。
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      event_stream_released_r <= #UDLY 1'b0;
    end else if (arm_w || cap_seq_done_w) begin
      event_stream_released_r <= #UDLY 1'b0;
    end else if (event_stream_start_w || stop_w) begin
      event_stream_released_r <= #UDLY 1'b1;
    end
  end

  lockstep_capture_stream_arbiter u_capture_stream_arbiter (
    .clk                (clk),
    .rst_n              (rst_n),
    .event_valid_i      (event_frame_valid_w),
    .event_ready_o      (event_frame_ready_w),
    .wide_valid_i       (wide_frame_valid_w),
    .wide_ready_o       (wide_frame_ready_w),
    .downstream_ready_i (cap_frame_ready_w),
    .stream_valid_o     (cap_frame_valid_w),
    .event_select_o     (cap_event_select_w)
  );

  assign cap_frame_type_w = cap_event_select_w ? event_frame_type_w : wide_frame_type_w;
  assign cap_frame_capture_id_w = cap_event_select_w ? event_frame_capture_id_w : wide_frame_capture_id_w;
  assign cap_frame_flags_w = cap_event_select_w ? event_frame_flags_w : wide_frame_flags_w;
  assign cap_payload_word_count_w = cap_event_select_w ? event_payload_word_count_w : wide_payload_word_count_w;
  assign cap_payload0_w = cap_event_select_w ? event_payload0_w : wide_payload0_w;
  assign cap_payload1_w = cap_event_select_w ? event_payload1_w : wide_payload1_w;
  assign cap_payload2_w = cap_event_select_w ? event_payload2_w : wide_payload2_w;
  assign cap_payload3_w = cap_event_select_w ? event_payload3_w : wide_payload3_w;
  assign cap_payload4_w = cap_event_select_w ? event_payload4_w : wide_payload4_w;
  assign cap_payload5_w = cap_event_select_w ? event_payload5_w : wide_payload5_w;
  assign cap_payload6_w = cap_event_select_w ? event_payload6_w : wide_payload6_w;
  assign cap_payload7_w = cap_event_select_w ? event_payload7_w : wide_payload7_w;
  assign cap_payload8_w = cap_event_select_w ? event_payload8_w : wide_payload8_w;
  assign cap_payload9_w = cap_event_select_w ? event_payload9_w : wide_payload9_w;
  assign cap_payload10_w = cap_event_select_w ? event_payload10_w : wide_payload10_w;
  assign cap_payload11_w = cap_event_select_w ? event_payload11_w : wide_payload11_w;
  assign cap_payload12_w = cap_event_select_w ? event_payload12_w : wide_payload12_w;
  assign cap_payload13_w = cap_event_select_w ? event_payload13_w : wide_payload13_w;
  assign cap_payload14_w = cap_event_select_w ? event_payload14_w : wide_payload14_w;
  assign cap_payload15_w = cap_event_select_w ? event_payload15_w : wide_payload15_w;

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      source_enable_o <= #UDLY 1'b0;
    end else begin
      source_enable_o <= #UDLY source_enable_next_w;
    end
  end

  // 稳定配置、capture ID 和 STOP 同步到 sample_clk 事件域。
  always @(posedge sample_clk or negedge sample_rst_n) begin
    if (!sample_rst_n) begin
      event_enable_sample_d1_r <= #UDLY 9'd0;
      event_enable_sample_d2_r <= #UDLY 9'd0;
      capture_id_sample_d1_r <= #UDLY 32'd0;
      capture_id_sample_d2_r <= #UDLY 32'd0;
      cfg_event_watchdog_sample_d1_r <= #UDLY 32'd0;
      cfg_event_watchdog_sample_d2_r <= #UDLY 32'd0;
      cfg_event_hard_timeout_sample_d1_r <= #UDLY 32'd0;
      cfg_event_hard_timeout_sample_d2_r <= #UDLY 32'd0;
      stop_toggle_sample_d1_r <= #UDLY 1'b0;
      stop_toggle_sample_d2_r <= #UDLY 1'b0;
      stop_toggle_sample_d3_r <= #UDLY 1'b0;
      cfg_event_watchdog_active_sample_r <= #UDLY 32'd0;
      cfg_event_hard_timeout_active_sample_r <= #UDLY 32'd0;
    end else begin
      event_enable_sample_d1_r <= #UDLY event_enable_w;
      event_enable_sample_d2_r <= #UDLY event_enable_sample_d1_r;
      capture_id_sample_d1_r <= #UDLY capture_id_w;
      capture_id_sample_d2_r <= #UDLY capture_id_sample_d1_r;
      cfg_event_watchdog_sample_d1_r <= #UDLY cfg_event_watchdog_ticks_w;
      cfg_event_watchdog_sample_d2_r <= #UDLY cfg_event_watchdog_sample_d1_r;
      cfg_event_hard_timeout_sample_d1_r <= #UDLY cfg_event_hard_timeout_ticks_w;
      cfg_event_hard_timeout_sample_d2_r <= #UDLY cfg_event_hard_timeout_sample_d1_r;
      stop_toggle_sample_d1_r <= #UDLY stop_toggle_clk_r;
      stop_toggle_sample_d2_r <= #UDLY stop_toggle_sample_d1_r;
      stop_toggle_sample_d3_r <= #UDLY stop_toggle_sample_d2_r;
      if (wide_trigger_pulse_sample_w) begin
        cfg_event_watchdog_active_sample_r <= #UDLY cfg_event_watchdog_sample_d2_r;
        cfg_event_hard_timeout_active_sample_r <= #UDLY cfg_event_hard_timeout_sample_d2_r;
      end
    end
  end

  lockstep_event_capture_controller u_event_capture_controller (
    .clk                  (sample_clk),
    .rst_n                (sample_rst_n),
    .capture_start_i      (wide_trigger_pulse_sample_w),
    .stop_i               (stop_sample_pulse_w),
    .program_done_i       (1'b0),
    .overflow_i           ((|event_source_drop_w) || (|event_overflow_sample_w)),
    .activity_i           (|event_source_push_w),
    .protocol_busy_i      (event_protocol_busy_sample_w),
    .watchdog_ticks_i     (cfg_event_watchdog_active_sample_r),
    .hard_timeout_ticks_i (cfg_event_hard_timeout_active_sample_r),
    .capture_active_o     (event_capture_active_sample_w),
    .draining_o           (event_draining_sample_w),
    .capture_done_pulse_o (event_controller_done_sample_w),
    .end_reason_o         (event_end_reason_sample_w)
  );

  lockstep_protocol_event_encoder u_protocol_event_encoder (
    .clk                  (sample_clk),
    .rst_n                (sample_rst_n),
    .capture_start_i      (wide_trigger_pulse_sample_w),
    .capture_active_i     (event_capture_active_sample_w),
    .capture_id_i         (capture_id_sample_d2_r),
    .timestamp_i          (event_global_timestamp_sample_w),
    .source_enable_mask_i (event_enable_sample_d2_r),
    .sample_valid_i       (wide_sample_valid_i),
    .sample_i             (wide_sample_i),
    .source_push_o        (event_source_push_w),
    .source_record_o      (event_source_record_w),
    .protocol_busy_o      (event_protocol_busy_sample_w)
  );

  lockstep_event_capture_core #(
    .AHB_FIFO_DEPTH           (4096),
    .AHB_FIFO_ADDR_WIDTH      (12),
    .UART_FIFO_DEPTH          (2048),
    .UART_FIFO_ADDR_WIDTH     (11),
    .SPI_FIFO_DEPTH           (2048),
    .SPI_FIFO_ADDR_WIDTH      (11),
    .CAN_FIFO_DEPTH           (2048),
    .CAN_FIFO_ADDR_WIDTH      (11),
    .I2C_FIFO_DEPTH           (2048),
    .I2C_FIFO_ADDR_WIDTH      (11),
    .ETH_FIFO_DEPTH           (2048),
    .ETH_FIFO_ADDR_WIDTH      (11),
    .USB_FIFO_DEPTH           (2048),
    .USB_FIFO_ADDR_WIDTH      (11),
    .JTAG_FIFO_DEPTH          (4096),
    .JTAG_FIFO_ADDR_WIDTH     (12),
    .MISMATCH_FIFO_DEPTH      (2048),
    .MISMATCH_FIFO_ADDR_WIDTH (11)
  ) u_event_capture_core (
    .clk                  (sample_clk),
    .rst_n                (sample_rst_n),
    .capture_start_i      (wide_trigger_pulse_sample_w),
    .source_enable_mask_i (event_enable_sample_d2_r),
    .source_push_i        (event_source_push_w),
    .source_record_i      (event_source_record_w),
    .source_accept_o      (),
    .source_drop_o        (event_source_drop_w),
    .event_valid_o        (event_arb_valid_w),
    .event_ready_i        (event_arb_ready_w),
    .event_record_o       (event_arb_record_w),
    .event_source_o       (event_arb_source_w),
    .overflow_mask_o      (event_overflow_sample_w),
    .accepted_count_o     (),
    .emitted_count_o      (),
    .dropped_count_o      (event_dropped_sample_w)
  );

  lockstep_event_async_fifo #(
    .ADDR_WIDTH (12),
    .DEPTH      (4096)
  ) u_event_async_fifo (
    .write_clk     (sample_clk),
    .write_rst_n   (event_fifo_write_rst_n_w),
    .write_valid_i (event_arb_valid_w && event_fifo_domains_ready_sample_w),
    .write_ready_o (event_fifo_write_ready_w),
    .write_data_i  (event_arb_record_w),
    .write_drop_o  (event_async_drop_w),
    .read_clk      (clk),
    .read_rst_n    (event_fifo_read_rst_n_w),
    .read_valid_o  (event_fifo_read_valid_w),
    .read_empty_o  (event_async_empty_w),
    .read_ready_i  (event_async_ready_w && event_fifo_domains_ready_read_w),
    .read_data_o   (event_async_record_w)
  );

  // 控制器结束后等待各协议 FIFO 完全排空，再跨域发布稳定统计快照。
  always @(posedge sample_clk or negedge sample_rst_n) begin
    if (!sample_rst_n) begin
      event_controller_ended_sample_r <= #UDLY 1'b0;
      event_done_toggle_sample_r <= #UDLY 1'b0;
      event_end_reason_stable_sample_r <= #UDLY 32'd0;
      event_overflow_stable_sample_r <= #UDLY 9'd0;
      event_dropped_stable_sample_r <= #UDLY {9*32{1'b0}};
    end else if (wide_trigger_pulse_sample_w) begin
      event_controller_ended_sample_r <= #UDLY 1'b0;
    end else begin
      if (event_controller_done_sample_w) begin
        event_controller_ended_sample_r <= #UDLY 1'b1;
      end
      if (event_controller_ended_sample_r && !event_arb_valid_w) begin
        event_controller_ended_sample_r <= #UDLY 1'b0;
        event_done_toggle_sample_r <= #UDLY !event_done_toggle_sample_r;
        event_end_reason_stable_sample_r <= #UDLY event_end_reason_sample_w;
        event_overflow_stable_sample_r <= #UDLY event_overflow_sample_w;
        event_dropped_stable_sample_r <= #UDLY event_dropped_sample_w;
      end
    end
  end

  // 源域结束到达后，等待异步 FIFO 在读域明确排空再发布统计快照。
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      event_done_toggle_read_d1_r <= #UDLY 1'b0;
      event_done_toggle_read_d2_r <= #UDLY 1'b0;
      event_done_toggle_read_d3_r <= #UDLY 1'b0;
      event_end_pending_read_r <= #UDLY 1'b0;
      event_collection_done_read_r <= #UDLY 1'b0;
      event_end_reason_read_r <= #UDLY 32'd0;
      event_overflow_read_r <= #UDLY 9'd0;
      event_dropped_read_r <= #UDLY {9*32{1'b0}};
    end else begin
      event_done_toggle_read_d1_r <= #UDLY event_done_toggle_sample_r;
      event_done_toggle_read_d2_r <= #UDLY event_done_toggle_read_d1_r;
      event_done_toggle_read_d3_r <= #UDLY event_done_toggle_read_d2_r;
      if (wide_arm_accepted_w) begin
        event_end_pending_read_r <= #UDLY 1'b0;
        event_collection_done_read_r <= #UDLY 1'b0;
      end else if (event_done_pulse_read_w) begin
        event_end_pending_read_r <= #UDLY 1'b1;
      end else if (event_end_pending_read_r && event_async_empty_w) begin
        event_end_pending_read_r <= #UDLY 1'b0;
        event_collection_done_read_r <= #UDLY 1'b1;
        event_end_reason_read_r <= #UDLY event_end_reason_stable_sample_r;
        event_overflow_read_r <= #UDLY event_overflow_stable_sample_r;
        event_dropped_read_r <= #UDLY event_dropped_stable_sample_r;
      end
    end
  end

  lockstep_rx_command_parser u_rx_command_parser (
    .clk                 (clk),
    .rst_n               (rst_n),
    .rx_word_valid_i     (rx_word_valid_i),
    .rx_word_ready_o     (rx_word_ready_o),
    .rx_word_data_i      (rx_word_data_i),
    .rx_be_valid_i       (rx_be_valid_i),
    .cmd_valid_o         (cmd_valid_w),
    .cmd_ready_i         (cmd_ready_w),
    .cmd_type_o          (cmd_type_w),
    .cmd_seq_o           (cmd_seq_w),
    .cmd_capture_id_o    (cmd_capture_id_w),
    .cmd_payload_words_o (cmd_payload_words_w),
    .cmd_payload0_o      (cmd_payload0_w),
    .cmd_payload1_o      (cmd_payload1_w),
    .cmd_payload2_o      (cmd_payload2_w),
    .cmd_payload3_o      (cmd_payload3_w),
    .cmd_payload4_o      (cmd_payload4_w),
    .cmd_payload5_o      (cmd_payload5_w),
    .cmd_payload6_o      (cmd_payload6_w),
    .cmd_payload7_o      (cmd_payload7_w),
    .cmd_payload8_o      (cmd_payload8_w),
    .cmd_payload9_o      (cmd_payload9_w),
    .cmd_payload10_o     (cmd_payload10_w),
    .cmd_payload11_o     (cmd_payload11_w),
    .cmd_payload12_o     (cmd_payload12_w),
    .error_valid_o       (parser_error_valid_w),
    .error_ready_i       (parser_error_ready_w),
    .error_code_o        (parser_error_code_w),
    .error_type_o        (parser_error_type_w),
    .error_seq_o         (parser_error_seq_w),
    .error_detail0_o     (parser_error_detail0_w),
    .error_detail1_o     (parser_error_detail1_w),
    .debug_state_o       (debug_parser_state_o)
  );

  lockstep_command_state_machine #(
    .MAX_SAMPLE_COUNT        (32'd4096),
    .HELLO_PHYSICAL_CHANNELS (PROBE_SAMPLE_BITS),
    .HELLO_SAMPLE_WORD_BITS  (PROBE_SAMPLE_BITS)
  ) u_command_state_machine (
    .clk                          (clk),
    .rst_n                        (rst_n),
    .cmd_valid_i                  (cmd_valid_w),
    .cmd_ready_o                  (cmd_ready_w),
    .cmd_type_i                   (cmd_type_w),
    .cmd_seq_i                    (cmd_seq_w),
    .cmd_payload_words_i          (cmd_payload_words_w),
    .cmd_payload0_i               (cmd_payload0_w),
    .cmd_payload1_i               (cmd_payload1_w),
    .cmd_payload2_i               (cmd_payload2_w),
    .cmd_payload3_i               (cmd_payload3_w),
    .cmd_payload4_i               (cmd_payload4_w),
    .cmd_payload5_i               (cmd_payload5_w),
    .cmd_payload6_i               (cmd_payload6_w),
    .cmd_payload7_i               (cmd_payload7_w),
    .cmd_payload8_i               (cmd_payload8_w),
    .cmd_payload9_i               (cmd_payload9_w),
    .cmd_payload10_i              (cmd_payload10_w),
    .cmd_payload11_i              (cmd_payload11_w),
    .cmd_payload12_i              (cmd_payload12_w),
    .cmd_error_valid_i            (parser_error_valid_w),
    .cmd_error_ready_o            (parser_error_ready_w),
    .cmd_error_code_i             (parser_error_code_w),
    .cmd_error_type_i             (parser_error_type_w),
    .cmd_error_seq_i              (parser_error_seq_w),
    .cmd_error_detail0_i          (parser_error_detail0_w),
    .cmd_error_detail1_i          (parser_error_detail1_w),
    .cfg_valid_o                  (cfg_valid_w),
    .cfg_ready_i                  (cfg_ready_w),
    .cfg_sample_rate_hz_o         (cfg_sample_rate_hz_w),
    .cfg_sample_count_o           (cfg_sample_count_w),
    .cfg_pretrigger_count_o       (cfg_pretrigger_count_w),
    .cfg_posttrigger_count_o      (cfg_posttrigger_count_w),
    .cfg_channel_mask_o           (cfg_channel_mask_w),
    .cfg_input_invert_mask_o      (cfg_input_invert_mask_w),
    .cfg_physical_channels_o      (cfg_physical_channels_w),
    .cfg_sample_word_bits_o       (cfg_sample_word_bits_w),
    .cfg_trigger_mask_o           (cfg_trigger_mask_w),
    .cfg_trigger_value_o          (cfg_trigger_value_w),
    .cfg_trigger_edge_rise_o      (cfg_trigger_edge_rise_w),
    .cfg_trigger_edge_fall_o      (cfg_trigger_edge_fall_w),
    .cfg_mode_o                   (cfg_mode_w),
    .cfg_trigger_timeout_samples_o(cfg_trigger_timeout_samples_w),
    .cfg_event_enable_mask_o      (cfg_event_enable_mask_w),
    .cfg_event_limit_o            (cfg_event_limit_w),
    .cfg_event_watchdog_ticks_o   (cfg_event_watchdog_ticks_w),
    .cfg_event_hard_timeout_ticks_o(cfg_event_hard_timeout_ticks_w),
    .cfg_error_valid_i            (cfg_error_valid_w),
    .cfg_error_code_i             (cfg_error_code_w),
    .cfg_error_detail0_i          (cfg_error_detail0_w),
    .cfg_error_detail1_i          (cfg_error_detail1_w),
    .arm_o                        (arm_w),
    .arm_accepted_i               (wide_arm_accepted_w),
    .capture_domain_ready_i       (capture_domain_ready_d2_r && event_fifo_domains_ready_read_w),
    .event_stream_start_o         (event_stream_start_w),
    .stop_o                       (stop_w),
    .capture_trigger_seen_i       (wide_trigger_seen_w),
    .capture_draining_i           (event_pipeline_draining_read_w),
    .capture_frame_done_i         (cap_seq_done_w),
    .capture_samples_captured_i   (wide_meta_valid_w ? wide_meta_window_sample_count_w : wide_samples_seen_w),
    .capture_samples_uploaded_i   (cap_samples_uploaded_w),
    .capture_device_status_flags_i(cap_device_status_flags_w | capture_external_status_flags_i),
    .capture_debug_flags_i         ({24'd0, wide_watchdog_expired_w, wide_trigger_seen_w,
                                     wide_meta_valid_w, wide_abort_pulse_w, wide_arm_accepted_w,
                                     wide_arm_ready_w, 2'd0}),
    .capture_window_state_i        ({28'd0, wide_capture_state_w}),
    .capture_pretrigger_samples_i  (wide_samples_seen_w),
    .capture_posttrigger_samples_i (wide_post_seen_w),
    .capture_frame_source_state_i  (cap_frame_source_state_w),
    .capture_tx_generator_state_i  (debug_generator_state_o),
    .capture_ft601_state_i         (capture_external_debug_state_i),
    .capture_tx_bytes_i             (cap_samples_uploaded_w),
    .frame_valid_o                (cmd_frame_valid_w),
    .frame_ready_i                (cmd_frame_ready_w),
    .frame_type_o                 (cmd_frame_type_w),
    .frame_capture_id_o           (cmd_frame_capture_id_w),
    .frame_flags_o                (cmd_frame_flags_w),
    .payload_word_count_o         (cmd_payload_word_count_w),
    .payload0_o                   (cmd_frame_payload0_w),
    .payload1_o                   (cmd_frame_payload1_w),
    .payload2_o                   (cmd_frame_payload2_w),
    .payload3_o                   (cmd_frame_payload3_w),
    .payload4_o                   (cmd_frame_payload4_w),
    .payload5_o                   (cmd_frame_payload5_w),
    .payload6_o                   (cmd_frame_payload6_w),
    .payload7_o                   (cmd_frame_payload7_w),
    .payload8_o                   (cmd_frame_payload8_w),
    .payload9_o                   (cmd_frame_payload9_w),
    .payload10_o                  (cmd_frame_payload10_w),
    .payload11_o                  (cmd_frame_payload11_w),
    .payload12_o                  (cmd_frame_payload12_w),
    .payload13_o                  (cmd_frame_payload13_w),
    .payload14_o                  (cmd_frame_payload14_w),
    .payload15_o                  (cmd_frame_payload15_w),
    .capture_id_o                 (capture_id_w),
    .device_state_o               (device_state_w),
    .last_error_code_o            (last_error_code_w),
    .debug_state_o                (debug_command_state_o)
  );

  lockstep_wide_capture_window_cdc #(
    .PROBE_SAMPLE_BITS       (PROBE_SAMPLE_BITS),
    .MAX_PROBE_SAMPLE_BITS   (MAX_PROBE_SAMPLE_BITS),
    .LANE_BITS               (PROBE_LANE_BITS),
    .LANE_INDEX_BITS         (LANE_INDEX_BITS),
    .PROTOCOL_COUNT          (PROTOCOL_COUNT),
    .SAMPLE_ADDR_WIDTH       (SAMPLE_ADDR_WIDTH),
    .AHB_TRIGGER_MODE        (AHB_TRIGGER_MODE)
  ) u_wide_capture_window (
    .sample_clk                     (sample_clk),
    .sample_rst_n                   (sample_rst_n),
    .read_clk                       (clk),
    .read_rst_n                     (rst_n),
    .arm_i                          (arm_w),
    .stop_i                         (window_stop_w),
    .trigger_timeout_samples_i      (cfg_trigger_timeout_samples_w),
    .protocol_enable_i              (protocol_enable_w),
    .trigger_addr_i                 (cfg_trigger_value_w),
    .trigger_mismatch_enable_i      (cfg_trigger_edge_rise_w[0]),
    .trigger_mismatch_mask_i        (cfg_trigger_mask_w[4:0]),
    .sample_valid_i                 (wide_sample_valid_i),
    .sample_i                       (wide_sample_i),
    .sample_abs_index_i             (wide_sample_abs_index_i),
    .arm_ready_o                    (wide_arm_ready_w),
    .arm_accepted_o                 (wide_arm_accepted_w),
    .busy_o                         (wide_busy_w),
    .done_o                         (wide_done_w),
    .done_pulse_o                   (wide_done_pulse_w),
    .upload_allowed_o               (wide_upload_allowed_w),
    .param_error_o                  (wide_param_error_w),
    .pretrigger_ready_o             (wide_pretrigger_ready_w),
    .meta_valid_o                   (wide_meta_valid_w),
    .meta_protocol_enable_o         (wide_meta_protocol_enable_w),
    .meta_protocol_count_o          (wide_meta_protocol_count_w),
    .meta_sample_bits_o             (wide_meta_sample_bits_w),
    .meta_lane_count_o              (wide_meta_lane_count_w),
    .meta_max_lane_count_o          (wide_meta_max_lane_count_w),
    .meta_window_sample_count_o     (wide_meta_window_sample_count_w),
    .meta_pretrigger_count_o        (wide_meta_pretrigger_count_w),
    .meta_trigger_count_o           (wide_meta_trigger_count_w),
    .meta_post_after_trigger_count_o(wide_meta_post_count_w),
    .meta_window_start_abs_index_o  (wide_meta_window_start_w),
    .meta_trigger_abs_index_o       (wide_meta_trigger_abs_index_w),
    .meta_trigger_sample_index_o    (wide_meta_trigger_sample_index_w),
    .samples_seen_o                 (wide_samples_seen_w),
    .post_after_trigger_seen_o      (wide_post_seen_w),
    .abort_pulse_o                  (wide_abort_pulse_w),
    .watchdog_pulse_o               (wide_watchdog_pulse_w),
    .trigger_seen_o                 (wide_trigger_seen_w),
    .trigger_pulse_sample_o         (wide_trigger_pulse_sample_w),
    .watchdog_expired_o             (wide_watchdog_expired_w),
    .read_req_i                     (wide_read_req_w),
    .read_sample_index_i            (wide_read_sample_index_w),
    .read_lane_index_i              (wide_read_lane_index_w),
    .read_ready_o                   (wide_read_ready_w),
    .read_valid_o                   (wide_read_valid_w),
    .read_error_o                   (wide_read_error_w),
    .read_data_o                    (wide_read_data_w),
    .read_sample_index_o            (),
    .read_lane_index_o              (),
    .read_last_lane_o               (wide_read_last_lane_w),
    .read_last_sample_o             (wide_read_last_sample_w),
    .state_o                        (wide_capture_state_w)
  );

  lockstep_wide_capture_frame_source #(
    .PROBE_SAMPLE_BITS     (PROBE_SAMPLE_BITS),
    .MAX_PROBE_SAMPLE_BITS (MAX_PROBE_SAMPLE_BITS),
    .LANE_BITS             (PROBE_LANE_BITS),
    .LANE_INDEX_BITS       (LANE_INDEX_BITS),
    .PROTOCOL_COUNT        (PROTOCOL_COUNT),
    .SAMPLE_ADDR_WIDTH     (SAMPLE_ADDR_WIDTH)
  ) u_wide_capture_frame_source (
    .clk                                (clk),
    .rst_n                              (rst_n),
    .cfg_valid_i                        (cfg_valid_w),
    .cfg_ready_o                        (cfg_ready_w),
    .cfg_sample_rate_hz_i               (cfg_sample_rate_hz_w),
    .cfg_sample_count_i                 (cfg_sample_count_w),
    .cfg_pretrigger_count_i             (cfg_pretrigger_count_w),
    .cfg_posttrigger_count_i            (cfg_posttrigger_count_w),
    .cfg_channel_mask_i                 (cfg_channel_mask_w),
    .cfg_input_invert_mask_i            (cfg_input_invert_mask_w),
    .cfg_physical_channels_i            (cfg_physical_channels_w),
    .cfg_sample_word_bits_i             (cfg_sample_word_bits_w),
    .cfg_mode_i                         (cfg_mode_w),
    .cfg_error_valid_o                  (cfg_error_valid_w),
    .cfg_error_code_o                   (cfg_error_code_w),
    .cfg_error_detail0_o                (cfg_error_detail0_w),
    .cfg_error_detail1_o                (cfg_error_detail1_w),
    .arm_i                              (wide_arm_accepted_w),
    .stop_i                             (frame_stop_w),
    .no_trigger_i                       (wide_watchdog_pulse_w),
    .capture_id_i                       (capture_id_w),
    .defer_capture_end_i                 (wide_trigger_seen_w && (event_enable_w != 9'd0)),
    .external_stream_done_i              (event_frame_done_w),
    .wide_meta_valid_i                  (wide_meta_valid_w),
    .wide_param_error_i                 (wide_param_error_w),
    .wide_meta_protocol_enable_i        (wide_meta_protocol_enable_w),
    .wide_meta_protocol_count_i         (wide_meta_protocol_count_w),
    .wide_meta_sample_bits_i            (wide_meta_sample_bits_w),
    .wide_meta_window_sample_count_i    (wide_meta_window_sample_count_w),
    .wide_meta_pretrigger_count_i       (wide_meta_pretrigger_count_w),
    .wide_meta_post_after_trigger_count_i(wide_meta_post_count_w),
    .wide_meta_window_start_abs_index_i (wide_meta_window_start_w),
    .wide_meta_trigger_abs_index_i      (wide_meta_trigger_abs_index_w),
    .wide_read_req_o                    (wide_read_req_w),
    .wide_read_sample_index_o           (wide_read_sample_index_w),
    .wide_read_lane_index_o             (wide_read_lane_index_w),
    .wide_read_ready_i                  (wide_read_ready_w),
    .wide_read_valid_i                  (wide_read_valid_w),
    .wide_read_error_i                  (wide_read_error_w),
    .wide_read_data_i                   (wide_read_data_w),
    .frame_valid_o                      (wide_frame_valid_w),
    .frame_ready_i                      (wide_frame_ready_w),
    .frame_type_o                       (wide_frame_type_w),
    .frame_capture_id_o                 (wide_frame_capture_id_w),
    .frame_flags_o                      (wide_frame_flags_w),
    .payload_word_count_o               (wide_payload_word_count_w),
    .payload0_o                         (wide_payload0_w),
    .payload1_o                         (wide_payload1_w),
    .payload2_o                         (wide_payload2_w),
    .payload3_o                         (wide_payload3_w),
    .payload4_o                         (wide_payload4_w),
    .payload5_o                         (wide_payload5_w),
    .payload6_o                         (wide_payload6_w),
    .payload7_o                         (wide_payload7_w),
    .payload8_o                         (wide_payload8_w),
    .payload9_o                         (wide_payload9_w),
    .payload10_o                        (wide_payload10_w),
    .payload11_o                        (wide_payload11_w),
    .payload12_o                        (wide_payload12_w),
    .payload13_o                        (wide_payload13_w),
    .payload14_o                        (wide_payload14_w),
    .payload15_o                        (wide_payload15_w),
    .payload_done_o                     (event_payload_done_w),
    .done_o                             (cap_seq_done_w),
    .samples_uploaded_o                 (cap_samples_uploaded_w),
    .device_status_flags_o              (cap_device_status_flags_w),
    .state_o                            (cap_frame_source_state_w)
  );

  lockstep_event_frame_source u_event_frame_source (
    .clk                       (clk),
    .rst_n                     (rst_n),
    .start_i                   (event_stream_released_r && wide_trigger_seen_w &&
                                (event_enable_w != 9'd0)),
    .capture_id_i              (capture_id_w),
    .sample_rate_hz_i          (cfg_sample_rate_hz_w),
    .implemented_source_mask_i (9'h19f),
    .enabled_source_mask_i     (event_enable_w),
    .design_gap_mask_i         (9'h060),
    .watchdog_ticks_i          (cfg_event_watchdog_ticks_w),
    .hard_timeout_ticks_i      (cfg_event_hard_timeout_ticks_w),
    .collection_done_i         (event_collection_done_read_r),
    .collection_end_reason_i   (event_end_reason_read_r),
    .overflow_mask_i           (event_overflow_read_r),
    .dropped_count_i           (event_dropped_read_r),
    .event_valid_i             (event_async_valid_w),
    .event_ready_o             (event_async_ready_w),
    .event_record_i            (event_async_record_w),
    .frame_valid_o             (event_frame_valid_w),
    .frame_ready_i             (event_frame_ready_w),
    .frame_type_o              (event_frame_type_w),
    .frame_capture_id_o        (event_frame_capture_id_w),
    .frame_flags_o             (event_frame_flags_w),
    .payload_word_count_o      (event_payload_word_count_w),
    .payload0_o                (event_payload0_w),
    .payload1_o                (event_payload1_w),
    .payload2_o                (event_payload2_w),
    .payload3_o                (event_payload3_w),
    .payload4_o                (event_payload4_w),
    .payload5_o                (event_payload5_w),
    .payload6_o                (event_payload6_w),
    .payload7_o                (event_payload7_w),
    .payload8_o                (event_payload8_w),
    .payload9_o                (event_payload9_w),
    .payload10_o               (event_payload10_w),
    .payload11_o               (event_payload11_w),
    .payload12_o               (event_payload12_w),
    .payload13_o               (event_payload13_w),
    .payload14_o               (event_payload14_w),
    .payload15_o               (event_payload15_w),
    .done_o                    (event_frame_done_w),
    .state_o                   ()
  );

  lockstep_frame_request_arbiter u_frame_request_arbiter (
    .clk                      (clk),
    .rst_n                    (rst_n),
    .cmd_frame_valid_i        (cmd_frame_valid_w),
    .cmd_frame_ready_o        (cmd_frame_ready_w),
    .cmd_frame_type_i         (cmd_frame_type_w),
    .cmd_frame_capture_id_i   (cmd_frame_capture_id_w),
    .cmd_frame_flags_i        (cmd_frame_flags_w),
    .cmd_payload_word_count_i (cmd_payload_word_count_w),
    .cmd_payload0_i           (cmd_frame_payload0_w),
    .cmd_payload1_i           (cmd_frame_payload1_w),
    .cmd_payload2_i           (cmd_frame_payload2_w),
    .cmd_payload3_i           (cmd_frame_payload3_w),
    .cmd_payload4_i           (cmd_frame_payload4_w),
    .cmd_payload5_i           (cmd_frame_payload5_w),
    .cmd_payload6_i           (cmd_frame_payload6_w),
    .cmd_payload7_i           (cmd_frame_payload7_w),
    .cmd_payload8_i           (cmd_frame_payload8_w),
    .cmd_payload9_i           (cmd_frame_payload9_w),
    .cmd_payload10_i          (cmd_frame_payload10_w),
    .cmd_payload11_i          (cmd_frame_payload11_w),
    .cmd_payload12_i          (cmd_frame_payload12_w),
    .cmd_payload13_i          (cmd_frame_payload13_w),
    .cmd_payload14_i          (cmd_frame_payload14_w),
    .cmd_payload15_i          (cmd_frame_payload15_w),
    .cap_frame_valid_i        (cap_frame_valid_w),
    .cap_frame_ready_o        (cap_frame_ready_w),
    .cap_frame_type_i         (cap_frame_type_w),
    .cap_frame_capture_id_i   (cap_frame_capture_id_w),
    .cap_frame_flags_i        (cap_frame_flags_w),
    .cap_payload_word_count_i (cap_payload_word_count_w),
    .cap_payload0_i           (cap_payload0_w),
    .cap_payload1_i           (cap_payload1_w),
    .cap_payload2_i           (cap_payload2_w),
    .cap_payload3_i           (cap_payload3_w),
    .cap_payload4_i           (cap_payload4_w),
    .cap_payload5_i           (cap_payload5_w),
    .cap_payload6_i           (cap_payload6_w),
    .cap_payload7_i           (cap_payload7_w),
    .cap_payload8_i           (cap_payload8_w),
    .cap_payload9_i           (cap_payload9_w),
    .cap_payload10_i          (cap_payload10_w),
    .cap_payload11_i          (cap_payload11_w),
    .cap_payload12_i          (cap_payload12_w),
    .cap_payload13_i          (cap_payload13_w),
    .cap_payload14_i          (cap_payload14_w),
    .cap_payload15_i          (cap_payload15_w),
    .frame_valid_o            (arb_frame_valid_w),
    .frame_ready_i            (arb_frame_ready_w),
    .frame_type_o             (arb_frame_type_w),
    .frame_capture_id_o       (arb_frame_capture_id_w),
    .frame_flags_o            (arb_frame_flags_w),
    .payload_word_count_o     (arb_payload_word_count_w),
    .payload0_o               (arb_payload0_w),
    .payload1_o               (arb_payload1_w),
    .payload2_o               (arb_payload2_w),
    .payload3_o               (arb_payload3_w),
    .payload4_o               (arb_payload4_w),
    .payload5_o               (arb_payload5_w),
    .payload6_o               (arb_payload6_w),
    .payload7_o               (arb_payload7_w),
    .payload8_o               (arb_payload8_w),
    .payload9_o               (arb_payload9_w),
    .payload10_o              (arb_payload10_w),
    .payload11_o              (arb_payload11_w),
    .payload12_o              (arb_payload12_w),
    .payload13_o              (arb_payload13_w),
    .payload14_o              (arb_payload14_w),
    .payload15_o              (arb_payload15_w),
    .debug_grant_o            ()
  );

  lockstep_tx_frame_generator u_tx_frame_generator (
    .clk                  (clk),
    .rst_n                (rst_n),
    .frame_valid_i        (arb_frame_valid_w),
    .frame_ready_o        (arb_frame_ready_w),
    .frame_type_i         (arb_frame_type_w),
    .frame_capture_id_i   (arb_frame_capture_id_w),
    .frame_flags_i        (arb_frame_flags_w),
    .payload_word_count_i (arb_payload_word_count_w),
    .payload0_i           (arb_payload0_w),
    .payload1_i           (arb_payload1_w),
    .payload2_i           (arb_payload2_w),
    .payload3_i           (arb_payload3_w),
    .payload4_i           (arb_payload4_w),
    .payload5_i           (arb_payload5_w),
    .payload6_i           (arb_payload6_w),
    .payload7_i           (arb_payload7_w),
    .payload8_i           (arb_payload8_w),
    .payload9_i           (arb_payload9_w),
    .payload10_i          (arb_payload10_w),
    .payload11_i          (arb_payload11_w),
    .payload12_i          (arb_payload12_w),
    .payload13_i          (arb_payload13_w),
    .payload14_i          (arb_payload14_w),
    .payload15_i          (arb_payload15_w),
    .tx_word_valid_o      (tx_word_valid_o),
    .tx_word_ready_i      (tx_word_ready_i),
    .tx_word_data_o       (tx_word_data_o),
    .tx_frame_start_o     (tx_frame_start_o),
    .tx_frame_end_o       (tx_frame_end_o),
    .tx_frame_type_o      (tx_frame_type_o),
    .tx_frame_seq_o       (tx_frame_seq_o),
    .debug_state_o        (debug_generator_state_o)
  );

  assign debug_device_state_o = device_state_w;
  assign debug_capture_id_o = capture_id_w;
  assign debug_cfg_trigger_value_o = cfg_trigger_value_w;
  assign debug_cfg_valid_o = cfg_valid_w;
  assign debug_capture_state_o = {28'd0, wide_capture_state_w};
  assign debug_sequencer_state_o = cap_frame_source_state_w;
  assign debug_wide_capture_state_o = {
    20'd0,
    wide_arm_ready_w,
    wide_busy_w,
    wide_done_w,
    wide_done_pulse_w,
    wide_upload_allowed_w,
    wide_param_error_w,
    wide_pretrigger_ready_w,
    wide_meta_valid_w,
    wide_capture_state_w
  };
  assign debug_wide_capture_metadata_o = {
    wide_meta_valid_w,
    wide_upload_allowed_w,
    wide_param_error_w,
    wide_pretrigger_ready_w,
    wide_meta_protocol_enable_w[7:0],
    wide_meta_lane_count_w[3:0],
    wide_meta_max_lane_count_w[3:0],
    wide_meta_sample_bits_w[11:0]
  };
  assign debug_wide_capture_samples_seen_o = wide_samples_seen_w;
  assign debug_wide_capture_flags_o = {24'd0, wide_watchdog_expired_w, wide_trigger_seen_w,
                                       wide_meta_valid_w, wide_abort_pulse_w, wide_arm_accepted_w,
                                       wide_arm_ready_w, 2'd0};
  assign debug_wide_pretrigger_samples_o = wide_samples_seen_w;
  assign debug_wide_posttrigger_samples_o = wide_post_seen_w;
  assign debug_frame_source_state_o = cap_frame_source_state_w;
  assign debug_tx_bytes_o = cap_samples_uploaded_w;

endmodule


