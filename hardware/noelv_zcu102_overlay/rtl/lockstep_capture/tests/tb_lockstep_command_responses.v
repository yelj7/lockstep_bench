/**********************************************************
* 文件名: tb_lockstep_command_responses.v
* 日期: 2026-07-17
* 版本: 1.1
* 更新记录: 增加 START_EVENT_STREAM 无响应释放命令回归。
* 描述: 验证配置、ARM 接受确认及事件流释放命令合同。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_command_responses;
  reg         clk;
  reg         rst_n;
  reg         cmd_valid_i;
  wire        cmd_ready_o;
  reg  [15:0] cmd_type_i;
  reg  [31:0] cmd_seq_i;
  reg  [31:0] cmd_payload0_i;
  reg  [31:0] cmd_payload1_i;
  reg  [31:0] cmd_payload2_i;
  reg  [31:0] cmd_payload3_i;
  reg         arm_accepted_i;
  wire        cfg_valid_o;
  wire        arm_o;
  wire        event_stream_start_o;
  wire        frame_valid_o;
  reg         frame_ready_i;
  wire        stop_o;
  reg         capture_frame_done_i;
  wire [15:0] frame_type_o;
  wire [31:0] frame_capture_id_o;
  wire [31:0] payload0_o;
  wire [31:0] payload1_o;
  wire [31:0] payload2_o;
  wire [31:0] payload_word_count_o;
  wire [31:0] device_state_o;
  wire [31:0] cfg_event_enable_mask_o;
  wire [31:0] cfg_event_limit_o;
  wire [31:0] cfg_event_watchdog_ticks_o;
  wire [31:0] cfg_event_hard_timeout_ticks_o;
  integer     failures_r;
  integer     arm_pulse_count_r;
  integer     event_start_pulse_count_r;

  lockstep_command_state_machine #(
    .MAX_SAMPLE_COUNT        (32'd4096),
    .HELLO_PHYSICAL_CHANNELS (32'd1024),
    .HELLO_SAMPLE_WORD_BITS  (32'd1024)
  ) dut (
    .clk(clk),
    .rst_n(rst_n),
    .cmd_valid_i(cmd_valid_i),
    .cmd_ready_o(cmd_ready_o),
    .cmd_type_i(cmd_type_i),
    .cmd_seq_i(cmd_seq_i),
    .cmd_payload_words_i(32'd13),
    .cmd_payload0_i(cmd_payload0_i),
    .cmd_payload1_i(cmd_payload1_i),
    .cmd_payload2_i(cmd_payload2_i),
    .cmd_payload3_i(cmd_payload3_i),
    .cmd_payload4_i(32'h000001ff),
    .cmd_payload5_i(32'd0),
    .cmd_payload6_i(32'h04000400),
    .cmd_payload7_i(32'd0),
    .cmd_payload8_i(32'd0),
    .cmd_payload9_i(32'd0),
    .cmd_payload10_i(32'd0),
    .cmd_payload11_i(32'd0),
    .cmd_payload12_i(32'd1200000000),
    .cmd_error_valid_i(1'b0),
    .cmd_error_ready_o(),
    .cmd_error_code_i(32'd0),
    .cmd_error_type_i(16'd0),
    .cmd_error_seq_i(32'd0),
    .cmd_error_detail0_i(32'd0),
    .cmd_error_detail1_i(32'd0),
    .cfg_valid_o(cfg_valid_o),
    .cfg_ready_i(1'b1),
    .cfg_sample_rate_hz_o(),
    .cfg_sample_count_o(),
    .cfg_pretrigger_count_o(),
    .cfg_posttrigger_count_o(),
    .cfg_channel_mask_o(),
    .cfg_input_invert_mask_o(),
    .cfg_physical_channels_o(),
    .cfg_sample_word_bits_o(),
    .cfg_trigger_mask_o(),
    .cfg_trigger_value_o(),
    .cfg_trigger_edge_rise_o(),
    .cfg_trigger_edge_fall_o(),
    .cfg_mode_o(),
    .cfg_trigger_timeout_samples_o(),
    .cfg_event_enable_mask_o(cfg_event_enable_mask_o),
    .cfg_event_limit_o(cfg_event_limit_o),
    .cfg_event_watchdog_ticks_o(cfg_event_watchdog_ticks_o),
    .cfg_event_hard_timeout_ticks_o(cfg_event_hard_timeout_ticks_o),
    .cfg_error_valid_i(1'b0),
    .cfg_error_code_i(32'd0),
    .cfg_error_detail0_i(32'd0),
    .cfg_error_detail1_i(32'd0),
    .arm_o(arm_o),
    .arm_accepted_i(arm_accepted_i),
    .event_stream_start_o(event_stream_start_o),
    .stop_o(stop_o),
    .capture_frame_done_i(capture_frame_done_i),
    .capture_samples_captured_i(32'd0),
    .capture_samples_uploaded_i(32'd0),
    .capture_device_status_flags_i(32'd0),
    .capture_debug_flags_i(32'd0),
    .capture_window_state_i(32'd1),
    .capture_pretrigger_samples_i(32'd0),
    .capture_posttrigger_samples_i(32'd0),
    .capture_frame_source_state_i(32'd0),
    .capture_tx_generator_state_i(32'd0),
    .capture_ft601_state_i(32'd0),
    .capture_tx_bytes_i(32'd0),
    .frame_valid_o(frame_valid_o),
    .frame_ready_i(frame_ready_i),
    .frame_type_o(frame_type_o),
    .frame_capture_id_o(frame_capture_id_o),
    .frame_flags_o(),
    .payload_word_count_o(payload_word_count_o),
    .payload0_o(payload0_o),
    .payload1_o(payload1_o),
    .payload2_o(payload2_o),
    .payload3_o(),
    .payload4_o(),
    .payload5_o(),
    .payload6_o(),
    .payload7_o(),
    .payload8_o(),
    .payload9_o(),
    .payload10_o(),
    .payload11_o(),
    .payload12_o(),
    .payload13_o(),
    .payload14_o(),
    .payload15_o(),
    .capture_id_o(),
    .device_state_o(device_state_o),
    .last_error_code_o(),
    .debug_state_o()
  );

  initial begin
    clk = 1'b0;
    forever #7.5 clk = !clk;
  end

  always @(posedge clk) begin
    if (arm_o) begin
      arm_pulse_count_r = arm_pulse_count_r + 1;
    end
    if (event_stream_start_o) begin
      event_start_pulse_count_r = event_start_pulse_count_r + 1;
    end
  end

  task fail_check;
    input [1023:0] message_i;
    begin
      failures_r = failures_r + 1;
      $display("FAIL time=%0t message=%0s state=%0d frame_valid=%0d frame_type=0x%04x payload0=%0d payload1=%0d payload2=%0d",
               $time, message_i, device_state_o, frame_valid_o, frame_type_o,
               payload0_o, payload1_o, payload2_o);
    end
  endtask

  task send_command;
    input [15:0] type_i;
    input [31:0] sequence_i;
    begin
      while (!cmd_ready_o) @(posedge clk);
      cmd_type_i = type_i;
      cmd_seq_i = sequence_i;
      cmd_valid_i = 1'b1;
      @(posedge clk);
      #1;
      cmd_valid_i = 1'b0;
    end
  endtask

  initial begin
    rst_n = 1'b0;
    cmd_valid_i = 1'b0;
    cmd_type_i = 16'd0;
    cmd_seq_i = 32'd0;
    cmd_payload0_i = 32'd120000000;
    cmd_payload1_i = 32'd4096;
    cmd_payload2_i = 32'd2047;
    cmd_payload3_i = 32'd2049;
    arm_accepted_i = 1'b0;
    frame_ready_i = 1'b0;
    capture_frame_done_i = 1'b0;
    failures_r = 0;
    arm_pulse_count_r = 0;
    event_start_pulse_count_r = 0;

    repeat (4) @(posedge clk);
    rst_n = 1'b1;
    repeat (2) @(posedge clk);

    send_command(16'h0002, 32'd1);
    while (!frame_valid_o) @(posedge clk);
    #1;
    if (frame_type_o !== 16'h8005 || payload_word_count_o !== 32'd16 ||
        payload0_o !== 32'd2 || payload1_o !== 32'd1) begin
      fail_check("CONFIG_CAPTURE did not return CONFIGURED STATUS_RSP");
    end
    frame_ready_i = 1'b1;
    @(posedge clk);
    #1;
    frame_ready_i = 1'b0;

    cmd_payload0_i = 32'h19f;
    cmd_payload1_i = 32'd0;
    cmd_payload2_i = 32'd12000000;
    cmd_payload3_i = 32'd240000000;
    send_command(16'h0006, 32'd2);
    while (!frame_valid_o) @(posedge clk);
    #1;
    if (frame_type_o !== 16'h8005 || payload0_o !== 32'd2 || payload1_o !== 32'd2 ||
        cfg_event_enable_mask_o !== 32'h19f ||
        cfg_event_limit_o !== 32'd0 ||
        cfg_event_watchdog_ticks_o !== 32'd12000000 ||
        cfg_event_hard_timeout_ticks_o !== 32'd240000000) begin
      fail_check("CONFIG_EVENTS did not latch v3 event configuration");
    end
    frame_ready_i = 1'b1;
    @(posedge clk);
    #1;
    frame_ready_i = 1'b0;

    send_command(16'h0003, 32'd4);
    repeat (5) begin
      @(posedge clk);
      #1;
      if (frame_valid_o) fail_check("ARM responded before sample-domain acceptance");
    end
    if (arm_pulse_count_r != 1) fail_check("ARM request pulse count mismatch");

    arm_accepted_i = 1'b1;
    @(posedge clk);
    #1;
    arm_accepted_i = 1'b0;
    while (!frame_valid_o) @(posedge clk);
    #1;
    if (frame_type_o !== 16'h8005 || payload_word_count_o !== 32'd16 ||
        payload0_o !== 32'd4 || payload1_o !== 32'd4 || payload2_o !== 32'd1 ||
        frame_capture_id_o !== 32'd1) begin
      fail_check("ARM acceptance STATUS_RSP contract mismatch");
    end
    frame_ready_i = 1'b1;
    @(posedge clk);
    #1;
    frame_ready_i = 1'b0;

    send_command(16'h0007, 32'd5);
    repeat (3) begin
      @(posedge clk);
      #2;
      if (frame_valid_o) fail_check("START_EVENT_STREAM must not generate a response frame");
    end
    if (event_start_pulse_count_r != 1) begin
      fail_check("START_EVENT_STREAM pulse count mismatch");
    end

    send_command(16'h0005, 32'd6);
    while (!frame_valid_o) @(posedge clk);
    #1;
    if (frame_type_o !== 16'h8005 || payload_word_count_o !== 32'd16 ||
        payload0_o !== 32'd4 || payload1_o !== 32'd6) begin
      fail_check("GET_STATUS must be accepted while capture is active");
    end
    frame_ready_i = 1'b1;
    @(posedge clk);
    #1;
    frame_ready_i = 1'b0;

    send_command(16'h0004, 32'd7);
    #1;
    if (!stop_o) fail_check("STOP_CAPTURE pulse was not generated");
    capture_frame_done_i = 1'b1;
    @(posedge clk);
    #2;
    capture_frame_done_i = 1'b0;
    if (device_state_o !== 32'd2) fail_check("STOP completion must return CONFIGURED");

    if (failures_r == 0) begin
      $display("PASS tb_lockstep_command_responses");
    end else begin
      $display("FAIL tb_lockstep_command_responses failures=%0d", failures_r);
    end
    $finish;
  end
endmodule
