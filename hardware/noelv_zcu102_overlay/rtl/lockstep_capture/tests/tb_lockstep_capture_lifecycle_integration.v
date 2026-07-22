/**********************************************************
* 文件名: tb_lockstep_capture_lifecycle_integration.v
* 日期: 2026-07-22
* 版本: 1.3
* 更新记录: 增加跨域事件配置、双域复位恢复与 STOP 单脉冲检查。
* 描述: 经真实命令解析和帧发送链路验证 ARM、DRAINING、EVENT_END、
*       CAPTURE_END 与 CONFIGURED 的完整顺序。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_capture_lifecycle_integration;
  localparam [31:0] MAGIC = 32'h3243534c;
  localparam [15:0] TYPE_CONFIG_CAPTURE = 16'h0002;
  localparam [15:0] TYPE_ARM_CAPTURE = 16'h0003;
  localparam [15:0] TYPE_CONFIG_EVENTS = 16'h0006;
  localparam [15:0] TYPE_START_EVENT_STREAM = 16'h0007;
  localparam [15:0] TYPE_STATUS_RSP = 16'h8005;
  localparam [15:0] TYPE_CAPTURE_META = 16'h8100;
  localparam [15:0] TYPE_CAPTURE_END = 16'h8102;
  localparam [15:0] TYPE_EVENT_META = 16'h8103;
  localparam [15:0] TYPE_EVENT_DATA = 16'h8104;
  localparam [15:0] TYPE_EVENT_END = 16'h8105;

  reg clk;
  reg rst_n;
  reg sample_clk;
  reg sample_rst_n;
  reg rx_word_valid;
  wire rx_word_ready;
  reg [31:0] rx_word_data;
  reg [3:0] rx_be_valid;
  reg wide_sample_valid;
  reg [1023:0] wide_sample;
  reg [31:0] wide_sample_abs_index;
  wire tx_word_valid;
  wire [31:0] tx_word_data;
  wire tx_frame_start;
  wire tx_frame_end;
  wire [15:0] tx_frame_type;
  wire [31:0] device_state;

  reg [31:0] frame_words_r [0:20];
  integer failures_r;
  integer status_count_r;
  integer capture_meta_count_r;
  integer capture_end_count_r;
  integer event_meta_count_r;
  integer event_data_count_r;
  integer event_end_count_r;
  integer completed_frame_count_r;
  integer event_end_ordinal_r;
  integer capture_end_ordinal_r;
  integer frame_word_index_r;
  integer wait_count_r;
  integer stop_sample_pulse_count_r;
  integer stop_sample_pulse_before_r;
  reg [31:0] event_end_reason_r;
  reg draining_seen_r;

  lockstep_capture_protocol_stream_core #(
    .PROBE_SAMPLE_BITS     (1024),
    .MAX_PROBE_SAMPLE_BITS (1024),
    .PROBE_LANE_BITS       (128),
    .PROTOCOL_COUNT        (9),
    .SAMPLE_ADDR_WIDTH     (12),
    .LANE_INDEX_BITS       (4),
    .AHB_TRIGGER_MODE      (1)
  ) dut (
    .clk(clk),
    .rst_n(rst_n),
    .sample_clk(sample_clk),
    .sample_rst_n(sample_rst_n),
    .rx_word_valid_i(rx_word_valid),
    .rx_word_ready_o(rx_word_ready),
    .rx_word_data_i(rx_word_data),
    .rx_be_valid_i(rx_be_valid),
    .wide_sample_valid_i(wide_sample_valid),
    .wide_sample_i(wide_sample),
    .wide_sample_abs_index_i(wide_sample_abs_index),
    .capture_external_status_flags_i(32'd0),
    .capture_external_debug_state_i(32'd0),
    .tx_word_valid_o(tx_word_valid),
    .tx_word_ready_i(1'b1),
    .tx_word_data_o(tx_word_data),
    .tx_frame_start_o(tx_frame_start),
    .tx_frame_end_o(tx_frame_end),
    .tx_frame_type_o(tx_frame_type),
    .tx_frame_seq_o(),
    .source_enable_o(),
    .debug_device_state_o(device_state),
    .debug_capture_id_o(),
    .debug_parser_state_o(),
    .debug_command_state_o(),
    .debug_capture_state_o(),
    .debug_sequencer_state_o(),
    .debug_generator_state_o(),
    .debug_cfg_trigger_value_o(),
    .debug_cfg_valid_o(),
    .debug_wide_capture_state_o(),
    .debug_wide_capture_metadata_o(),
    .debug_wide_capture_samples_seen_o(),
    .debug_wide_capture_flags_o(),
    .debug_wide_pretrigger_samples_o(),
    .debug_wide_posttrigger_samples_o(),
    .debug_frame_source_state_o(),
    .debug_tx_bytes_o()
  );

  defparam dut.u_event_capture_controller.QUIET_GUARD_TICKS = 16;

  always #7.5 clk = !clk;
  always #4 sample_clk = !sample_clk;

  initial begin
    #20000000;
    $display("FAIL tb_lockstep_capture_lifecycle_integration simulation timeout");
    $finish;
  end

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
      $display("FAIL time=%0t message=%0s state=%0d status=%0d event_end=%0d capture_end=%0d",
               $time, message_i, device_state, status_count_r,
               event_end_count_r, capture_end_count_r);
    end
  endtask

  task finalize_frame;
    input [15:0] type_i;
    input [15:0] version_i;
    input [31:0] sequence_i;
    input integer payload_words_i;
    reg [31:0] crc_v;
    integer word_index_v;
    begin
      frame_words_r[0] = MAGIC;
      frame_words_r[1] = {type_i, version_i};
      frame_words_r[2] = 32'd32;
      frame_words_r[3] = payload_words_i * 4;
      frame_words_r[4] = sequence_i;
      frame_words_r[5] = 32'd0;
      frame_words_r[6] = 32'd0;
      frame_words_r[7] = 32'd0;
      crc_v = 32'hffffffff;
      for (word_index_v = 0; word_index_v < 8 + payload_words_i;
           word_index_v = word_index_v + 1)
        crc_v = crc32_word(crc_v, frame_words_r[word_index_v]);
      frame_words_r[7] = crc_v ^ 32'hffffffff;
    end
  endtask

  task send_frame;
    input integer word_count_i;
    integer word_index_v;
    begin
      for (word_index_v = 0; word_index_v < word_count_i; word_index_v = word_index_v + 1) begin
        while (!rx_word_ready) @(posedge clk);
        rx_word_data = frame_words_r[word_index_v];
        rx_word_valid = 1'b1;
        @(posedge clk);
        #2;
        rx_word_valid = 1'b0;
      end
    end
  endtask

  task wait_status_count;
    input integer target_i;
    begin
      wait_count_r = 0;
      while (status_count_r < target_i && wait_count_r < 10000) begin
        @(posedge clk);
        wait_count_r = wait_count_r + 1;
      end
      if (status_count_r < target_i) fail_check("timed out waiting for STATUS_RSP");
    end
  endtask

  always @(posedge sample_clk) begin
    if (!sample_rst_n)
      wide_sample_abs_index <= 32'd0;
    else if (wide_sample_valid)
      wide_sample_abs_index <= wide_sample_abs_index + 32'd1;
    if (sample_rst_n && dut.stop_sample_pulse_w)
      stop_sample_pulse_count_r = stop_sample_pulse_count_r + 1;
  end

  always @(posedge clk) begin
    if (tx_word_valid) begin
      if (tx_frame_start) frame_word_index_r = 0;
      if (tx_frame_type == TYPE_EVENT_END && frame_word_index_r == 8)
        event_end_reason_r = tx_word_data;
      if (tx_frame_end) begin
        completed_frame_count_r = completed_frame_count_r + 1;
        case (tx_frame_type)
          TYPE_STATUS_RSP: status_count_r = status_count_r + 1;
          TYPE_CAPTURE_META: capture_meta_count_r = capture_meta_count_r + 1;
          TYPE_CAPTURE_END: begin
            capture_end_count_r = capture_end_count_r + 1;
            capture_end_ordinal_r = completed_frame_count_r;
          end
          TYPE_EVENT_META: event_meta_count_r = event_meta_count_r + 1;
          TYPE_EVENT_DATA: event_data_count_r = event_data_count_r + 1;
          TYPE_EVENT_END: begin
            event_end_count_r = event_end_count_r + 1;
            event_end_ordinal_r = completed_frame_count_r;
          end
          default: begin end
        endcase
      end
      frame_word_index_r = frame_word_index_r + 1;
    end
    if (device_state == 32'd6) draining_seen_r = 1'b1;
  end

  initial begin
    clk = 1'b0;
    rst_n = 1'b0;
    sample_clk = 1'b0;
    sample_rst_n = 1'b0;
    rx_word_valid = 1'b0;
    rx_word_data = 32'd0;
    rx_be_valid = 4'hf;
    wide_sample_valid = 1'b1;
    wide_sample = 1024'd0;
    wide_sample[512] = 1'b1;
    wide_sample[513] = 1'b1;
    wide_sample[547] = 1'b1;
    wide_sample[576] = 1'b1;
    wide_sample[577] = 1'b1;
    wide_sample_abs_index = 32'd0;
    failures_r = 0;
    status_count_r = 0;
    capture_meta_count_r = 0;
    capture_end_count_r = 0;
    event_meta_count_r = 0;
    event_data_count_r = 0;
    event_end_count_r = 0;
    completed_frame_count_r = 0;
    event_end_ordinal_r = 0;
    capture_end_ordinal_r = 0;
    frame_word_index_r = 0;
    stop_sample_pulse_count_r = 0;
    stop_sample_pulse_before_r = 0;
    event_end_reason_r = 32'hffffffff;
    draining_seen_r = 1'b0;

    repeat (6) @(posedge clk);
    rst_n = 1'b1;
    sample_rst_n = 1'b1;
    repeat (8) @(posedge clk);
    repeat (6) @(posedge sample_clk);
    if (!dut.event_fifo_domains_ready_sample_w || !dut.event_fifo_domains_ready_read_w)
      fail_check("event async FIFO domains did not leave reset");

    @(negedge sample_clk);
    sample_rst_n = 1'b0;
    repeat (4) @(posedge clk);
    if (dut.event_fifo_read_rst_n_w)
      fail_check("event FIFO read domain ignored sample reset");
    @(negedge sample_clk);
    sample_rst_n = 1'b1;
    repeat (8) @(posedge sample_clk);
    repeat (8) @(posedge clk);
    if (!dut.event_fifo_domains_ready_sample_w || !dut.event_fifo_domains_ready_read_w)
      fail_check("event FIFO did not recover from sample reset");

    @(negedge clk);
    rst_n = 1'b0;
    repeat (4) @(posedge sample_clk);
    if (dut.event_fifo_write_rst_n_w)
      fail_check("event FIFO write domain ignored host reset");
    @(negedge clk);
    rst_n = 1'b1;
    repeat (8) @(posedge clk);
    repeat (8) @(posedge sample_clk);
    if (!dut.event_fifo_domains_ready_sample_w || !dut.event_fifo_domains_ready_read_w)
      fail_check("event FIFO did not recover from host reset");

    frame_words_r[8] = 32'd120000000;
    frame_words_r[9] = 32'd4096;
    frame_words_r[10] = 32'd2047;
    frame_words_r[11] = 32'd2049;
    frame_words_r[12] = 32'h000001ff;
    frame_words_r[13] = 32'd0;
    frame_words_r[14] = 32'h04000400;
    frame_words_r[15] = 32'd0;
    frame_words_r[16] = 32'hffffffff;
    frame_words_r[17] = 32'd0;
    frame_words_r[18] = 32'd0;
    frame_words_r[19] = 32'd0;
    frame_words_r[20] = 32'd1000000;
    finalize_frame(TYPE_CONFIG_CAPTURE, 16'd2, 32'd1, 13);
    send_frame(21);
    wait_status_count(1);

    frame_words_r[8] = 32'h00000002;
    frame_words_r[9] = 32'd0;
    frame_words_r[10] = 32'd1000000;
    frame_words_r[11] = 32'd2000000;
    finalize_frame(TYPE_CONFIG_EVENTS, 16'd3, 32'd2, 4);
    send_frame(12);
    wait_status_count(2);
    repeat (4) @(posedge sample_clk);
    if (dut.cfg_event_watchdog_sample_d2_r != 32'd1000000 ||
        dut.cfg_event_hard_timeout_sample_d2_r != 32'd2000000)
      fail_check("event timeout configuration did not cross into sample domain");

    finalize_frame(TYPE_ARM_CAPTURE, 16'd2, 32'd3, 0);
    send_frame(8);
    wait_status_count(3);
    if (device_state != 32'd4) fail_check("ARM did not enter CAPTURING_PRETRIGGER");

    finalize_frame(TYPE_START_EVENT_STREAM, 16'd3, 32'd4, 0);
    send_frame(8);
    repeat (8) @(posedge clk);

    wait_count_r = 0;
    while (!dut.wide_pretrigger_ready_w && wait_count_r < 5000) begin
      @(posedge sample_clk);
      wait_count_r = wait_count_r + 1;
    end
    if (!dut.wide_pretrigger_ready_w)
      fail_check("pretrigger history did not become ready before address trigger");

    @(negedge sample_clk);
    wide_sample[418] = 1'b1;
    wide_sample[429] = 1'b1;
    @(negedge sample_clk);
    wide_sample[418] = 1'b0;
    wide_sample[429] = 1'b0;

    wait_count_r = 0;
    while (device_state != 32'd5 && wait_count_r < 1000) begin
      @(posedge clk);
      wait_count_r = wait_count_r + 1;
    end
    if (device_state != 32'd5) fail_check("trigger did not enter CAPTURING_POSTTRIGGER");
    if (dut.cfg_event_watchdog_active_sample_r != 32'd1000000 ||
        dut.cfg_event_hard_timeout_active_sample_r != 32'd2000000)
      fail_check("event timeout configuration was not latched for capture");

    @(negedge sample_clk);
    wide_sample[518] = 1'b1;
    @(negedge sample_clk);
    wide_sample[518] = 1'b0;
    force dut.u_event_capture_controller.program_done_i = 1'b1;
    @(posedge sample_clk);
    #2;
    release dut.u_event_capture_controller.program_done_i;

    wait_count_r = 0;
    while (!draining_seen_r && wait_count_r < 1000) begin
      @(posedge clk);
      wait_count_r = wait_count_r + 1;
    end
    if (!draining_seen_r) fail_check("program_done did not expose DRAINING");

    wait_count_r = 0;
    while ((capture_end_count_r == 0 || event_end_count_r == 0 || device_state != 32'd2) &&
           wait_count_r < 900000) begin
      @(posedge clk);
      wait_count_r = wait_count_r + 1;
    end
    if (capture_meta_count_r != 1 || capture_end_count_r != 1 ||
        event_meta_count_r != 1 || event_data_count_r < 1 || event_end_count_r != 1)
      fail_check("capture/event frame closure count mismatch");
    if (event_end_reason_r != 32'd0)
      fail_check("EVENT_END did not preserve program_done reason");
    if (event_end_ordinal_r == 0 || capture_end_ordinal_r <= event_end_ordinal_r)
      fail_check("CAPTURE_END was emitted before EVENT_END completed");
    if (device_state != 32'd2)
      fail_check("completed capture did not return CONFIGURED");
    if (!dut.event_async_empty_w || dut.event_end_pending_read_r)
      fail_check("event async FIFO was not empty at completion");

    stop_sample_pulse_before_r = stop_sample_pulse_count_r;
    @(negedge clk);
    dut.stop_toggle_clk_r = !dut.stop_toggle_clk_r;
    repeat (12) @(posedge sample_clk);
    $display("STOP_CDC_DIAGNOSTIC before=%0d after=%0d source=%b d1=%b d2=%b d3=%b",
             stop_sample_pulse_before_r, stop_sample_pulse_count_r,
             dut.stop_toggle_clk_r, dut.stop_toggle_sample_d1_r,
             dut.stop_toggle_sample_d2_r, dut.stop_toggle_sample_d3_r);
    if (stop_sample_pulse_count_r != stop_sample_pulse_before_r + 1)
      fail_check("STOP did not cross domains as exactly one pulse");

    if (failures_r == 0)
      $display("PASS tb_lockstep_capture_lifecycle_integration");
    else
      $display("FAIL tb_lockstep_capture_lifecycle_integration failures=%0d", failures_r);
    $finish;
  end
endmodule
