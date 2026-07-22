/**********************************************************
* 文件名: lockstep_event_frame_source.v
* 日期: 2026-07-19
* 版本: 1.2
* 更新记录: 保留主结束原因，overflow 和 dropped 仅通过独立字段上报。
* 描述: 发送 EVENT_META、逐条 EVENT_DATA 和 EVENT_END，并保持事件统计。
**********************************************************/

`timescale 1ns/1ps

module lockstep_event_frame_source (
  clk,
  rst_n,
  start_i,
  capture_id_i,
  sample_rate_hz_i,
  implemented_source_mask_i,
  enabled_source_mask_i,
  design_gap_mask_i,
  watchdog_ticks_i,
  hard_timeout_ticks_i,
  collection_done_i,
  collection_end_reason_i,
  overflow_mask_i,
  dropped_count_i,
  event_valid_i,
  event_ready_o,
  event_record_i,
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
  done_o,
  state_o
);
  parameter UDLY = 1;

  input         clk;
  input         rst_n;
  input         start_i;
  input  [31:0] capture_id_i;
  input  [31:0] sample_rate_hz_i;
  input  [8:0]  implemented_source_mask_i;
  input  [8:0]  enabled_source_mask_i;
  input  [8:0]  design_gap_mask_i;
  input  [31:0] watchdog_ticks_i;
  input  [31:0] hard_timeout_ticks_i;
  input         collection_done_i;
  input  [31:0] collection_end_reason_i;
  input  [8:0]  overflow_mask_i;
  input  [9*32-1:0] dropped_count_i;
  input         event_valid_i;
  output        event_ready_o;
  input  [511:0] event_record_i;
  output        frame_valid_o;
  input         frame_ready_i;
  output [15:0] frame_type_o;
  output [31:0] frame_capture_id_o;
  output [31:0] frame_flags_o;
  output [31:0] payload_word_count_o;
  output [31:0] payload0_o;
  output [31:0] payload1_o;
  output [31:0] payload2_o;
  output [31:0] payload3_o;
  output [31:0] payload4_o;
  output [31:0] payload5_o;
  output [31:0] payload6_o;
  output [31:0] payload7_o;
  output [31:0] payload8_o;
  output [31:0] payload9_o;
  output [31:0] payload10_o;
  output [31:0] payload11_o;
  output [31:0] payload12_o;
  output [31:0] payload13_o;
  output [31:0] payload14_o;
  output [31:0] payload15_o;
  output        done_o;
  output [31:0] state_o;

  localparam [2:0] ST_IDLE = 3'd0;
  localparam [2:0] ST_SEND_META = 3'd1;
  localparam [2:0] ST_WAIT_EVENT = 3'd2;
  localparam [2:0] ST_SEND_EVENT = 3'd3;
  localparam [2:0] ST_SEND_END = 3'd4;
  localparam [2:0] ST_DONE = 3'd5;

  reg [2:0] cur_state;
  reg [2:0] nxt_state;
  reg [31:0] emitted_count_r;
  wire frame_fire_w;
  wire [31:0] dropped_total_w;

  assign frame_fire_w = frame_valid_o && frame_ready_i;
  assign dropped_total_w = dropped_count_i[1*32-1:0*32] +
                           dropped_count_i[2*32-1:1*32] +
                           dropped_count_i[3*32-1:2*32] +
                           dropped_count_i[4*32-1:3*32] +
                           dropped_count_i[5*32-1:4*32] +
                           dropped_count_i[6*32-1:5*32] +
                           dropped_count_i[7*32-1:6*32] +
                           dropped_count_i[8*32-1:7*32] +
                           dropped_count_i[9*32-1:8*32];

  assign frame_valid_o = (cur_state == ST_SEND_META) ||
                         (cur_state == ST_SEND_EVENT) ||
                         (cur_state == ST_SEND_END);
  assign frame_type_o = (cur_state == ST_SEND_META) ? 16'h8103 :
                        (cur_state == ST_SEND_EVENT) ? 16'h8104 : 16'h8105;
  assign frame_capture_id_o = capture_id_i;
  assign frame_flags_o = 32'd0;
  assign payload_word_count_o = 32'd16;
  assign event_ready_o = (cur_state == ST_SEND_EVENT) && frame_ready_i;
  assign done_o = (cur_state == ST_DONE);
  assign state_o = {29'd0, cur_state};

  assign payload0_o = (cur_state == ST_SEND_META) ? sample_rate_hz_i :
                      (cur_state == ST_SEND_EVENT) ? event_record_i[31:0] : collection_end_reason_i;
  assign payload1_o = (cur_state == ST_SEND_META) ? {23'd0, implemented_source_mask_i} :
                      (cur_state == ST_SEND_EVENT) ? event_record_i[63:32] : {23'd0, overflow_mask_i};
  assign payload2_o = (cur_state == ST_SEND_META) ? {23'd0, enabled_source_mask_i} :
                      (cur_state == ST_SEND_EVENT) ? event_record_i[95:64] : emitted_count_r + dropped_total_w;
  assign payload3_o = (cur_state == ST_SEND_META) ? {23'd0, design_gap_mask_i} :
                      (cur_state == ST_SEND_EVENT) ? event_record_i[127:96] : emitted_count_r;
  assign payload4_o = (cur_state == ST_SEND_META) ? 32'd64 :
                      (cur_state == ST_SEND_EVENT) ? event_record_i[159:128] : dropped_total_w;
  assign payload5_o = (cur_state == ST_SEND_META) ? 32'd0 :
                      (cur_state == ST_SEND_EVENT) ? event_record_i[191:160] : dropped_count_i[1*32-1:0*32];
  assign payload6_o = (cur_state == ST_SEND_META) ? watchdog_ticks_i :
                      (cur_state == ST_SEND_EVENT) ? event_record_i[223:192] : dropped_count_i[2*32-1:1*32];
  assign payload7_o = (cur_state == ST_SEND_META) ? hard_timeout_ticks_i :
                      (cur_state == ST_SEND_EVENT) ? event_record_i[255:224] : dropped_count_i[3*32-1:2*32];
  assign payload8_o = (cur_state == ST_SEND_EVENT) ? event_record_i[287:256] :
                      (cur_state == ST_SEND_END) ? dropped_count_i[4*32-1:3*32] : 32'd0;
  assign payload9_o = (cur_state == ST_SEND_EVENT) ? event_record_i[319:288] :
                      (cur_state == ST_SEND_END) ? dropped_count_i[5*32-1:4*32] : 32'd0;
  assign payload10_o = (cur_state == ST_SEND_EVENT) ? event_record_i[351:320] :
                       (cur_state == ST_SEND_END) ? dropped_count_i[6*32-1:5*32] : 32'd0;
  assign payload11_o = (cur_state == ST_SEND_EVENT) ? event_record_i[383:352] :
                       (cur_state == ST_SEND_END) ? dropped_count_i[7*32-1:6*32] : 32'd0;
  assign payload12_o = (cur_state == ST_SEND_EVENT) ? event_record_i[415:384] :
                       (cur_state == ST_SEND_END) ? dropped_count_i[8*32-1:7*32] : 32'd0;
  assign payload13_o = (cur_state == ST_SEND_EVENT) ? event_record_i[447:416] :
                       (cur_state == ST_SEND_END) ? dropped_count_i[9*32-1:8*32] : 32'd0;
  assign payload14_o = (cur_state == ST_SEND_EVENT) ? event_record_i[479:448] :
                       (cur_state == ST_SEND_END) ? {23'd0, enabled_source_mask_i} : 32'd0;
  assign payload15_o = (cur_state == ST_SEND_EVENT) ? event_record_i[511:480] :
                       (cur_state == ST_SEND_END) ? {23'd0, implemented_source_mask_i} : 32'd0;

  // 先发元数据，随后逐条事件，确认采集结束且 FIFO 排空后发结束帧。
  always @(*) begin
    nxt_state = cur_state;
    case (cur_state)
      ST_IDLE: if (start_i) nxt_state = ST_SEND_META;
      ST_SEND_META: if (frame_fire_w) nxt_state = ST_WAIT_EVENT;
      ST_WAIT_EVENT: begin
        if (event_valid_i) nxt_state = ST_SEND_EVENT;
        else if (collection_done_i) nxt_state = ST_SEND_END;
      end
      ST_SEND_EVENT: if (frame_fire_w) nxt_state = ST_WAIT_EVENT;
      ST_SEND_END: if (frame_fire_w) nxt_state = ST_DONE;
      ST_DONE: if (!start_i) nxt_state = ST_IDLE;
      default: nxt_state = ST_IDLE;
    endcase
  end

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      cur_state <= #UDLY ST_IDLE;
      emitted_count_r <= #UDLY 32'd0;
    end else begin
      cur_state <= #UDLY nxt_state;
      if ((cur_state == ST_IDLE) && start_i) begin
        emitted_count_r <= #UDLY 32'd0;
      end else if ((cur_state == ST_SEND_EVENT) && frame_fire_w) begin
        emitted_count_r <= #UDLY emitted_count_r + 32'd1;
      end
    end
  end

endmodule
