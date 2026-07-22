/**********************************************************
* 文件名: lockstep_sparse_change_window_cdc.v
* 日期: 2026-07-22
* 版本: 1.0
* 更新记录: 初版，实现 16-bit 低速探针变化环与冻结后跨时钟域读取。
* 描述: 在 sample 域记录统一触发窗口内的低速状态变化，在 read 域发布快照并顺序读取。
**********************************************************/

`timescale 1ns/1ps

module lockstep_sparse_change_window_cdc (
  sample_clk,
  sample_rst_n,
  arm_pulse_i,
  stop_pulse_i,
  trigger_pulse_i,
  capture_done_pulse_i,
  sample_valid_i,
  sample_abs_index_i,
  sample_timestamp_i,
  state_i,
  read_clk,
  read_rst_n,
  meta_valid_o,
  ring_record_count_o,
  observed_count_o,
  window_initial_state_o,
  final_state_o,
  window_origin_tick_o,
  trigger_tick_o,
  window_end_exclusive_tick_o,
  read_req_i,
  read_index_i,
  read_ready_o,
  read_valid_o,
  read_record_o
);
  parameter UDLY = 1;
  parameter RING_DEPTH = 4096;
  parameter ADDR_WIDTH = 12;

  input                   sample_clk;
  input                   sample_rst_n;
  input                   arm_pulse_i;
  input                   stop_pulse_i;
  input                   trigger_pulse_i;
  input                   capture_done_pulse_i;
  input                   sample_valid_i;
  input  [31:0]           sample_abs_index_i;
  input  [63:0]           sample_timestamp_i;
  input  [15:0]           state_i;
  input                   read_clk;
  input                   read_rst_n;
  output                  meta_valid_o;
  output [12:0]           ring_record_count_o;
  output [31:0]           observed_count_o;
  output [15:0]           window_initial_state_o;
  output [15:0]           final_state_o;
  output [63:0]           window_origin_tick_o;
  output [63:0]           trigger_tick_o;
  output [63:0]           window_end_exclusive_tick_o;
  input                   read_req_i;
  input  [11:0]           read_index_i;
  output                  read_ready_o;
  output                  read_valid_o;
  output [127:0]          read_record_o;

  localparam [1:0] SCAN_IDLE  = 2'd0;
  localparam [1:0] SCAN_ISSUE = 2'd1;
  localparam [1:0] SCAN_CHECK = 2'd2;

  (* ram_style = "block" *) reg [127:0] ring_mem_r [0:RING_DEPTH-1];

  reg                       capturing_sample_r;
  reg                       trigger_seen_sample_r;
  reg [15:0]                previous_state_sample_r;
  reg [ADDR_WIDTH-1:0]      write_addr_sample_r;
  reg [ADDR_WIDTH:0]        ring_count_sample_r;
  reg [31:0]                observed_count_sample_r;
  reg [31:0]                window_start_index_sample_r;
  reg [63:0]                window_origin_tick_sample_r;
  reg [63:0]                trigger_tick_sample_r;
  reg [63:0]                window_end_tick_sample_r;
  reg [ADDR_WIDTH-1:0]      snapshot_oldest_addr_sample_r;
  reg [ADDR_WIDTH:0]        snapshot_count_sample_r;
  reg [31:0]                snapshot_observed_count_sample_r;
  reg [15:0]                snapshot_final_state_sample_r;
  reg                       snapshot_toggle_sample_r;
  reg                       invalidate_toggle_sample_r;

  (* ASYNC_REG = "TRUE" *) reg snapshot_toggle_read_d1_r;
  (* ASYNC_REG = "TRUE" *) reg snapshot_toggle_read_d2_r;
  (* ASYNC_REG = "TRUE" *) reg snapshot_toggle_read_d3_r;
  (* ASYNC_REG = "TRUE" *) reg invalidate_toggle_read_d1_r;
  (* ASYNC_REG = "TRUE" *) reg invalidate_toggle_read_d2_r;
  (* ASYNC_REG = "TRUE" *) reg invalidate_toggle_read_d3_r;

  reg                       meta_valid_o;
  reg [12:0]                ring_record_count_o;
  reg [31:0]                observed_count_o;
  reg [15:0]                window_initial_state_o;
  reg [15:0]                final_state_o;
  reg [63:0]                window_origin_tick_o;
  reg [63:0]                trigger_tick_o;
  reg [63:0]                window_end_exclusive_tick_o;
  reg                       read_valid_o;
  reg [127:0]               read_record_o;
  reg [1:0]                 scan_state_read_r;
  reg [ADDR_WIDTH-1:0]      scan_addr_read_r;
  reg [ADDR_WIDTH:0]        scan_remaining_read_r;
  reg [127:0]               scan_record_read_r;
  reg [ADDR_WIDTH-1:0]      snapshot_first_addr_read_r;
  reg [31:0]                window_start_index_read_r;

  wire [15:0]               change_mask_w;
  wire                      change_fire_w;
  wire [7:0]                source_mask_w;
  wire [127:0]              write_record_w;
  wire [ADDR_WIDTH:0]       count_after_write_w;
  wire [ADDR_WIDTH-1:0]     oldest_after_write_w;
  wire [31:0]               observed_after_write_w;
  wire                      snapshot_event_read_w;
  wire                      invalidate_event_read_w;
  wire [31:0]               scan_window_offset_w;
  wire                      scan_record_in_window_w;
  wire [ADDR_WIDTH-1:0]     user_read_addr_w;

  assign change_mask_w = state_i ^ previous_state_sample_r;
  assign change_fire_w = capturing_sample_r && sample_valid_i &&
                         (change_mask_w != 16'd0);
  assign source_mask_w = {change_mask_w[15:12] != 4'd0,
                          2'b00,
                          change_mask_w[11:10] != 2'd0,
                          change_mask_w[9:8] != 2'd0,
                          change_mask_w[7:4] != 4'd0,
                          change_mask_w[3:0] != 4'd0,
                          1'b0};
  assign write_record_w = {16'd0, change_mask_w,
                           4'd0, source_mask_w, 4'd0, state_i,
                           observed_count_sample_r, sample_abs_index_i};

  assign count_after_write_w = change_fire_w && (ring_count_sample_r < RING_DEPTH) ?
                               ring_count_sample_r + 13'd1 : ring_count_sample_r;
  assign oldest_after_write_w = (ring_count_sample_r == RING_DEPTH) ?
                                (change_fire_w ? write_addr_sample_r + 12'd1 : write_addr_sample_r) :
                                {ADDR_WIDTH{1'b0}};
  assign observed_after_write_w = observed_count_sample_r +
                                  (change_fire_w ? 32'd1 : 32'd0);

  assign snapshot_event_read_w = snapshot_toggle_read_d2_r ^ snapshot_toggle_read_d3_r;
  assign invalidate_event_read_w = invalidate_toggle_read_d2_r ^ invalidate_toggle_read_d3_r;
  assign scan_window_offset_w = scan_record_read_r[31:0] - window_start_index_read_r;
  assign scan_record_in_window_w = (scan_window_offset_w < 32'd4096);
  assign user_read_addr_w = snapshot_first_addr_read_r + read_index_i;
  assign read_ready_o = meta_valid_o &&
                        ({1'b0, read_index_i} < ring_record_count_o);

  // 采样域写端每个有效 sample 最多合并写入一条变化记录。
  always @(posedge sample_clk) begin
    if (change_fire_w) begin
      ring_mem_r[write_addr_sample_r] <= #UDLY write_record_w;
    end
  end

  // 采样域维护采集生命周期、环形指针以及冻结后保持稳定的快照。
  always @(posedge sample_clk or negedge sample_rst_n) begin
    if (!sample_rst_n) begin
      capturing_sample_r <= #UDLY 1'b0;
      trigger_seen_sample_r <= #UDLY 1'b0;
      previous_state_sample_r <= #UDLY 16'd0;
      write_addr_sample_r <= #UDLY {ADDR_WIDTH{1'b0}};
      ring_count_sample_r <= #UDLY {(ADDR_WIDTH+1){1'b0}};
      observed_count_sample_r <= #UDLY 32'd0;
      window_start_index_sample_r <= #UDLY 32'd0;
      window_origin_tick_sample_r <= #UDLY 64'd0;
      trigger_tick_sample_r <= #UDLY 64'd0;
      window_end_tick_sample_r <= #UDLY 64'd0;
      snapshot_oldest_addr_sample_r <= #UDLY {ADDR_WIDTH{1'b0}};
      snapshot_count_sample_r <= #UDLY {(ADDR_WIDTH+1){1'b0}};
      snapshot_observed_count_sample_r <= #UDLY 32'd0;
      snapshot_final_state_sample_r <= #UDLY 16'd0;
      snapshot_toggle_sample_r <= #UDLY 1'b0;
      invalidate_toggle_sample_r <= #UDLY 1'b0;
    end else if (stop_pulse_i) begin
      capturing_sample_r <= #UDLY 1'b0;
      trigger_seen_sample_r <= #UDLY 1'b0;
      invalidate_toggle_sample_r <= #UDLY !invalidate_toggle_sample_r;
    end else if (arm_pulse_i) begin
      capturing_sample_r <= #UDLY 1'b1;
      trigger_seen_sample_r <= #UDLY 1'b0;
      previous_state_sample_r <= #UDLY state_i;
      write_addr_sample_r <= #UDLY {ADDR_WIDTH{1'b0}};
      ring_count_sample_r <= #UDLY {(ADDR_WIDTH+1){1'b0}};
      observed_count_sample_r <= #UDLY 32'd0;
      window_start_index_sample_r <= #UDLY 32'd0;
      window_origin_tick_sample_r <= #UDLY 64'd0;
      trigger_tick_sample_r <= #UDLY 64'd0;
      window_end_tick_sample_r <= #UDLY 64'd0;
      invalidate_toggle_sample_r <= #UDLY !invalidate_toggle_sample_r;
    end else if (capturing_sample_r) begin
      if (sample_valid_i) begin
        previous_state_sample_r <= #UDLY state_i;
      end
      if (change_fire_w) begin
        write_addr_sample_r <= #UDLY write_addr_sample_r + 12'd1;
        ring_count_sample_r <= #UDLY count_after_write_w;
        observed_count_sample_r <= #UDLY observed_after_write_w;
      end
      if (trigger_pulse_i && sample_valid_i && !trigger_seen_sample_r) begin
        trigger_seen_sample_r <= #UDLY 1'b1;
        window_start_index_sample_r <= #UDLY sample_abs_index_i - 32'd2047;
        window_origin_tick_sample_r <= #UDLY sample_timestamp_i - 64'd2047;
        trigger_tick_sample_r <= #UDLY sample_timestamp_i;
        window_end_tick_sample_r <= #UDLY sample_timestamp_i + 64'd2049;
      end
      if (capture_done_pulse_i && trigger_seen_sample_r) begin
        capturing_sample_r <= #UDLY 1'b0;
        snapshot_oldest_addr_sample_r <= #UDLY oldest_after_write_w;
        snapshot_count_sample_r <= #UDLY count_after_write_w;
        snapshot_observed_count_sample_r <= #UDLY observed_after_write_w;
        snapshot_final_state_sample_r <= #UDLY sample_valid_i ? state_i : previous_state_sample_r;
        snapshot_toggle_sample_r <= #UDLY !snapshot_toggle_sample_r;
      end
    end
  end

  // 快照和失效 toggle 进入读时钟域；对应多位快照在 toggle 发出后保持不变。
  always @(posedge read_clk or negedge read_rst_n) begin
    if (!read_rst_n) begin
      snapshot_toggle_read_d1_r <= #UDLY 1'b0;
      snapshot_toggle_read_d2_r <= #UDLY 1'b0;
      snapshot_toggle_read_d3_r <= #UDLY 1'b0;
      invalidate_toggle_read_d1_r <= #UDLY 1'b0;
      invalidate_toggle_read_d2_r <= #UDLY 1'b0;
      invalidate_toggle_read_d3_r <= #UDLY 1'b0;
    end else begin
      snapshot_toggle_read_d1_r <= #UDLY snapshot_toggle_sample_r;
      snapshot_toggle_read_d2_r <= #UDLY snapshot_toggle_read_d1_r;
      snapshot_toggle_read_d3_r <= #UDLY snapshot_toggle_read_d2_r;
      invalidate_toggle_read_d1_r <= #UDLY invalidate_toggle_sample_r;
      invalidate_toggle_read_d2_r <= #UDLY invalidate_toggle_read_d1_r;
      invalidate_toggle_read_d3_r <= #UDLY invalidate_toggle_read_d2_r;
    end
  end

  // 冻结后扫描并剔除窗口起点之前的旧历史，再按逻辑索引读取双口 RAM。
  always @(posedge read_clk or negedge read_rst_n) begin
    if (!read_rst_n) begin
      meta_valid_o <= #UDLY 1'b0;
      ring_record_count_o <= #UDLY 13'd0;
      observed_count_o <= #UDLY 32'd0;
      window_initial_state_o <= #UDLY 16'd0;
      final_state_o <= #UDLY 16'd0;
      window_origin_tick_o <= #UDLY 64'd0;
      trigger_tick_o <= #UDLY 64'd0;
      window_end_exclusive_tick_o <= #UDLY 64'd0;
      read_valid_o <= #UDLY 1'b0;
      read_record_o <= #UDLY 128'd0;
      scan_state_read_r <= #UDLY SCAN_IDLE;
      scan_addr_read_r <= #UDLY {ADDR_WIDTH{1'b0}};
      scan_remaining_read_r <= #UDLY {(ADDR_WIDTH+1){1'b0}};
      scan_record_read_r <= #UDLY 128'd0;
      snapshot_first_addr_read_r <= #UDLY {ADDR_WIDTH{1'b0}};
      window_start_index_read_r <= #UDLY 32'd0;
    end else begin
      read_valid_o <= #UDLY 1'b0;
      if (invalidate_event_read_w) begin
        meta_valid_o <= #UDLY 1'b0;
        ring_record_count_o <= #UDLY 13'd0;
        scan_state_read_r <= #UDLY SCAN_IDLE;
      end else if (snapshot_event_read_w) begin
        meta_valid_o <= #UDLY 1'b0;
        observed_count_o <= #UDLY snapshot_observed_count_sample_r;
        final_state_o <= #UDLY snapshot_final_state_sample_r;
        window_origin_tick_o <= #UDLY window_origin_tick_sample_r;
        trigger_tick_o <= #UDLY trigger_tick_sample_r;
        window_end_exclusive_tick_o <= #UDLY window_end_tick_sample_r;
        window_start_index_read_r <= #UDLY window_start_index_sample_r;
        snapshot_first_addr_read_r <= #UDLY snapshot_oldest_addr_sample_r;
        if (snapshot_count_sample_r == 13'd0) begin
          ring_record_count_o <= #UDLY 13'd0;
          window_initial_state_o <= #UDLY snapshot_final_state_sample_r;
          meta_valid_o <= #UDLY 1'b1;
          scan_state_read_r <= #UDLY SCAN_IDLE;
        end else begin
          scan_addr_read_r <= #UDLY snapshot_oldest_addr_sample_r;
          scan_remaining_read_r <= #UDLY snapshot_count_sample_r;
          scan_state_read_r <= #UDLY SCAN_ISSUE;
        end
      end else begin
        case (scan_state_read_r)
          SCAN_IDLE: begin
            if (read_req_i && read_ready_o) begin
              read_record_o <= #UDLY ring_mem_r[user_read_addr_w];
              read_valid_o <= #UDLY 1'b1;
            end
          end
          SCAN_ISSUE: begin
            scan_record_read_r <= #UDLY ring_mem_r[scan_addr_read_r];
            scan_state_read_r <= #UDLY SCAN_CHECK;
          end
          SCAN_CHECK: begin
            if (scan_record_in_window_w) begin
              snapshot_first_addr_read_r <= #UDLY scan_addr_read_r;
              ring_record_count_o <= #UDLY scan_remaining_read_r;
              window_initial_state_o <= #UDLY scan_record_read_r[79:64] ^
                                                 scan_record_read_r[111:96];
              meta_valid_o <= #UDLY 1'b1;
              scan_state_read_r <= #UDLY SCAN_IDLE;
            end else if (scan_remaining_read_r == 13'd1) begin
              ring_record_count_o <= #UDLY 13'd0;
              window_initial_state_o <= #UDLY snapshot_final_state_sample_r;
              meta_valid_o <= #UDLY 1'b1;
              scan_state_read_r <= #UDLY SCAN_IDLE;
            end else begin
              scan_addr_read_r <= #UDLY scan_addr_read_r + 12'd1;
              scan_remaining_read_r <= #UDLY scan_remaining_read_r - 13'd1;
              scan_state_read_r <= #UDLY SCAN_ISSUE;
            end
          end
          default: begin
            meta_valid_o <= #UDLY 1'b0;
            scan_state_read_r <= #UDLY SCAN_IDLE;
          end
        endcase
      end
    end
  end

endmodule
