/**********************************************************
* 文件名: lockstep_wide_capture_frame_source.v
* 日期: 2026-07-05
* 版本: 0.1
* 更新记录:
*   0.1 新增 1024-bit 宽窗口上传帧源。
* 描述: 将 lockstep_wide_capture_window 的 read 口输出聚合为 LOCKSTEP 上行帧。
**********************************************************/

`timescale 1ns/1ps

module lockstep_wide_capture_frame_source (
  clk,
  rst_n,
  cfg_valid_i,
  cfg_ready_o,
  cfg_sample_rate_hz_i,
  cfg_sample_count_i,
  cfg_pretrigger_count_i,
  cfg_posttrigger_count_i,
  cfg_channel_mask_i,
  cfg_input_invert_mask_i,
  cfg_physical_channels_i,
  cfg_sample_word_bits_i,
  cfg_mode_i,
  cfg_error_valid_o,
  cfg_error_code_o,
  cfg_error_detail0_o,
  cfg_error_detail1_o,
  arm_i,
  stop_i,
  no_trigger_i,
  capture_id_i,
  defer_capture_end_i,
  external_stream_done_i,
  wide_meta_valid_i,
  wide_param_error_i,
  wide_meta_protocol_enable_i,
  wide_meta_protocol_count_i,
  wide_meta_sample_bits_i,
  wide_meta_window_sample_count_i,
  wide_meta_pretrigger_count_i,
  wide_meta_post_after_trigger_count_i,
  wide_meta_window_start_abs_index_i,
  wide_meta_trigger_abs_index_i,
  wide_read_req_o,
  wide_read_sample_index_o,
  wide_read_lane_index_o,
  wide_read_ready_i,
  wide_read_valid_i,
  wide_read_error_i,
  wide_read_data_i,
  frame_valid_o,
  frame_ready_i,
  frame_type_o,
  frame_capture_id_o,
  frame_flags_o,
  payload_word_count_o,
  payload0_o,
  payload1_o,
  payload2_o,
  payload3_o,
  payload4_o,
  payload5_o,
  payload6_o,
  payload7_o,
  payload8_o,
  payload9_o,
  payload10_o,
  payload11_o,
  payload12_o,
  payload13_o,
  payload14_o,
  payload15_o,
  payload_done_o,
  done_o,
  samples_uploaded_o,
  device_status_flags_o,
  state_o
);
  parameter UDLY                  = 1;
  parameter PROBE_SAMPLE_BITS     = 384;
  parameter MAX_PROBE_SAMPLE_BITS = 1024;
  parameter LANE_BITS             = 128;
  parameter LANE_INDEX_BITS       = 4;
  parameter PROTOCOL_COUNT        = 8;
  parameter SAMPLE_ADDR_WIDTH     = 12;
  parameter WINDOW_SAMPLE_COUNT   = 4096;

  input                          clk;
  input                          rst_n;
  input                          cfg_valid_i;
  output                         cfg_ready_o;
  input  [31:0]                  cfg_sample_rate_hz_i;
  input  [31:0]                  cfg_sample_count_i;
  input  [31:0]                  cfg_pretrigger_count_i;
  input  [31:0]                  cfg_posttrigger_count_i;
  input  [31:0]                  cfg_channel_mask_i;
  input  [31:0]                  cfg_input_invert_mask_i;
  input  [15:0]                  cfg_physical_channels_i;
  input  [15:0]                  cfg_sample_word_bits_i;
  input  [31:0]                  cfg_mode_i;
  output                         cfg_error_valid_o;
  output [31:0]                  cfg_error_code_o;
  output [31:0]                  cfg_error_detail0_o;
  output [31:0]                  cfg_error_detail1_o;
  input                          arm_i;
  input                          stop_i;
  input                          no_trigger_i;
  input  [31:0]                  capture_id_i;
  input                          defer_capture_end_i;
  input                          external_stream_done_i;
  input                          wide_meta_valid_i;
  input                          wide_param_error_i;
  input  [PROTOCOL_COUNT-1:0]    wide_meta_protocol_enable_i;
  input  [31:0]                  wide_meta_protocol_count_i;
  input  [31:0]                  wide_meta_sample_bits_i;
  input  [31:0]                  wide_meta_window_sample_count_i;
  input  [31:0]                  wide_meta_pretrigger_count_i;
  input  [31:0]                  wide_meta_post_after_trigger_count_i;
  input  [31:0]                  wide_meta_window_start_abs_index_i;
  input  [31:0]                  wide_meta_trigger_abs_index_i;
  output                         wide_read_req_o;
  output [SAMPLE_ADDR_WIDTH-1:0] wide_read_sample_index_o;
  output [LANE_INDEX_BITS-1:0]   wide_read_lane_index_o;
  input                          wide_read_ready_i;
  input                          wide_read_valid_i;
  input                          wide_read_error_i;
  input  [LANE_BITS-1:0]         wide_read_data_i;
  output                         frame_valid_o;
  input                          frame_ready_i;
  output [15:0]                  frame_type_o;
  output [31:0]                  frame_capture_id_o;
  output [31:0]                  frame_flags_o;
  output [31:0]                  payload_word_count_o;
  output [31:0]                  payload0_o;
  output [31:0]                  payload1_o;
  output [31:0]                  payload2_o;
  output [31:0]                  payload3_o;
  output [31:0]                  payload4_o;
  output [31:0]                  payload5_o;
  output [31:0]                  payload6_o;
  output [31:0]                  payload7_o;
  output [31:0]                  payload8_o;
  output [31:0]                  payload9_o;
  output [31:0]                  payload10_o;
  output [31:0]                  payload11_o;
  output [31:0]                  payload12_o;
  output [31:0]                  payload13_o;
  output [31:0]                  payload14_o;
  output [31:0]                  payload15_o;
  output                         payload_done_o;
  output                         done_o;
  output [31:0]                  samples_uploaded_o;
  output [31:0]                  device_status_flags_o;
  output [31:0]                  state_o;

  `ifdef LOCKSTEP_CAPTURE_PROTOCOL_V2_VH
  `undef LOCKSTEP_CAPTURE_PROTOCOL_V2_VH
  `endif
  `include "lockstep_capture_protocol_v2.vh"

  localparam integer ACTIVE_LANE_COUNT    = (PROBE_SAMPLE_BITS + LANE_BITS - 1) / LANE_BITS;
  localparam integer SAMPLE_PAYLOAD_WORDS = (PROBE_SAMPLE_BITS + 31) / 32;
  localparam integer SAMPLE_BYTES         = (PROBE_SAMPLE_BITS + 7) / 8;
  localparam integer SAMPLE_WORDS_PER_LANE = LANE_BITS / 32;
  localparam integer MAX_FRAME_PAYLOAD_WORDS = 16;
  localparam integer LANES_PER_SAMPLE_FRAME = MAX_FRAME_PAYLOAD_WORDS / SAMPLE_WORDS_PER_LANE;
  localparam integer SAMPLE_FRAME_SEGMENT_COUNT =
                     (ACTIVE_LANE_COUNT + LANES_PER_SAMPLE_FRAME - 1) / LANES_PER_SAMPLE_FRAME;
  localparam integer LAST_SEGMENT_PAYLOAD_WORDS =
                     SAMPLE_PAYLOAD_WORDS -
                     ((SAMPLE_FRAME_SEGMENT_COUNT - 1) * MAX_FRAME_PAYLOAD_WORDS);
  localparam [15:0] PROBE_SAMPLE_BITS_16  = PROBE_SAMPLE_BITS;
  localparam [31:0] WINDOW_SAMPLE_COUNT_32 = WINDOW_SAMPLE_COUNT;
  localparam [31:0] WINDOW_SAMPLE_BYTES_32 = WINDOW_SAMPLE_COUNT * SAMPLE_BYTES;
  localparam [31:0] PRETRIGGER_SAMPLE_COUNT_32 = 32'd2047;
  localparam [31:0] POSTTRIGGER_CONFIG_COUNT_32 = 32'd2049;

  localparam [3:0] ST_IDLE        = 4'd0;
  localparam [3:0] ST_WAIT_WINDOW = 4'd1;
  localparam [3:0] ST_SEND_META   = 4'd2;
  localparam [3:0] ST_REQ_LANE    = 4'd3;
  localparam [3:0] ST_WAIT_LANE   = 4'd4;
  localparam [3:0] ST_SEND_SAMPLE = 4'd5;
  localparam [3:0] ST_SEND_END    = 4'd6;
  localparam [3:0] ST_DONE        = 4'd7;
  localparam [3:0] ST_WAIT_EXTERNAL = 4'd8;

  reg [3:0]                  cur_state;
  reg [3:0]                  nxt_state;
  reg [31:0]                 cfg_sample_rate_hz_r;
  reg [31:0]                 cfg_channel_mask_r;
  reg [31:0]                 cfg_input_invert_mask_r;
  reg                        cfg_error_valid_o;
  reg [31:0]                 cfg_error_code_o;
  reg [31:0]                 cfg_error_detail0_o;
  reg [31:0]                 cfg_error_detail1_o;
  reg [SAMPLE_ADDR_WIDTH-1:0] sample_index_r;
  reg [LANE_INDEX_BITS-1:0]  lane_index_r;
  reg [LANE_INDEX_BITS-1:0]  segment_index_r;
  reg [31:0]                 samples_uploaded_r;
  reg [31:0]                 device_status_flags_r;
  reg                        stopped_by_host_r;
  reg                        read_fault_r;
  reg                        stop_pending_r;
  reg                        no_trigger_r;
  reg [31:0]                 payload0_o;
  reg [31:0]                 payload1_o;
  reg [31:0]                 payload2_o;
  reg [31:0]                 payload3_o;
  reg [31:0]                 payload4_o;
  reg [31:0]                 payload5_o;
  reg [31:0]                 payload6_o;
  reg [31:0]                 payload7_o;
  reg [31:0]                 payload8_o;
  reg [31:0]                 payload9_o;
  reg [31:0]                 payload10_o;
  reg [31:0]                 payload11_o;
  reg [31:0]                 payload12_o;
  reg [31:0]                 payload13_o;
  reg [31:0]                 payload14_o;
  reg [31:0]                 payload15_o;

  wire                       cfg_accept_w;
  wire                       cfg_mode_bad_w;
  wire                       cfg_width_bad_w;
  wire                       cfg_count_bad_w;
  wire                       cfg_param_bad_w;
  wire                       cfg_error_fire_w;
  wire                       frame_fire_w;
  wire                       sample_last_w;
  wire                       lane_last_w;
  wire                       segment_last_w;
  wire                       segment_lane_last_w;
  wire                       read_issue_w;
  wire [LANE_INDEX_BITS-1:0] segment_start_lane_w;
  wire [1:0]                 payload_lane_slot_w;
  wire [31:0]                sample_payload_word_count_w;
  wire [31:0]                active_protocol_mask_w;

  assign cfg_ready_o = (cur_state == ST_IDLE) || (cur_state == ST_DONE);
  assign cfg_accept_w = cfg_valid_i && cfg_ready_o;
  assign cfg_mode_bad_w = (cfg_mode_i != LOCKSTEP_MODE_FINITE);
  assign cfg_width_bad_w = (cfg_sample_word_bits_i != PROBE_SAMPLE_BITS_16) ||
                           (cfg_physical_channels_i != PROBE_SAMPLE_BITS_16);
  assign cfg_count_bad_w = (cfg_sample_count_i != WINDOW_SAMPLE_COUNT_32) ||
                           (cfg_pretrigger_count_i != PRETRIGGER_SAMPLE_COUNT_32) ||
                           (cfg_posttrigger_count_i != POSTTRIGGER_CONFIG_COUNT_32);
  assign cfg_param_bad_w = wide_param_error_i ||
                           (PROBE_SAMPLE_BITS <= 0) ||
                           (PROBE_SAMPLE_BITS > MAX_PROBE_SAMPLE_BITS) ||
                           (LANE_BITS != 128) ||
                           (SAMPLE_WORDS_PER_LANE == 0) ||
                           (LANES_PER_SAMPLE_FRAME == 0) ||
                           (LAST_SEGMENT_PAYLOAD_WORDS > MAX_FRAME_PAYLOAD_WORDS);
  assign cfg_error_fire_w = cfg_accept_w && (cfg_mode_bad_w || cfg_width_bad_w ||
                                             cfg_count_bad_w || cfg_param_bad_w);
  assign frame_fire_w = frame_valid_o && frame_ready_i;
  assign sample_last_w = (sample_index_r == (WINDOW_SAMPLE_COUNT - 1));
  assign lane_last_w = (lane_index_r == (ACTIVE_LANE_COUNT - 1));
  assign segment_start_lane_w = segment_index_r * LANES_PER_SAMPLE_FRAME;
  assign payload_lane_slot_w = lane_index_r - segment_start_lane_w;
  assign segment_last_w = (segment_index_r == (SAMPLE_FRAME_SEGMENT_COUNT - 1));
  assign segment_lane_last_w = lane_last_w ||
                               (payload_lane_slot_w == (LANES_PER_SAMPLE_FRAME - 1));
  assign read_issue_w = (cur_state == ST_REQ_LANE) && wide_read_ready_i;
  assign sample_payload_word_count_w = segment_last_w ?
                                       LAST_SEGMENT_PAYLOAD_WORDS :
                                       MAX_FRAME_PAYLOAD_WORDS;
  assign active_protocol_mask_w = {{(32-PROTOCOL_COUNT){1'b0}}, wide_meta_protocol_enable_i};

  assign wide_read_req_o = (cur_state == ST_REQ_LANE);
  assign wide_read_sample_index_o = sample_index_r;
  assign wide_read_lane_index_o = lane_index_r;

  assign frame_valid_o = (cur_state == ST_SEND_META) ||
                         (cur_state == ST_SEND_SAMPLE) ||
                         (cur_state == ST_SEND_END);
  assign frame_type_o = (cur_state == ST_SEND_META) ? LOCKSTEP_FRAME_CAPTURE_META :
                        (cur_state == ST_SEND_SAMPLE) ? LOCKSTEP_FRAME_SAMPLE_DATA :
                        LOCKSTEP_FRAME_CAPTURE_END;
  assign frame_capture_id_o = capture_id_i;
  assign frame_flags_o = (cur_state == ST_SEND_END) ? LOCKSTEP_FRAME_FLAG_LAST_IN_CAPTURE : 32'd0;
  assign payload_word_count_o = (cur_state == ST_SEND_META) ? 32'd10 :
                                (cur_state == ST_SEND_SAMPLE) ? sample_payload_word_count_w :
                                32'd8;
  assign done_o = (cur_state == ST_DONE);
  assign payload_done_o = (cur_state == ST_WAIT_EXTERNAL);
  assign samples_uploaded_o = samples_uploaded_r;
  assign device_status_flags_o = device_status_flags_r;
  assign state_o = {28'd0, cur_state};

  // FSM next-state logic.
  always @(*) begin
    nxt_state = cur_state;

    case (cur_state)
      ST_IDLE: begin
        if (arm_i) begin
          nxt_state = ST_WAIT_WINDOW;
        end
      end

      ST_WAIT_WINDOW: begin
        if (stop_i || stop_pending_r) begin
          nxt_state = defer_capture_end_i ? ST_WAIT_EXTERNAL : ST_SEND_END;
        end else if (wide_meta_valid_i) begin
          nxt_state = ST_SEND_META;
        end
      end

      ST_SEND_META: begin
        if (frame_fire_w) begin
          nxt_state = stop_pending_r ?
                      (defer_capture_end_i ? ST_WAIT_EXTERNAL : ST_SEND_END) : ST_REQ_LANE;
        end
      end

      ST_REQ_LANE: begin
        if (stop_pending_r) begin
          nxt_state = defer_capture_end_i ? ST_WAIT_EXTERNAL : ST_SEND_END;
        end else if (wide_read_ready_i) begin
          nxt_state = ST_WAIT_LANE;
        end
      end

      ST_WAIT_LANE: begin
        if (stop_pending_r) begin
          nxt_state = defer_capture_end_i ? ST_WAIT_EXTERNAL : ST_SEND_END;
        end else if (wide_read_error_i) begin
          nxt_state = defer_capture_end_i ? ST_WAIT_EXTERNAL : ST_SEND_END;
        end else if (wide_read_valid_i && segment_lane_last_w) begin
          nxt_state = ST_SEND_SAMPLE;
        end else if (wide_read_valid_i) begin
          nxt_state = ST_REQ_LANE;
        end
      end

      ST_SEND_SAMPLE: begin
        if (frame_fire_w) begin
          if (stop_pending_r || (sample_last_w && segment_last_w)) begin
            nxt_state = defer_capture_end_i ? ST_WAIT_EXTERNAL : ST_SEND_END;
          end else begin
            nxt_state = ST_REQ_LANE;
          end
        end
      end

      ST_SEND_END: begin
        if (frame_fire_w) begin
          nxt_state = ST_DONE;
        end
      end

      ST_WAIT_EXTERNAL: begin
        if (external_stream_done_i) begin
          nxt_state = ST_SEND_END;
        end
      end

      ST_DONE: begin
        if (arm_i) begin
          nxt_state = ST_WAIT_WINDOW;
        end else if (cfg_accept_w && !cfg_error_fire_w) begin
          nxt_state = ST_IDLE;
        end
      end

      default: begin
        nxt_state = ST_IDLE;
      end
    endcase
  end

  // FSM state register.
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      cur_state <= #UDLY ST_IDLE;
    end else begin
      cur_state <= #UDLY nxt_state;
    end
  end

  // STOP 请求和无触发原因在帧上传期间锁存，保证结束帧原因稳定。
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      stop_pending_r <= #UDLY 1'b0;
      no_trigger_r <= #UDLY 1'b0;
    end else begin
      if (arm_i) begin
        stop_pending_r <= #UDLY 1'b0;
        no_trigger_r <= #UDLY 1'b0;
      end else if (stop_i && (cur_state != ST_IDLE) && (cur_state != ST_DONE)) begin
        stop_pending_r <= #UDLY 1'b1;
        no_trigger_r <= #UDLY no_trigger_i;
      end else if (frame_fire_w && (cur_state == ST_SEND_END)) begin
        stop_pending_r <= #UDLY 1'b0;
        no_trigger_r <= #UDLY 1'b0;
      end
    end
  end

  // Configuration validation pulse.
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      cfg_error_valid_o   <= #UDLY 1'b0;
      cfg_error_code_o    <= #UDLY LOCKSTEP_ERR_NONE;
      cfg_error_detail0_o <= #UDLY 32'd0;
      cfg_error_detail1_o <= #UDLY 32'd0;
    end else begin
      cfg_error_valid_o <= #UDLY cfg_error_fire_w;
      if (cfg_mode_bad_w) begin
        cfg_error_code_o    <= #UDLY LOCKSTEP_ERR_UNSUPPORTED_MODE;
        cfg_error_detail0_o <= #UDLY cfg_mode_i;
        cfg_error_detail1_o <= #UDLY LOCKSTEP_MODE_FINITE;
      end else if (cfg_width_bad_w) begin
        cfg_error_code_o    <= #UDLY LOCKSTEP_ERR_BAD_SAMPLE_WORD_BITS;
        cfg_error_detail0_o <= #UDLY {cfg_sample_word_bits_i, cfg_physical_channels_i};
        cfg_error_detail1_o <= #UDLY PROBE_SAMPLE_BITS;
      end else if (cfg_count_bad_w) begin
        cfg_error_code_o    <= #UDLY LOCKSTEP_ERR_BAD_SAMPLE_COUNT;
        cfg_error_detail0_o <= #UDLY cfg_sample_count_i;
        cfg_error_detail1_o <= #UDLY {cfg_posttrigger_count_i[15:0], cfg_pretrigger_count_i[15:0]};
      end else if (cfg_param_bad_w) begin
        cfg_error_code_o    <= #UDLY LOCKSTEP_ERR_INTERNAL;
        cfg_error_detail0_o <= #UDLY PROBE_SAMPLE_BITS;
        cfg_error_detail1_o <= #UDLY MAX_PROBE_SAMPLE_BITS;
      end else begin
        cfg_error_code_o    <= #UDLY LOCKSTEP_ERR_NONE;
        cfg_error_detail0_o <= #UDLY 32'd0;
        cfg_error_detail1_o <= #UDLY 32'd0;
      end
    end
  end

  // Configuration fields retained for metadata.
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      cfg_sample_rate_hz_r    <= #UDLY 32'd0;
      cfg_channel_mask_r      <= #UDLY 32'd0;
      cfg_input_invert_mask_r <= #UDLY 32'd0;
    end else if (cfg_accept_w && !cfg_error_fire_w) begin
      cfg_sample_rate_hz_r    <= #UDLY cfg_sample_rate_hz_i;
      cfg_channel_mask_r      <= #UDLY cfg_channel_mask_i;
      cfg_input_invert_mask_r <= #UDLY cfg_input_invert_mask_i;
    end
  end

  // Upload position and status tracking.
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      sample_index_r        <= #UDLY {SAMPLE_ADDR_WIDTH{1'b0}};
      lane_index_r          <= #UDLY {LANE_INDEX_BITS{1'b0}};
      segment_index_r       <= #UDLY {LANE_INDEX_BITS{1'b0}};
      samples_uploaded_r    <= #UDLY 32'd0;
      device_status_flags_r <= #UDLY 32'd0;
      stopped_by_host_r     <= #UDLY 1'b0;
      read_fault_r          <= #UDLY 1'b0;
    end else if (arm_i) begin
      sample_index_r        <= #UDLY {SAMPLE_ADDR_WIDTH{1'b0}};
      lane_index_r          <= #UDLY {LANE_INDEX_BITS{1'b0}};
      segment_index_r       <= #UDLY {LANE_INDEX_BITS{1'b0}};
      samples_uploaded_r    <= #UDLY 32'd0;
      device_status_flags_r <= #UDLY 32'd0;
      stopped_by_host_r     <= #UDLY 1'b0;
      read_fault_r          <= #UDLY 1'b0;
    end else begin
      if (stop_i && (cur_state == ST_WAIT_WINDOW)) begin
        stopped_by_host_r     <= #UDLY 1'b1;
        device_status_flags_r <= #UDLY LOCKSTEP_DEVICE_STATUS_FLAG_STOPPED_BY_HOST;
      end else if (wide_read_error_i && (cur_state == ST_WAIT_LANE)) begin
        read_fault_r          <= #UDLY 1'b1;
        device_status_flags_r <= #UDLY LOCKSTEP_DEVICE_STATUS_FLAG_FATAL_INTERNAL_ERROR;
      end

      if (wide_read_valid_i && (cur_state == ST_WAIT_LANE)) begin
        if (!segment_lane_last_w) begin
          lane_index_r <= #UDLY lane_index_r + {{(LANE_INDEX_BITS-1){1'b0}}, 1'b1};
        end
      end

      if (frame_fire_w && (cur_state == ST_SEND_SAMPLE)) begin
        if (segment_last_w) begin
          samples_uploaded_r <= #UDLY samples_uploaded_r + 32'd1;
          lane_index_r       <= #UDLY {LANE_INDEX_BITS{1'b0}};
          segment_index_r    <= #UDLY {LANE_INDEX_BITS{1'b0}};
          if (!sample_last_w) begin
            sample_index_r <= #UDLY sample_index_r + {{(SAMPLE_ADDR_WIDTH-1){1'b0}}, 1'b1};
          end
        end else begin
          lane_index_r    <= #UDLY lane_index_r + {{(LANE_INDEX_BITS-1){1'b0}}, 1'b1};
          segment_index_r <= #UDLY segment_index_r + {{(LANE_INDEX_BITS-1){1'b0}}, 1'b1};
        end
      end
    end
  end

  // Payload registers for all frame types.
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      payload0_o  <= #UDLY 32'd0;
      payload1_o  <= #UDLY 32'd0;
      payload2_o  <= #UDLY 32'd0;
      payload3_o  <= #UDLY 32'd0;
      payload4_o  <= #UDLY 32'd0;
      payload5_o  <= #UDLY 32'd0;
      payload6_o  <= #UDLY 32'd0;
      payload7_o  <= #UDLY 32'd0;
      payload8_o  <= #UDLY 32'd0;
      payload9_o  <= #UDLY 32'd0;
      payload10_o <= #UDLY 32'd0;
      payload11_o <= #UDLY 32'd0;
      payload12_o <= #UDLY 32'd0;
      payload13_o <= #UDLY 32'd0;
      payload14_o <= #UDLY 32'd0;
      payload15_o <= #UDLY 32'd0;
    end else begin
      if ((cur_state == ST_WAIT_WINDOW) && wide_meta_valid_i) begin
        payload0_o  <= #UDLY cfg_sample_rate_hz_r;
        payload1_o  <= #UDLY WINDOW_SAMPLE_COUNT_32;
        payload2_o  <= #UDLY wide_meta_pretrigger_count_i;
        payload3_o  <= #UDLY wide_meta_post_after_trigger_count_i + 32'd1;
        payload4_o  <= #UDLY ((cfg_channel_mask_r == 32'd0) ? active_protocol_mask_w : cfg_channel_mask_r);
        payload5_o  <= #UDLY cfg_input_invert_mask_r;
        payload6_o  <= #UDLY {16'd0, cfg_physical_channels_i};
        payload7_o  <= #UDLY wide_meta_sample_bits_i;
        payload8_o  <= #UDLY wide_meta_window_start_abs_index_i;
        payload9_o  <= #UDLY wide_meta_trigger_abs_index_i;
        payload10_o <= #UDLY 32'd0;
        payload11_o <= #UDLY 32'd0;
        payload12_o <= #UDLY 32'd0;
        payload13_o <= #UDLY 32'd0;
        payload14_o <= #UDLY 32'd0;
        payload15_o <= #UDLY 32'd0;
      end else if (wide_read_valid_i && (cur_state == ST_WAIT_LANE)) begin
        case (payload_lane_slot_w)
          4'd0: begin
            payload0_o <= #UDLY wide_read_data_i[31:0];
            payload1_o <= #UDLY wide_read_data_i[63:32];
            payload2_o <= #UDLY wide_read_data_i[95:64];
            payload3_o <= #UDLY wide_read_data_i[127:96];
          end
          4'd1: begin
            payload4_o <= #UDLY wide_read_data_i[31:0];
            payload5_o <= #UDLY wide_read_data_i[63:32];
            payload6_o <= #UDLY wide_read_data_i[95:64];
            payload7_o <= #UDLY wide_read_data_i[127:96];
          end
          4'd2: begin
            payload8_o  <= #UDLY wide_read_data_i[31:0];
            payload9_o  <= #UDLY wide_read_data_i[63:32];
            payload10_o <= #UDLY wide_read_data_i[95:64];
            payload11_o <= #UDLY wide_read_data_i[127:96];
          end
          default: begin
            payload12_o <= #UDLY wide_read_data_i[31:0];
            payload13_o <= #UDLY wide_read_data_i[63:32];
            payload14_o <= #UDLY wide_read_data_i[95:64];
            payload15_o <= #UDLY wide_read_data_i[127:96];
          end
        endcase
      end else if ((cur_state == ST_SEND_SAMPLE) && frame_fire_w && sample_last_w && segment_last_w) begin
        payload0_o <= #UDLY WINDOW_SAMPLE_COUNT_32;
        payload1_o <= #UDLY wide_meta_window_start_abs_index_i;
        payload2_o <= #UDLY wide_meta_trigger_abs_index_i;
        payload3_o <= #UDLY (no_trigger_r ? LOCKSTEP_STOP_NO_TRIGGER :
                              read_fault_r ? LOCKSTEP_STOP_FATAL_ERROR :
                              stopped_by_host_r ? LOCKSTEP_STOP_HOST_REQUEST :
                              LOCKSTEP_STOP_DEPTH_REACHED);
        payload4_o <= #UDLY device_status_flags_r;
        payload5_o <= #UDLY WINDOW_SAMPLE_BYTES_32;
        payload6_o <= #UDLY WINDOW_SAMPLE_BYTES_32;
        payload7_o <= #UDLY 32'd0;
      end else if (stop_i && (cur_state != ST_IDLE) && (cur_state != ST_DONE)) begin
        payload0_o <= #UDLY samples_uploaded_r;
        payload1_o <= #UDLY wide_meta_window_start_abs_index_i;
        payload2_o <= #UDLY wide_meta_trigger_abs_index_i;
        payload3_o <= #UDLY (no_trigger_r ? LOCKSTEP_STOP_NO_TRIGGER :
                             LOCKSTEP_STOP_HOST_REQUEST);
        payload4_o <= #UDLY LOCKSTEP_DEVICE_STATUS_FLAG_STOPPED_BY_HOST;
        payload5_o <= #UDLY samples_uploaded_r * SAMPLE_BYTES;
        payload6_o <= #UDLY samples_uploaded_r * SAMPLE_BYTES;
        payload7_o <= #UDLY 32'd0;
      end else if ((cur_state == ST_WAIT_LANE) && wide_read_error_i) begin
        payload0_o <= #UDLY samples_uploaded_r;
        payload1_o <= #UDLY wide_meta_window_start_abs_index_i;
        payload2_o <= #UDLY wide_meta_trigger_abs_index_i;
        payload3_o <= #UDLY LOCKSTEP_STOP_FATAL_ERROR;
        payload4_o <= #UDLY LOCKSTEP_DEVICE_STATUS_FLAG_FATAL_INTERNAL_ERROR;
        payload5_o <= #UDLY samples_uploaded_r * SAMPLE_BYTES;
        payload6_o <= #UDLY samples_uploaded_r * SAMPLE_BYTES;
        payload7_o <= #UDLY 32'd0;
      end
    end
  end

endmodule
