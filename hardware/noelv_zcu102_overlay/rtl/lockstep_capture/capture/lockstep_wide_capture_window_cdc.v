/**********************************************************
* 文件名: lockstep_wide_capture_window_cdc.v
* 日期: 2026-07-16
* 版本: 1.0
* 更新记录: ARM 改为 pending/ready 握手，并增加任意 AHB 传输触发值。
* 描述: sample_clk 域负责 AHB/UART/SPI/CAN/I2C/ETH/USB/JTAG
*       最高 1024-bit 宽样本写入和触发窗口冻结；read_clk 域在采集完成后
*       读取 BRAM lane 并交给 FT601 上行帧源。FT601 只参与配置和
*       采后上传，不参与实时采样。
**********************************************************/

`timescale 1ns/1ps

module lockstep_wide_capture_window_cdc (
  sample_clk,
  sample_rst_n,
  read_clk,
  read_rst_n,
  arm_i,
  stop_i,
  trigger_timeout_samples_i,
  protocol_enable_i,
  trigger_addr_i,
  trigger_mismatch_enable_i,
  trigger_mismatch_mask_i,
  sample_valid_i,
  sample_i,
  sample_abs_index_i,
  arm_ready_o,
  arm_accepted_o,
  busy_o,
  done_o,
  done_pulse_o,
  upload_allowed_o,
  param_error_o,
  pretrigger_ready_o,
  meta_valid_o,
  meta_protocol_enable_o,
  meta_protocol_count_o,
  meta_sample_bits_o,
  meta_lane_count_o,
  meta_max_lane_count_o,
  meta_window_sample_count_o,
  meta_pretrigger_count_o,
  meta_trigger_count_o,
  meta_post_after_trigger_count_o,
  meta_window_start_abs_index_o,
  meta_trigger_abs_index_o,
  meta_trigger_sample_index_o,
  samples_seen_o,
  post_after_trigger_seen_o,
  abort_pulse_o,
  watchdog_pulse_o,
  trigger_seen_o,
  trigger_pulse_sample_o,
  watchdog_expired_o,
  read_req_i,
  read_sample_index_i,
  read_lane_index_i,
  read_ready_o,
  read_valid_o,
  read_error_o,
  read_data_o,
  read_sample_index_o,
  read_lane_index_o,
  read_last_lane_o,
  read_last_sample_o,
  state_o
);
  parameter UDLY                     = 1;
  parameter PROBE_SAMPLE_BITS        = 384;
  parameter MAX_PROBE_SAMPLE_BITS    = 1024;
  parameter LANE_BITS                = 128;
  parameter LANE_INDEX_BITS          = 4;
  parameter PROTOCOL_COUNT           = 8;
  parameter SAMPLE_ADDR_WIDTH        = 12;
  parameter WINDOW_SAMPLE_COUNT      = 4096;
  parameter PRETRIGGER_SAMPLE_COUNT  = 2047;
  parameter TRIGGER_SAMPLE_COUNT     = 1;
  parameter POSTTRIGGER_SAMPLE_COUNT = 2048;
  parameter AHB_TRIGGER_MODE         = 1;

  input                               sample_clk;
  input                               sample_rst_n;
  input                               read_clk;
  input                               read_rst_n;
  input                               arm_i;
  input                               stop_i;
  input  [31:0]                       trigger_timeout_samples_i;
  input  [PROTOCOL_COUNT-1:0]         protocol_enable_i;
  input  [31:0]                       trigger_addr_i;
  input                               trigger_mismatch_enable_i;
  input  [4:0]                        trigger_mismatch_mask_i;
  input                               sample_valid_i;
  input  [PROBE_SAMPLE_BITS-1:0]      sample_i;
  input  [31:0]                       sample_abs_index_i;
  output                              arm_ready_o;
  output                              arm_accepted_o;
  output                              busy_o;
  output                              done_o;
  output                              done_pulse_o;
  output                              upload_allowed_o;
  output                              param_error_o;
  output                              pretrigger_ready_o;
  output                              meta_valid_o;
  output [PROTOCOL_COUNT-1:0]         meta_protocol_enable_o;
  output [31:0]                       meta_protocol_count_o;
  output [31:0]                       meta_sample_bits_o;
  output [31:0]                       meta_lane_count_o;
  output [31:0]                       meta_max_lane_count_o;
  output [31:0]                       meta_window_sample_count_o;
  output [31:0]                       meta_pretrigger_count_o;
  output [31:0]                       meta_trigger_count_o;
  output [31:0]                       meta_post_after_trigger_count_o;
  output [31:0]                       meta_window_start_abs_index_o;
  output [31:0]                       meta_trigger_abs_index_o;
  output [31:0]                       meta_trigger_sample_index_o;
  output [31:0]                       samples_seen_o;
  output [31:0]                       post_after_trigger_seen_o;
  output                              abort_pulse_o;
  output                              watchdog_pulse_o;
  output                              trigger_seen_o;
  output                              trigger_pulse_sample_o;
  output                              watchdog_expired_o;
  input                               read_req_i;
  input  [SAMPLE_ADDR_WIDTH-1:0]      read_sample_index_i;
  input  [LANE_INDEX_BITS-1:0]        read_lane_index_i;
  output                              read_ready_o;
  output                              read_valid_o;
  output                              read_error_o;
  output [LANE_BITS-1:0]              read_data_o;
  output [SAMPLE_ADDR_WIDTH-1:0]      read_sample_index_o;
  output [LANE_INDEX_BITS-1:0]        read_lane_index_o;
  output                              read_last_lane_o;
  output                              read_last_sample_o;
  output [3:0]                        state_o;

  localparam integer ACTIVE_LANE_COUNT = (PROBE_SAMPLE_BITS + LANE_BITS - 1) / LANE_BITS;
  localparam integer MAX_LANE_COUNT    = (MAX_PROBE_SAMPLE_BITS + LANE_BITS - 1) / LANE_BITS;
  localparam integer READ_BUS_BITS     = MAX_LANE_COUNT * LANE_BITS;

  localparam [3:0] ST_IDLE = 4'd0;
  localparam [3:0] ST_PRE  = 4'd1;
  localparam [3:0] ST_POST = 4'd2;
  localparam [3:0] ST_DONE = 4'd3;

  localparam [SAMPLE_ADDR_WIDTH-1:0] WINDOW_LAST_ADDR = (WINDOW_SAMPLE_COUNT - 1);

  reg [3:0]                       sample_state_r;
  reg [3:0]                       sample_state_next_r;
  reg [PROTOCOL_COUNT-1:0]        protocol_enable_sample_r;
  reg [31:0]                      trigger_addr_sample_r;
  reg                             trigger_mismatch_enable_sample_r;
  reg [4:0]                       trigger_mismatch_mask_sample_r;
  reg [4:0]                       mismatch_prev_sample_r;
  reg [SAMPLE_ADDR_WIDTH-1:0]     wr_addr_r;
  reg [SAMPLE_ADDR_WIDTH-1:0]     window_start_addr_sample_r;
  reg [31:0]                      sample_index_r;
  reg [31:0]                      post_count_sample_r;
  reg [31:0]                      sample_index_gray_sample_r;
  reg [31:0]                      post_count_gray_sample_r;
  reg [31:0]                      pre_count_sample_r;
  reg [31:0]                      post_target_count_sample_r;
  reg [31:0]                      window_start_abs_index_sample_r;
  reg [31:0]                      trigger_abs_index_meta_sample_r;
  reg [31:0]                      trigger_sample_index_sample_r;
  reg                             done_toggle_sample_r;
  reg                             arm_ready_sample_r;
  reg [31:0]                      trigger_timeout_samples_read_r;
  (* ASYNC_REG = "TRUE" *) reg [31:0] trigger_timeout_samples_sample_d1_r;
  (* ASYNC_REG = "TRUE" *) reg [31:0] trigger_timeout_samples_sample_d2_r;

  reg                             arm_toggle_read_r;
  reg                             arm_pending_read_r;
  reg                             arm_accepted_read_r;
  reg                             stop_toggle_read_r;
  reg [PROTOCOL_COUNT-1:0]        protocol_enable_read_r;
  reg [31:0]                      trigger_addr_read_r;
  reg                             trigger_mismatch_enable_read_r;
  reg [4:0]                       trigger_mismatch_mask_read_r;
  (* ASYNC_REG = "TRUE" *) reg    arm_ready_sample_d1_r;
  (* ASYNC_REG = "TRUE" *) reg    arm_ready_sample_d2_r;
  (* ASYNC_REG = "TRUE" *) reg    done_toggle_read_d1_r;
  (* ASYNC_REG = "TRUE" *) reg    done_toggle_read_d2_r;
  (* ASYNC_REG = "TRUE" *) reg    done_toggle_read_d3_r;
  reg                             meta_valid_o;
  reg                             trigger_seen_o;
  reg                             watchdog_expired_o;
  reg [PROTOCOL_COUNT-1:0]        meta_protocol_enable_o;
  reg [31:0]                      meta_pretrigger_count_o;
  reg [31:0]                      meta_post_after_trigger_count_o;
  reg [31:0]                      meta_window_start_abs_index_o;
  reg [31:0]                      meta_trigger_abs_index_o;
  reg [31:0]                      meta_trigger_sample_index_o;
  reg [31:0]                      samples_seen_o;
  reg [31:0]                      post_after_trigger_seen_o;
  reg [SAMPLE_ADDR_WIDTH-1:0]     window_start_addr_read_r;
  reg                             read_valid_o;
  reg                             read_error_o;
  reg [SAMPLE_ADDR_WIDTH-1:0]     read_sample_index_o;
  reg [LANE_INDEX_BITS-1:0]       read_lane_index_o;

  (* ASYNC_REG = "TRUE" *) reg    arm_toggle_sample_d1_r;
  (* ASYNC_REG = "TRUE" *) reg    arm_toggle_sample_d2_r;
  (* ASYNC_REG = "TRUE" *) reg    arm_toggle_sample_d3_r;
  (* ASYNC_REG = "TRUE" *) reg    stop_toggle_sample_d1_r;
  (* ASYNC_REG = "TRUE" *) reg    stop_toggle_sample_d2_r;
  (* ASYNC_REG = "TRUE" *) reg    stop_toggle_sample_d3_r;
  reg                             abort_toggle_sample_r;
  reg                             trigger_toggle_sample_r;
  reg                             watchdog_toggle_sample_r;
  (* ASYNC_REG = "TRUE" *) reg    abort_toggle_read_d1_r;
  (* ASYNC_REG = "TRUE" *) reg    abort_toggle_read_d2_r;
  (* ASYNC_REG = "TRUE" *) reg    abort_toggle_read_d3_r;
  (* ASYNC_REG = "TRUE" *) reg    trigger_toggle_read_d1_r;
  (* ASYNC_REG = "TRUE" *) reg    trigger_toggle_read_d2_r;
  (* ASYNC_REG = "TRUE" *) reg    trigger_toggle_read_d3_r;
  (* ASYNC_REG = "TRUE" *) reg    watchdog_toggle_read_d1_r;
  (* ASYNC_REG = "TRUE" *) reg    watchdog_toggle_read_d2_r;
  (* ASYNC_REG = "TRUE" *) reg    watchdog_toggle_read_d3_r;
  (* ASYNC_REG = "TRUE" *) reg [31:0] sample_index_gray_read_d1_r;
  (* ASYNC_REG = "TRUE" *) reg [31:0] sample_index_gray_read_d2_r;
  (* ASYNC_REG = "TRUE" *) reg [31:0] post_count_gray_read_d1_r;
  (* ASYNC_REG = "TRUE" *) reg [31:0] post_count_gray_read_d2_r;

  wire                            param_width_ok_w;
  wire                            param_lane_ok_w;
  wire                            param_window_ok_w;
  wire                            param_ok_w;
  wire                            arm_ready_sample_w;
  wire                            arm_request_sample_w;
  wire                            arm_accept_sample_w;
  wire                            trigger_accept_w;
  wire                            addr_valid_w;
  wire                            addr_ready_w;
  wire                            addr_match_w;
  wire                            addr_trigger_w;
  wire                            legacy_index_trigger_w;
  wire                            trace_trigger_w;
  wire [4:0]                      mismatch_mask_w;
  wire [4:0]                      mismatch_sample_w;
  wire [4:0]                      mismatch_rise_w;
  wire [4:0]                      mismatch_active_w;
  wire                            mismatch_trigger_w;
  wire                            post_sample_accept_w;
  wire                            capture_done_fire_w;
  wire                            capture_write_w;
  wire [MAX_PROBE_SAMPLE_BITS-1:0] sample_padded_w;
  wire [SAMPLE_ADDR_WIDTH-1:0]    trigger_window_start_addr_w;
  wire [SAMPLE_ADDR_WIDTH-1:0]    trigger_pre_count_addr_w;
  wire [31:0]                     trigger_pre_count_w;
  wire [31:0]                     trigger_post_target_count_w;
  wire [31:0]                     trigger_window_start_abs_index_w;
  wire                            arm_accept_read_w;
  wire                            stop_request_sample_w;
  wire                            watchdog_fire_w;
  wire                            abort_event_read_w;
  wire                            trigger_event_read_w;
  wire                            watchdog_event_read_w;
  wire                            done_event_read_w;
  wire [SAMPLE_ADDR_WIDTH-1:0]    read_phys_addr_w;
  wire                            read_lane_ok_w;
  wire                            read_accept_w;
  wire [LANE_INDEX_BITS-1:0]      last_lane_index_w;
  wire [READ_BUS_BITS-1:0]        read_lane_data_bus_w;

  genvar                          lane_g;

  function [31:0] gray_to_binary;
    input [31:0] gray_i;
    integer bit_index;
    begin
      gray_to_binary[31] = gray_i[31];
      for (bit_index = 30; bit_index >= 0; bit_index = bit_index - 1) begin
        gray_to_binary[bit_index] = gray_to_binary[bit_index + 1] ^ gray_i[bit_index];
      end
    end
  endfunction

  assign param_width_ok_w  = (PROBE_SAMPLE_BITS > 0) &&
                             (MAX_PROBE_SAMPLE_BITS > 0) &&
                             (PROBE_SAMPLE_BITS <= MAX_PROBE_SAMPLE_BITS);
  assign param_lane_ok_w   = (LANE_BITS == 128) &&
                             (ACTIVE_LANE_COUNT <= MAX_LANE_COUNT) &&
                             (MAX_LANE_COUNT > 0);
  assign param_window_ok_w = (WINDOW_SAMPLE_COUNT == 4096) &&
                             (PRETRIGGER_SAMPLE_COUNT == 2047) &&
                             (TRIGGER_SAMPLE_COUNT == 1) &&
                             (POSTTRIGGER_SAMPLE_COUNT == 2048) &&
                             ((PRETRIGGER_SAMPLE_COUNT + TRIGGER_SAMPLE_COUNT + POSTTRIGGER_SAMPLE_COUNT) ==
                              WINDOW_SAMPLE_COUNT);
  assign param_ok_w        = param_width_ok_w && param_lane_ok_w && param_window_ok_w;
  assign param_error_o     = !param_ok_w;
  assign trigger_pulse_sample_o = trigger_accept_w;

  assign sample_padded_w = sample_i;
  assign arm_ready_sample_w = ((sample_state_r == ST_IDLE) || (sample_state_r == ST_DONE)) && param_ok_w;
  assign arm_request_sample_w = arm_toggle_sample_d2_r ^ arm_toggle_sample_d3_r;
  assign arm_accept_sample_w = arm_request_sample_w && arm_ready_sample_w;
  assign stop_request_sample_w = stop_toggle_sample_d2_r ^ stop_toggle_sample_d3_r;
  assign watchdog_fire_w = (sample_state_r == ST_PRE) &&
                           (trigger_timeout_samples_sample_d2_r != 32'd0) &&
                           sample_valid_i &&
                           (sample_index_r >= (trigger_timeout_samples_sample_d2_r - 32'd1));
  assign addr_valid_w = sample_padded_w[418];
  assign addr_ready_w = sample_padded_w[429];
  assign addr_match_w = (trigger_addr_sample_r == 32'hffffffff) ||
                        (sample_padded_w[63:32] == trigger_addr_sample_r);
  // ILA-style address trigger: valid && ready && addr == configured trigger_addr.
  assign addr_trigger_w = sample_valid_i && addr_valid_w && addr_ready_w && addr_match_w;
  assign legacy_index_trigger_w = sample_valid_i && (sample_abs_index_i == trigger_addr_sample_r);
  assign mismatch_mask_w = (trigger_mismatch_mask_sample_r == 5'b00000) ?
                           5'b11111 : trigger_mismatch_mask_sample_r;
  assign mismatch_sample_w = sample_padded_w[506:502];
  assign mismatch_rise_w = mismatch_sample_w & ~mismatch_prev_sample_r;
  assign mismatch_active_w = mismatch_rise_w & mismatch_mask_w;
  assign mismatch_trigger_w = sample_valid_i && trigger_mismatch_enable_sample_r &&
                              (mismatch_active_w != 5'b00000);
  // Address match and mismatch rising edge are independent trigger sources.
  assign trace_trigger_w = addr_trigger_w || mismatch_trigger_w;
  assign trigger_accept_w = (sample_state_r == ST_PRE) &&
                            ((AHB_TRIGGER_MODE != 0) ? trace_trigger_w : legacy_index_trigger_w);
  assign post_sample_accept_w = (sample_state_r == ST_POST) && sample_valid_i;
  assign capture_done_fire_w = post_sample_accept_w && (post_count_sample_r == (post_target_count_sample_r - 32'd1));
  assign capture_write_w = ((sample_state_r == ST_PRE) || (sample_state_r == ST_POST)) && sample_valid_i;

  assign trigger_pre_count_w = (sample_index_r >= PRETRIGGER_SAMPLE_COUNT) ?
                               PRETRIGGER_SAMPLE_COUNT : sample_index_r;
  assign trigger_post_target_count_w = (WINDOW_SAMPLE_COUNT - 32'd1) - trigger_pre_count_w;
  assign trigger_window_start_abs_index_w = sample_index_r - trigger_pre_count_w;
  assign trigger_pre_count_addr_w = trigger_pre_count_w[SAMPLE_ADDR_WIDTH-1:0];
  assign trigger_window_start_addr_w = wr_addr_r - trigger_pre_count_addr_w;

  assign arm_accept_read_w = arm_pending_read_r && arm_ready_o;
  assign done_event_read_w = done_toggle_read_d2_r ^ done_toggle_read_d3_r;
  assign abort_event_read_w = abort_toggle_read_d2_r ^ abort_toggle_read_d3_r;
  assign trigger_event_read_w = trigger_toggle_read_d2_r ^ trigger_toggle_read_d3_r;
  assign watchdog_event_read_w = watchdog_toggle_read_d2_r ^ watchdog_toggle_read_d3_r;
  assign read_phys_addr_w = window_start_addr_read_r + read_sample_index_i;
  assign read_lane_ok_w = (read_lane_index_i < ACTIVE_LANE_COUNT);
  assign read_accept_w = read_req_i && read_ready_o && read_lane_ok_w;
  assign last_lane_index_w = (ACTIVE_LANE_COUNT - 1);

  assign arm_ready_o = arm_ready_sample_d2_r && param_ok_w;
  assign arm_accepted_o = arm_accepted_read_r;
  assign busy_o = !arm_ready_sample_d2_r && !meta_valid_o;
  assign done_o = meta_valid_o;
  assign done_pulse_o = done_event_read_w;
  assign upload_allowed_o = meta_valid_o;
  assign pretrigger_ready_o = 1'b0;
  assign meta_protocol_count_o = PROTOCOL_COUNT;
  assign meta_sample_bits_o = PROBE_SAMPLE_BITS;
  assign meta_lane_count_o = ACTIVE_LANE_COUNT;
  assign meta_max_lane_count_o = MAX_LANE_COUNT;
  assign meta_window_sample_count_o = WINDOW_SAMPLE_COUNT;
  assign meta_trigger_count_o = TRIGGER_SAMPLE_COUNT;
  assign read_ready_o = meta_valid_o && param_ok_w;
  assign read_data_o = read_lane_data_bus_w[(read_lane_index_o * LANE_BITS) +: LANE_BITS];
  assign read_last_lane_o = read_valid_o && (read_lane_index_o == last_lane_index_w);
  assign read_last_sample_o = read_valid_o && (read_sample_index_o == WINDOW_LAST_ADDR);
  assign state_o = meta_valid_o ? ST_DONE : (arm_ready_o ? ST_IDLE : ST_PRE);
  assign abort_pulse_o = abort_event_read_w;
  assign watchdog_pulse_o = watchdog_event_read_w;

  generate
    for (lane_g = 0; lane_g < MAX_LANE_COUNT; lane_g = lane_g + 1) begin : g_lane_ram
      if (lane_g < ACTIVE_LANE_COUNT) begin : g_active_lane
        reg [LANE_BITS-1:0] lane_mem_r [0:WINDOW_SAMPLE_COUNT-1];
        reg [LANE_BITS-1:0] lane_read_data_r;

        always @(posedge sample_clk) begin
          if (capture_write_w) begin
            lane_mem_r[wr_addr_r] <= #UDLY sample_padded_w[(lane_g * LANE_BITS) +: LANE_BITS];
          end
        end

        always @(posedge read_clk or negedge read_rst_n) begin
          if (!read_rst_n) begin
            lane_read_data_r <= #UDLY {LANE_BITS{1'b0}};
          end else if (read_accept_w) begin
            lane_read_data_r <= #UDLY lane_mem_r[read_phys_addr_w];
          end
        end

        assign read_lane_data_bus_w[(lane_g * LANE_BITS) +: LANE_BITS] = lane_read_data_r;
      end else begin : g_inactive_lane
        assign read_lane_data_bus_w[(lane_g * LANE_BITS) +: LANE_BITS] = {LANE_BITS{1'b0}};
      end
    end
  endgenerate

  always @(*) begin
    sample_state_next_r = sample_state_r;
    case (sample_state_r)
      ST_IDLE: begin
        if (arm_accept_sample_w) begin
          sample_state_next_r = ST_PRE;
        end
      end
      ST_PRE: begin
        if (stop_request_sample_w || watchdog_fire_w) begin
          sample_state_next_r = ST_IDLE;
        end else if (trigger_accept_w) begin
          sample_state_next_r = ST_POST;
        end
      end
      ST_POST: begin
        if (stop_request_sample_w) begin
          sample_state_next_r = ST_IDLE;
        end else if (capture_done_fire_w) begin
          sample_state_next_r = ST_DONE;
        end
      end
      ST_DONE: begin
        if (arm_accept_sample_w) begin
          sample_state_next_r = ST_PRE;
        end
      end
      default: begin
        sample_state_next_r = ST_IDLE;
      end
    endcase
  end

  always @(posedge sample_clk or negedge sample_rst_n) begin
    if (!sample_rst_n) begin
      arm_toggle_sample_d1_r <= #UDLY 1'b0;
      arm_toggle_sample_d2_r <= #UDLY 1'b0;
      arm_toggle_sample_d3_r <= #UDLY 1'b0;
      stop_toggle_sample_d1_r <= #UDLY 1'b0;
      stop_toggle_sample_d2_r <= #UDLY 1'b0;
      stop_toggle_sample_d3_r <= #UDLY 1'b0;
      trigger_timeout_samples_sample_d1_r <= #UDLY 32'd0;
      trigger_timeout_samples_sample_d2_r <= #UDLY 32'd0;
    end else begin
      arm_toggle_sample_d1_r <= #UDLY arm_toggle_read_r;
      arm_toggle_sample_d2_r <= #UDLY arm_toggle_sample_d1_r;
      arm_toggle_sample_d3_r <= #UDLY arm_toggle_sample_d2_r;
      stop_toggle_sample_d1_r <= #UDLY stop_toggle_read_r;
      stop_toggle_sample_d2_r <= #UDLY stop_toggle_sample_d1_r;
      stop_toggle_sample_d3_r <= #UDLY stop_toggle_sample_d2_r;
      trigger_timeout_samples_sample_d1_r <= #UDLY trigger_timeout_samples_read_r;
      trigger_timeout_samples_sample_d2_r <= #UDLY trigger_timeout_samples_sample_d1_r;
    end
  end

  // 状态计数先在源时钟域寄存为 Gray 码，再跨域同步，避免组合逻辑进入同步器。
  always @(posedge sample_clk or negedge sample_rst_n) begin
    if (!sample_rst_n) begin
      sample_index_gray_sample_r <= #UDLY 32'd0;
      post_count_gray_sample_r <= #UDLY 32'd0;
    end else begin
      sample_index_gray_sample_r <= #UDLY sample_index_r ^ (sample_index_r >> 1);
      post_count_gray_sample_r <= #UDLY post_count_sample_r ^ (post_count_sample_r >> 1);
    end
  end

  always @(posedge sample_clk or negedge sample_rst_n) begin
    if (!sample_rst_n) begin
      sample_state_r <= #UDLY ST_IDLE;
      arm_ready_sample_r <= #UDLY 1'b0;
    end else begin
      sample_state_r <= #UDLY sample_state_next_r;
      arm_ready_sample_r <= #UDLY arm_ready_sample_w || stop_request_sample_w || watchdog_fire_w;
    end
  end

  always @(posedge sample_clk or negedge sample_rst_n) begin
    if (!sample_rst_n) begin
      protocol_enable_sample_r <= #UDLY {PROTOCOL_COUNT{1'b0}};
      trigger_addr_sample_r <= #UDLY 32'd0;
      trigger_mismatch_enable_sample_r <= #UDLY 1'b0;
      trigger_mismatch_mask_sample_r <= #UDLY 5'b00000;
    end else if (arm_accept_sample_w) begin
      protocol_enable_sample_r <= #UDLY protocol_enable_read_r;
      trigger_addr_sample_r <= #UDLY trigger_addr_read_r;
      trigger_mismatch_enable_sample_r <= #UDLY trigger_mismatch_enable_read_r;
      trigger_mismatch_mask_sample_r <= #UDLY trigger_mismatch_mask_read_r;
    end
  end

  always @(posedge sample_clk or negedge sample_rst_n) begin
    if (!sample_rst_n) begin
      mismatch_prev_sample_r <= #UDLY 5'b00000;
    end else if (arm_accept_sample_w) begin
      mismatch_prev_sample_r <= #UDLY 5'b00000;
    end else if (sample_valid_i) begin
      mismatch_prev_sample_r <= #UDLY mismatch_sample_w;
    end
  end

  always @(posedge sample_clk or negedge sample_rst_n) begin
    if (!sample_rst_n) begin
      wr_addr_r <= #UDLY {SAMPLE_ADDR_WIDTH{1'b0}};
      sample_index_r <= #UDLY 32'd0;
    end else if (arm_accept_sample_w) begin
      wr_addr_r <= #UDLY {SAMPLE_ADDR_WIDTH{1'b0}};
      sample_index_r <= #UDLY 32'd0;
    end else if (capture_write_w) begin
      wr_addr_r <= #UDLY wr_addr_r + {{(SAMPLE_ADDR_WIDTH-1){1'b0}}, 1'b1};
      sample_index_r <= #UDLY sample_index_r + 32'd1;
    end
  end

  always @(posedge sample_clk or negedge sample_rst_n) begin
    if (!sample_rst_n) begin
      window_start_addr_sample_r <= #UDLY {SAMPLE_ADDR_WIDTH{1'b0}};
      window_start_abs_index_sample_r <= #UDLY 32'd0;
      trigger_abs_index_meta_sample_r <= #UDLY 32'd0;
      trigger_sample_index_sample_r <= #UDLY 32'd0;
      pre_count_sample_r <= #UDLY 32'd0;
      post_target_count_sample_r <= #UDLY POSTTRIGGER_SAMPLE_COUNT;
      post_count_sample_r <= #UDLY 32'd0;
      done_toggle_sample_r <= #UDLY 1'b0;
      abort_toggle_sample_r <= #UDLY 1'b0;
      trigger_toggle_sample_r <= #UDLY 1'b0;
      watchdog_toggle_sample_r <= #UDLY 1'b0;
    end else if (arm_accept_sample_w) begin
      window_start_addr_sample_r <= #UDLY {SAMPLE_ADDR_WIDTH{1'b0}};
      window_start_abs_index_sample_r <= #UDLY 32'd0;
      trigger_abs_index_meta_sample_r <= #UDLY 32'd0;
      trigger_sample_index_sample_r <= #UDLY 32'd0;
      pre_count_sample_r <= #UDLY 32'd0;
      post_target_count_sample_r <= #UDLY POSTTRIGGER_SAMPLE_COUNT;
      post_count_sample_r <= #UDLY 32'd0;
    end else if (stop_request_sample_w || watchdog_fire_w) begin
      abort_toggle_sample_r <= #UDLY !abort_toggle_sample_r;
      if (watchdog_fire_w) begin
        watchdog_toggle_sample_r <= #UDLY !watchdog_toggle_sample_r;
      end
    end else begin
      if (trigger_accept_w) begin
        window_start_addr_sample_r <= #UDLY trigger_window_start_addr_w;
        window_start_abs_index_sample_r <= #UDLY trigger_window_start_abs_index_w;
        trigger_abs_index_meta_sample_r <= #UDLY sample_abs_index_i;
        trigger_sample_index_sample_r <= #UDLY trigger_pre_count_w;
        pre_count_sample_r <= #UDLY trigger_pre_count_w;
        post_target_count_sample_r <= #UDLY trigger_post_target_count_w;
        post_count_sample_r <= #UDLY 32'd0;
        trigger_toggle_sample_r <= #UDLY !trigger_toggle_sample_r;
      end else if (post_sample_accept_w && !capture_done_fire_w) begin
        post_count_sample_r <= #UDLY post_count_sample_r + 32'd1;
      end else if (capture_done_fire_w) begin
        post_count_sample_r <= #UDLY post_target_count_sample_r;
        done_toggle_sample_r <= #UDLY !done_toggle_sample_r;
      end
    end
  end

  always @(posedge read_clk or negedge read_rst_n) begin
    if (!read_rst_n) begin
      arm_toggle_read_r <= #UDLY 1'b0;
      arm_pending_read_r <= #UDLY 1'b0;
      arm_accepted_read_r <= #UDLY 1'b0;
      stop_toggle_read_r <= #UDLY 1'b0;
      protocol_enable_read_r <= #UDLY {PROTOCOL_COUNT{1'b0}};
      trigger_addr_read_r <= #UDLY 32'd0;
      trigger_mismatch_enable_read_r <= #UDLY 1'b0;
      trigger_mismatch_mask_read_r <= #UDLY 5'b00000;
      trigger_timeout_samples_read_r <= #UDLY 32'd0;
    end else begin
      arm_accepted_read_r <= #UDLY 1'b0;
      if (stop_i) begin
        stop_toggle_read_r <= #UDLY !stop_toggle_read_r;
        arm_pending_read_r <= #UDLY 1'b0;
      end else if (arm_i) begin
        arm_pending_read_r <= #UDLY 1'b1;
        protocol_enable_read_r <= #UDLY protocol_enable_i;
        trigger_addr_read_r <= #UDLY trigger_addr_i;
        trigger_mismatch_enable_read_r <= #UDLY trigger_mismatch_enable_i;
        trigger_mismatch_mask_read_r <= #UDLY trigger_mismatch_mask_i;
        trigger_timeout_samples_read_r <= #UDLY trigger_timeout_samples_i;
      end
      if (arm_accept_read_w) begin
        arm_toggle_read_r <= #UDLY !arm_toggle_read_r;
        arm_pending_read_r <= #UDLY 1'b0;
        arm_accepted_read_r <= #UDLY 1'b1;
      end
    end
  end

  always @(posedge read_clk or negedge read_rst_n) begin
    if (!read_rst_n) begin
      arm_ready_sample_d1_r <= #UDLY 1'b0;
      arm_ready_sample_d2_r <= #UDLY 1'b0;
      done_toggle_read_d1_r <= #UDLY 1'b0;
      done_toggle_read_d2_r <= #UDLY 1'b0;
      done_toggle_read_d3_r <= #UDLY 1'b0;
      abort_toggle_read_d1_r <= #UDLY 1'b0;
      abort_toggle_read_d2_r <= #UDLY 1'b0;
      abort_toggle_read_d3_r <= #UDLY 1'b0;
      trigger_toggle_read_d1_r <= #UDLY 1'b0;
      trigger_toggle_read_d2_r <= #UDLY 1'b0;
      trigger_toggle_read_d3_r <= #UDLY 1'b0;
      watchdog_toggle_read_d1_r <= #UDLY 1'b0;
      watchdog_toggle_read_d2_r <= #UDLY 1'b0;
      watchdog_toggle_read_d3_r <= #UDLY 1'b0;
      sample_index_gray_read_d1_r <= #UDLY 32'd0;
      sample_index_gray_read_d2_r <= #UDLY 32'd0;
      post_count_gray_read_d1_r <= #UDLY 32'd0;
      post_count_gray_read_d2_r <= #UDLY 32'd0;
    end else begin
      arm_ready_sample_d1_r <= #UDLY arm_ready_sample_r;
      arm_ready_sample_d2_r <= #UDLY arm_ready_sample_d1_r;
      done_toggle_read_d1_r <= #UDLY done_toggle_sample_r;
      done_toggle_read_d2_r <= #UDLY done_toggle_read_d1_r;
      done_toggle_read_d3_r <= #UDLY done_toggle_read_d2_r;
      abort_toggle_read_d1_r <= #UDLY abort_toggle_sample_r;
      abort_toggle_read_d2_r <= #UDLY abort_toggle_read_d1_r;
      abort_toggle_read_d3_r <= #UDLY abort_toggle_read_d2_r;
      trigger_toggle_read_d1_r <= #UDLY trigger_toggle_sample_r;
      trigger_toggle_read_d2_r <= #UDLY trigger_toggle_read_d1_r;
      trigger_toggle_read_d3_r <= #UDLY trigger_toggle_read_d2_r;
      watchdog_toggle_read_d1_r <= #UDLY watchdog_toggle_sample_r;
      watchdog_toggle_read_d2_r <= #UDLY watchdog_toggle_read_d1_r;
      watchdog_toggle_read_d3_r <= #UDLY watchdog_toggle_read_d2_r;
      sample_index_gray_read_d1_r <= #UDLY sample_index_gray_sample_r;
      sample_index_gray_read_d2_r <= #UDLY sample_index_gray_read_d1_r;
      post_count_gray_read_d1_r <= #UDLY post_count_gray_sample_r;
      post_count_gray_read_d2_r <= #UDLY post_count_gray_read_d1_r;
    end
  end

  always @(posedge read_clk or negedge read_rst_n) begin
    if (!read_rst_n) begin
      meta_valid_o <= #UDLY 1'b0;
      trigger_seen_o <= #UDLY 1'b0;
      watchdog_expired_o <= #UDLY 1'b0;
      meta_protocol_enable_o <= #UDLY {PROTOCOL_COUNT{1'b0}};
      meta_pretrigger_count_o <= #UDLY 32'd0;
      meta_post_after_trigger_count_o <= #UDLY 32'd0;
      meta_window_start_abs_index_o <= #UDLY 32'd0;
      meta_trigger_abs_index_o <= #UDLY 32'd0;
      meta_trigger_sample_index_o <= #UDLY 32'd0;
      samples_seen_o <= #UDLY 32'd0;
      post_after_trigger_seen_o <= #UDLY 32'd0;
      window_start_addr_read_r <= #UDLY {SAMPLE_ADDR_WIDTH{1'b0}};
    end else if (arm_accept_read_w) begin
      meta_valid_o <= #UDLY 1'b0;
      trigger_seen_o <= #UDLY 1'b0;
      watchdog_expired_o <= #UDLY 1'b0;
      samples_seen_o <= #UDLY 32'd0;
      post_after_trigger_seen_o <= #UDLY 32'd0;
    end else if (done_event_read_w) begin
      meta_valid_o <= #UDLY 1'b1;
      meta_protocol_enable_o <= #UDLY protocol_enable_sample_r;
      meta_pretrigger_count_o <= #UDLY pre_count_sample_r;
      meta_post_after_trigger_count_o <= #UDLY post_target_count_sample_r;
      meta_window_start_abs_index_o <= #UDLY window_start_abs_index_sample_r;
      meta_trigger_abs_index_o <= #UDLY trigger_abs_index_meta_sample_r;
      meta_trigger_sample_index_o <= #UDLY trigger_sample_index_sample_r;
      samples_seen_o <= #UDLY gray_to_binary(sample_index_gray_read_d2_r);
      post_after_trigger_seen_o <= #UDLY gray_to_binary(post_count_gray_read_d2_r);
      window_start_addr_read_r <= #UDLY window_start_addr_sample_r;
    end else begin
      samples_seen_o <= #UDLY gray_to_binary(sample_index_gray_read_d2_r);
      post_after_trigger_seen_o <= #UDLY gray_to_binary(post_count_gray_read_d2_r);
      if (trigger_event_read_w) begin
        trigger_seen_o <= #UDLY 1'b1;
      end
      if (watchdog_event_read_w) begin
        watchdog_expired_o <= #UDLY 1'b1;
      end
      if (abort_event_read_w) begin
        meta_valid_o <= #UDLY 1'b0;
      end
    end
  end

  always @(posedge read_clk or negedge read_rst_n) begin
    if (!read_rst_n) begin
      read_valid_o <= #UDLY 1'b0;
      read_error_o <= #UDLY 1'b0;
      read_sample_index_o <= #UDLY {SAMPLE_ADDR_WIDTH{1'b0}};
      read_lane_index_o <= #UDLY {LANE_INDEX_BITS{1'b0}};
    end else if (arm_accept_read_w) begin
      read_valid_o <= #UDLY 1'b0;
      read_error_o <= #UDLY 1'b0;
      read_sample_index_o <= #UDLY {SAMPLE_ADDR_WIDTH{1'b0}};
      read_lane_index_o <= #UDLY {LANE_INDEX_BITS{1'b0}};
    end else begin
      read_valid_o <= #UDLY read_accept_w;
      read_error_o <= #UDLY read_req_i && !read_accept_w;
      if (read_accept_w) begin
        read_sample_index_o <= #UDLY read_sample_index_i;
        read_lane_index_o <= #UDLY read_lane_index_i;
      end
    end
  end

endmodule
