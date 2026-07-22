/**********************************************************
* 文件名: tb_lockstep_event_overflow_pipeline.v
* 日期: 2026-07-22
* 版本: 1.0
* 更新记录: 新增事件全链路溢出、排空、分类与 CRC 集成回归。
* 描述: 使用真实事件编码、分源 FIFO、跨时钟 FIFO、帧仲裁和 CRC 发送链，
*       验证持续 AHB 事件在下游反压时以 overflow 结束并完整排空。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_event_overflow_pipeline;
  localparam [15:0] TYPE_WIDE_TEST = 16'h81f0;
  localparam [15:0] TYPE_EVENT_META = 16'h8103;
  localparam [15:0] TYPE_EVENT_DATA = 16'h8104;
  localparam [15:0] TYPE_EVENT_END = 16'h8105;

  reg sample_clk;
  reg read_clk;
  reg sample_rst_n;
  reg read_rst_n;
  reg capture_start;
  reg sample_valid;
  reg [1023:0] sample_data;
  reg [63:0] timestamp_ticks;

  wire capture_active;
  wire capture_draining;
  wire controller_done;
  wire [31:0] controller_end_reason;
  wire [8:0] source_push;
  wire [9*512-1:0] source_record;
  wire protocol_busy;
  wire [8:0] source_accept;
  wire [8:0] source_drop;
  wire event_arb_valid;
  wire event_arb_ready;
  wire [511:0] event_arb_record;
  wire [3:0] event_arb_source;
  wire [8:0] overflow_mask;
  wire [9*32-1:0] accepted_count;
  wire [9*32-1:0] emitted_count;
  wire [9*32-1:0] dropped_count;

  wire async_write_ready;
  wire async_write_drop;
  wire async_read_valid;
  wire async_read_empty;
  wire async_read_ready;
  wire [511:0] async_read_data;

  reg controller_ended_sample_r;
  reg end_toggle_sample_r;
  reg [31:0] end_reason_stable_sample_r;
  reg [8:0] overflow_stable_sample_r;
  reg [9*32-1:0] dropped_stable_sample_r;
  reg end_toggle_read_d1_r;
  reg end_toggle_read_d2_r;
  reg end_toggle_read_d3_r;
  reg end_pending_read_r;
  reg collection_done_read_r;
  reg [31:0] end_reason_read_r;
  reg [8:0] overflow_read_r;
  reg [9*32-1:0] dropped_read_r;

  reg event_stream_start;
  wire event_frame_valid;
  wire event_frame_ready;
  wire [15:0] event_frame_type;
  wire [31:0] event_frame_capture_id;
  wire [31:0] event_frame_flags;
  wire [31:0] event_payload_word_count;
  wire [511:0] event_payload;
  wire event_frame_done;

  reg wide_frame_valid;
  wire wide_frame_ready;
  wire stream_valid;
  wire event_select;
  wire tx_frame_ready;
  wire [15:0] mux_frame_type;
  wire [31:0] mux_frame_capture_id;
  wire [31:0] mux_frame_flags;
  wire [31:0] mux_payload_word_count;
  wire [511:0] mux_payload;
  wire tx_word_valid;
  reg tx_word_ready;
  wire [31:0] tx_word_data;
  wire tx_frame_start;
  wire tx_frame_end;
  wire [15:0] tx_frame_type;
  wire [31:0] tx_frame_sequence;

  integer failures_r;
  integer sample_cycle_r;
  integer first_drop_cycle_r;
  integer controller_done_cycle_r;
  integer drop_pulse_count_r;
  integer push_after_done_count_r;
  integer crc_failure_count_r;
  integer frame_sequence_gap_count_r;
  integer wide_frame_count_r;
  integer event_meta_count_r;
  integer event_data_count_r;
  integer event_end_count_r;
  integer frame_word_index_r;
  integer wait_count_r;
  reg [31:0] running_crc_r;
  reg [31:0] received_crc_r;
  reg [31:0] previous_frame_sequence_r;
  reg have_frame_sequence_r;
  reg [31:0] event_end_reason_r;
  reg [31:0] event_end_overflow_r;
  reg [31:0] event_end_accepted_r;
  reg [31:0] event_end_emitted_r;
  reg [31:0] event_end_dropped_r;
  reg [31:0] event_end_drop_count_r [0:8];
  reg [31:0] crc_input_r;
  reg [31:0] crc_next_r;
  integer drop_index_r;

  wire end_toggle_pulse_read_w;
  assign end_toggle_pulse_read_w = end_toggle_read_d2_r ^ end_toggle_read_d3_r;
  assign event_arb_ready = async_write_ready;

  lockstep_event_capture_controller #(
    .QUIET_GUARD_TICKS (8)
  ) u_controller (
    .clk(sample_clk),
    .rst_n(sample_rst_n),
    .capture_start_i(capture_start),
    .stop_i(1'b0),
    .program_done_i(1'b0),
    .overflow_i((|source_drop) || (|overflow_mask)),
    .activity_i(|source_push),
    .protocol_busy_i(protocol_busy),
    .watchdog_ticks_i(32'd0),
    .hard_timeout_ticks_i(32'hffffffff),
    .capture_active_o(capture_active),
    .draining_o(capture_draining),
    .capture_done_pulse_o(controller_done),
    .end_reason_o(controller_end_reason)
  );

  lockstep_protocol_event_encoder u_encoder (
    .clk(sample_clk),
    .rst_n(sample_rst_n),
    .capture_start_i(capture_start),
    .capture_active_i(capture_active),
    .capture_id_i(32'd7),
    .timestamp_i(timestamp_ticks),
    .source_enable_mask_i(9'h001),
    .sample_valid_i(sample_valid),
    .sample_i(sample_data),
    .source_push_o(source_push),
    .source_record_o(source_record),
    .protocol_busy_o(protocol_busy)
  );

  lockstep_event_capture_core #(
    .AHB_FIFO_DEPTH(4),
    .AHB_FIFO_ADDR_WIDTH(2),
    .UART_FIFO_DEPTH(2),
    .UART_FIFO_ADDR_WIDTH(1),
    .SPI_FIFO_DEPTH(2),
    .SPI_FIFO_ADDR_WIDTH(1),
    .CAN_FIFO_DEPTH(2),
    .CAN_FIFO_ADDR_WIDTH(1),
    .I2C_FIFO_DEPTH(2),
    .I2C_FIFO_ADDR_WIDTH(1),
    .ETH_FIFO_DEPTH(2),
    .ETH_FIFO_ADDR_WIDTH(1),
    .USB_FIFO_DEPTH(2),
    .USB_FIFO_ADDR_WIDTH(1),
    .JTAG_FIFO_DEPTH(2),
    .JTAG_FIFO_ADDR_WIDTH(1),
    .MISMATCH_FIFO_DEPTH(2),
    .MISMATCH_FIFO_ADDR_WIDTH(1)
  ) u_capture_core (
    .clk(sample_clk),
    .rst_n(sample_rst_n),
    .capture_start_i(capture_start),
    .source_enable_mask_i(9'h001),
    .source_push_i(source_push),
    .source_record_i(source_record),
    .source_accept_o(source_accept),
    .source_drop_o(source_drop),
    .event_valid_o(event_arb_valid),
    .event_ready_i(event_arb_ready),
    .event_record_o(event_arb_record),
    .event_source_o(event_arb_source),
    .overflow_mask_o(overflow_mask),
    .accepted_count_o(accepted_count),
    .emitted_count_o(emitted_count),
    .dropped_count_o(dropped_count)
  );

  lockstep_event_async_fifo #(
    .ADDR_WIDTH(2),
    .DEPTH(4)
  ) u_async_fifo (
    .write_clk(sample_clk),
    .write_rst_n(sample_rst_n),
    .write_valid_i(event_arb_valid),
    .write_ready_o(async_write_ready),
    .write_data_i(event_arb_record),
    .write_drop_o(async_write_drop),
    .read_clk(read_clk),
    .read_rst_n(read_rst_n),
    .read_valid_o(async_read_valid),
    .read_empty_o(async_read_empty),
    .read_ready_i(async_read_ready),
    .read_data_o(async_read_data)
  );

  always @(posedge sample_clk or negedge sample_rst_n) begin
    if (!sample_rst_n) begin
      controller_ended_sample_r <= 1'b0;
      end_toggle_sample_r <= 1'b0;
      end_reason_stable_sample_r <= 32'd0;
      overflow_stable_sample_r <= 9'd0;
      dropped_stable_sample_r <= {9*32{1'b0}};
    end else if (capture_start) begin
      controller_ended_sample_r <= 1'b0;
    end else begin
      if (controller_done)
        controller_ended_sample_r <= 1'b1;
      if (controller_ended_sample_r && !event_arb_valid) begin
        controller_ended_sample_r <= 1'b0;
        end_toggle_sample_r <= !end_toggle_sample_r;
        end_reason_stable_sample_r <= controller_end_reason;
        overflow_stable_sample_r <= overflow_mask;
        dropped_stable_sample_r <= dropped_count;
      end
    end
  end

  always @(posedge read_clk or negedge read_rst_n) begin
    if (!read_rst_n) begin
      end_toggle_read_d1_r <= 1'b0;
      end_toggle_read_d2_r <= 1'b0;
      end_toggle_read_d3_r <= 1'b0;
      end_pending_read_r <= 1'b0;
      collection_done_read_r <= 1'b0;
      end_reason_read_r <= 32'd0;
      overflow_read_r <= 9'd0;
      dropped_read_r <= {9*32{1'b0}};
    end else begin
      end_toggle_read_d1_r <= end_toggle_sample_r;
      end_toggle_read_d2_r <= end_toggle_read_d1_r;
      end_toggle_read_d3_r <= end_toggle_read_d2_r;
      if (capture_start) begin
        end_pending_read_r <= 1'b0;
        collection_done_read_r <= 1'b0;
      end else if (end_toggle_pulse_read_w) begin
        end_pending_read_r <= 1'b1;
      end else if (end_pending_read_r && async_read_empty) begin
        end_pending_read_r <= 1'b0;
        collection_done_read_r <= 1'b1;
        end_reason_read_r <= end_reason_stable_sample_r;
        overflow_read_r <= overflow_stable_sample_r;
        dropped_read_r <= dropped_stable_sample_r;
      end
    end
  end

  lockstep_event_frame_source u_event_frame_source (
    .clk(read_clk),
    .rst_n(read_rst_n),
    .start_i(event_stream_start),
    .capture_id_i(32'd7),
    .sample_rate_hz_i(32'd120000000),
    .implemented_source_mask_i(9'h19f),
    .enabled_source_mask_i(9'h001),
    .design_gap_mask_i(9'h060),
    .watchdog_ticks_i(32'd0),
    .hard_timeout_ticks_i(32'hffffffff),
    .collection_done_i(collection_done_read_r),
    .collection_end_reason_i(end_reason_read_r),
    .overflow_mask_i(overflow_read_r),
    .dropped_count_i(dropped_read_r),
    .event_valid_i(async_read_valid),
    .event_ready_o(async_read_ready),
    .event_record_i(async_read_data),
    .frame_valid_o(event_frame_valid),
    .frame_ready_i(event_frame_ready),
    .frame_type_o(event_frame_type),
    .frame_capture_id_o(event_frame_capture_id),
    .frame_flags_o(event_frame_flags),
    .payload_word_count_o(event_payload_word_count),
    .payload0_o(event_payload[1*32-1:0*32]),
    .payload1_o(event_payload[2*32-1:1*32]),
    .payload2_o(event_payload[3*32-1:2*32]),
    .payload3_o(event_payload[4*32-1:3*32]),
    .payload4_o(event_payload[5*32-1:4*32]),
    .payload5_o(event_payload[6*32-1:5*32]),
    .payload6_o(event_payload[7*32-1:6*32]),
    .payload7_o(event_payload[8*32-1:7*32]),
    .payload8_o(event_payload[9*32-1:8*32]),
    .payload9_o(event_payload[10*32-1:9*32]),
    .payload10_o(event_payload[11*32-1:10*32]),
    .payload11_o(event_payload[12*32-1:11*32]),
    .payload12_o(event_payload[13*32-1:12*32]),
    .payload13_o(event_payload[14*32-1:13*32]),
    .payload14_o(event_payload[15*32-1:14*32]),
    .payload15_o(event_payload[16*32-1:15*32]),
    .done_o(event_frame_done),
    .state_o()
  );

  lockstep_capture_stream_arbiter u_stream_arbiter (
    .clk(read_clk),
    .rst_n(read_rst_n),
    .event_valid_i(event_frame_valid),
    .event_ready_o(event_frame_ready),
    .wide_valid_i(wide_frame_valid),
    .wide_ready_o(wide_frame_ready),
    .downstream_ready_i(tx_frame_ready),
    .stream_valid_o(stream_valid),
    .event_select_o(event_select)
  );

  assign mux_frame_type = event_select ? event_frame_type : TYPE_WIDE_TEST;
  assign mux_frame_capture_id = event_select ? event_frame_capture_id : 32'd7;
  assign mux_frame_flags = event_select ? event_frame_flags : 32'd0;
  assign mux_payload_word_count = event_select ? event_payload_word_count : 32'd1;
  assign mux_payload[1*32-1:0*32] = event_select ?
         event_payload[1*32-1:0*32] : 32'hcafebabe;
  assign mux_payload[511:32] = event_select ? event_payload[511:32] : 480'd0;

  lockstep_tx_frame_generator u_tx_frame_generator (
    .clk(read_clk),
    .rst_n(read_rst_n),
    .frame_valid_i(stream_valid),
    .frame_ready_o(tx_frame_ready),
    .frame_type_i(mux_frame_type),
    .frame_capture_id_i(mux_frame_capture_id),
    .frame_flags_i(mux_frame_flags),
    .payload_word_count_i(mux_payload_word_count),
    .payload0_i(mux_payload[1*32-1:0*32]),
    .payload1_i(mux_payload[2*32-1:1*32]),
    .payload2_i(mux_payload[3*32-1:2*32]),
    .payload3_i(mux_payload[4*32-1:3*32]),
    .payload4_i(mux_payload[5*32-1:4*32]),
    .payload5_i(mux_payload[6*32-1:5*32]),
    .payload6_i(mux_payload[7*32-1:6*32]),
    .payload7_i(mux_payload[8*32-1:7*32]),
    .payload8_i(mux_payload[9*32-1:8*32]),
    .payload9_i(mux_payload[10*32-1:9*32]),
    .payload10_i(mux_payload[11*32-1:10*32]),
    .payload11_i(mux_payload[12*32-1:11*32]),
    .payload12_i(mux_payload[13*32-1:12*32]),
    .payload13_i(mux_payload[14*32-1:13*32]),
    .payload14_i(mux_payload[15*32-1:14*32]),
    .payload15_i(mux_payload[16*32-1:15*32]),
    .tx_word_valid_o(tx_word_valid),
    .tx_word_ready_i(tx_word_ready),
    .tx_word_data_o(tx_word_data),
    .tx_frame_start_o(tx_frame_start),
    .tx_frame_end_o(tx_frame_end),
    .tx_frame_type_o(tx_frame_type),
    .tx_frame_seq_o(tx_frame_sequence),
    .debug_state_o()
  );

  always #4 sample_clk = !sample_clk;
  always #7.5 read_clk = !read_clk;

  function [31:0] crc32_word;
    input [31:0] crc_i;
    input [31:0] data_i;
    reg [31:0] crc_v;
    reg [7:0] byte_v;
    integer byte_index_v;
    integer bit_index_v;
    begin
      crc_v = crc_i;
      for (byte_index_v = 0; byte_index_v < 4; byte_index_v = byte_index_v + 1) begin
        case (byte_index_v)
          0: byte_v = data_i[7:0];
          1: byte_v = data_i[15:8];
          2: byte_v = data_i[23:16];
          default: byte_v = data_i[31:24];
        endcase
        crc_v = crc_v ^ {24'd0, byte_v};
        for (bit_index_v = 0; bit_index_v < 8; bit_index_v = bit_index_v + 1) begin
          if (crc_v[0])
            crc_v = (crc_v >> 1) ^ 32'hedb88320;
          else
            crc_v = crc_v >> 1;
        end
      end
      crc32_word = crc_v;
    end
  endfunction

  task fail_check;
    input [1023:0] message_i;
    begin
      failures_r = failures_r + 1;
      $display("FAIL time=%0t message=%0s active=%b overflow=%03x drops=%0d event_data=%0d first_drop_cycle=%0d done_cycle=%0d",
               $time, message_i, capture_active, overflow_mask,
               dropped_count[31:0], event_data_count_r,
               first_drop_cycle_r, controller_done_cycle_r);
    end
  endtask

  always @(posedge sample_clk) begin
    if (!sample_rst_n) begin
      timestamp_ticks <= 64'd0;
      sample_cycle_r = 0;
      first_drop_cycle_r = -1;
      controller_done_cycle_r = -1;
      drop_pulse_count_r = 0;
      push_after_done_count_r = 0;
    end else begin
      timestamp_ticks <= timestamp_ticks + 64'd1;
      sample_cycle_r = sample_cycle_r + 1;
      #2;
      if (source_drop[0]) begin
        drop_pulse_count_r = drop_pulse_count_r + 1;
        if (first_drop_cycle_r < 0)
          first_drop_cycle_r = sample_cycle_r;
      end
      if (controller_done && controller_done_cycle_r < 0)
        controller_done_cycle_r = sample_cycle_r;
      if (controller_done_cycle_r >= 0 && source_push != 9'd0)
        push_after_done_count_r = push_after_done_count_r + 1;
    end
  end

  always @(posedge read_clk) begin
    if (!read_rst_n) begin
      wide_frame_valid <= 1'b0;
    end else if (wide_frame_valid && wide_frame_ready) begin
      wide_frame_valid <= 1'b0;
    end
  end

  always @(posedge read_clk) begin
    if (read_rst_n && tx_word_valid && tx_word_ready) begin
      if (tx_frame_start) begin
        frame_word_index_r = 0;
        running_crc_r = 32'hffffffff;
        if (have_frame_sequence_r && tx_frame_sequence != previous_frame_sequence_r + 32'd1)
          frame_sequence_gap_count_r = frame_sequence_gap_count_r + 1;
        previous_frame_sequence_r = tx_frame_sequence;
        have_frame_sequence_r = 1'b1;
      end

      crc_input_r = (frame_word_index_r == 7) ? 32'd0 : tx_word_data;
      crc_next_r = crc32_word(running_crc_r, crc_input_r);
      if (frame_word_index_r == 7)
        received_crc_r = tx_word_data;

      if (tx_frame_type == TYPE_EVENT_END) begin
        case (frame_word_index_r)
          8: event_end_reason_r = tx_word_data;
          9: event_end_overflow_r = tx_word_data;
          10: event_end_accepted_r = tx_word_data;
          11: event_end_emitted_r = tx_word_data;
          12: event_end_dropped_r = tx_word_data;
          13: event_end_drop_count_r[0] = tx_word_data;
          14: event_end_drop_count_r[1] = tx_word_data;
          15: event_end_drop_count_r[2] = tx_word_data;
          16: event_end_drop_count_r[3] = tx_word_data;
          17: event_end_drop_count_r[4] = tx_word_data;
          18: event_end_drop_count_r[5] = tx_word_data;
          19: event_end_drop_count_r[6] = tx_word_data;
          20: event_end_drop_count_r[7] = tx_word_data;
          21: event_end_drop_count_r[8] = tx_word_data;
          default: begin end
        endcase
      end

      if (tx_frame_end) begin
        if (received_crc_r != (crc_next_r ^ 32'hffffffff))
          crc_failure_count_r = crc_failure_count_r + 1;
        case (tx_frame_type)
          TYPE_WIDE_TEST: wide_frame_count_r = wide_frame_count_r + 1;
          TYPE_EVENT_META: event_meta_count_r = event_meta_count_r + 1;
          TYPE_EVENT_DATA: event_data_count_r = event_data_count_r + 1;
          TYPE_EVENT_END: event_end_count_r = event_end_count_r + 1;
          default: fail_check("unexpected frame type in overflow pipeline");
        endcase
      end

      running_crc_r = crc_next_r;
      frame_word_index_r = frame_word_index_r + 1;
    end
  end

  initial begin
    #2000000;
    $display("FAIL tb_lockstep_event_overflow_pipeline simulation timeout");
    $finish;
  end

  initial begin
    sample_clk = 1'b0;
    read_clk = 1'b0;
    sample_rst_n = 1'b0;
    read_rst_n = 1'b0;
    capture_start = 1'b0;
    sample_valid = 1'b1;
    sample_data = 1024'd0;
    sample_data[442] = 1'b1;
    sample_data[429] = 1'b1;
    sample_data[425] = 1'b1;
    sample_data[416] = 1'b1;
    timestamp_ticks = 64'd0;
    event_stream_start = 1'b0;
    wide_frame_valid = 1'b0;
    tx_word_ready = 1'b0;
    failures_r = 0;
    sample_cycle_r = 0;
    first_drop_cycle_r = -1;
    controller_done_cycle_r = -1;
    drop_pulse_count_r = 0;
    push_after_done_count_r = 0;
    crc_failure_count_r = 0;
    frame_sequence_gap_count_r = 0;
    wide_frame_count_r = 0;
    event_meta_count_r = 0;
    event_data_count_r = 0;
    event_end_count_r = 0;
    frame_word_index_r = 0;
    running_crc_r = 32'hffffffff;
    received_crc_r = 32'd0;
    previous_frame_sequence_r = 32'd0;
    have_frame_sequence_r = 1'b0;
    event_end_reason_r = 32'hffffffff;
    event_end_overflow_r = 32'd0;
    event_end_accepted_r = 32'd0;
    event_end_emitted_r = 32'd0;
    event_end_dropped_r = 32'd0;
    crc_input_r = 32'd0;
    crc_next_r = 32'd0;
    for (drop_index_r = 0; drop_index_r < 9; drop_index_r = drop_index_r + 1)
      event_end_drop_count_r[drop_index_r] = 32'd0;

    repeat (6) @(posedge sample_clk);
    sample_rst_n = 1'b1;
    repeat (4) @(posedge read_clk);
    read_rst_n = 1'b1;
    repeat (6) @(posedge sample_clk);

    @(negedge sample_clk);
    capture_start = 1'b1;
    event_stream_start = 1'b1;
    wide_frame_valid = 1'b1;
    @(negedge sample_clk);
    capture_start = 1'b0;

    wait_count_r = 0;
    while (controller_done_cycle_r < 0 && wait_count_r < 100) begin
      @(posedge sample_clk);
      wait_count_r = wait_count_r + 1;
    end
    if (first_drop_cycle_r < 0)
      fail_check("backpressure did not produce an AHB FIFO drop");
    if (controller_done_cycle_r < 0)
      fail_check("overflow did not terminate event collection");
    if (controller_done_cycle_r - first_drop_cycle_r > 1)
      fail_check("overflow response exceeded one sample clock");
    if (controller_end_reason != 32'd3)
      fail_check("controller did not classify overflow as reason 3");
    if (!capture_draining)
      fail_check("overflow did not expose DRAINING while FIFO data remained");
    if (overflow_mask != 9'h001)
      fail_check("overflow mask was not isolated to AHB");
    if (drop_pulse_count_r != 1)
      fail_check("overflow stop did not end on the first dropped event");
    if (push_after_done_count_r != 0)
      fail_check("encoder continued producing events after overflow completion");

    tx_word_ready = 1'b1;
    wait_count_r = 0;
    while (event_end_count_r == 0 && wait_count_r < 10000) begin
      @(posedge read_clk);
      wait_count_r = wait_count_r + 1;
    end
    if (event_end_count_r != 1)
      fail_check("overflow pipeline did not emit exactly one EVENT_END");
    if (event_meta_count_r != 1 || wide_frame_count_r != 1)
      fail_check("frame arbiter did not preserve event meta and competing wide frame");
    if (event_data_count_r < 1)
      fail_check("accepted events were not drained as EVENT_DATA");
    if (event_end_reason_r != 32'd3 || event_end_overflow_r != 32'h00000001)
      fail_check("EVENT_END did not preserve overflow reason and mask");
    if (event_end_emitted_r != event_data_count_r ||
        event_end_accepted_r != event_end_emitted_r + event_end_dropped_r)
      fail_check("EVENT_END accepted/emitted/dropped invariant failed");
    if (event_end_dropped_r != drop_pulse_count_r ||
        event_end_drop_count_r[0] != event_end_dropped_r)
      fail_check("EVENT_END AHB dropped count did not match the real FIFO drops");
    for (drop_index_r = 1; drop_index_r < 9; drop_index_r = drop_index_r + 1)
      if (event_end_drop_count_r[drop_index_r] != 32'd0)
        fail_check("EVENT_END attributed AHB loss to another protocol");
    if (crc_failure_count_r != 0)
      fail_check("transmitted frame CRC check failed");
    if (frame_sequence_gap_count_r != 0)
      fail_check("transmitted frame sequence was not continuous");
    if (!async_read_empty || event_arb_valid || !event_frame_done)
      fail_check("overflow pipeline did not drain both FIFO levels");

    if (failures_r == 0)
      $display("PASS tb_lockstep_event_overflow_pipeline");
    else
      $display("FAIL tb_lockstep_event_overflow_pipeline failures=%0d", failures_r);
    $finish;
  end
endmodule
