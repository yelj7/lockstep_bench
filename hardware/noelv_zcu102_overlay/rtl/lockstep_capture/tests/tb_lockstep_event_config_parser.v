/**********************************************************
* 文件名: tb_lockstep_event_config_parser.v
* 日期: 2026-07-19
* 版本: 1.1
* 更新记录: 增加零负载 START_EVENT_STREAM v3 帧编解码回归。
* 描述: 验证事件配置和释放命令的版本、长度、CRC 与命令接口。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_event_config_parser;
  reg clk;
  reg rst_n;
  reg frame_valid;
  reg [15:0] frame_type;
  reg [31:0] payload_word_count;
  wire frame_ready;
  wire tx_valid;
  wire tx_ready;
  wire [31:0] tx_data;
  wire cmd_valid;
  reg cmd_ready;
  wire [15:0] cmd_type;
  wire [31:0] cmd_payload_words;
  wire [31:0] cmd_payload0;
  wire [31:0] cmd_payload1;
  wire [31:0] cmd_payload2;
  wire [31:0] cmd_payload3;
  wire error_valid;

  lockstep_tx_frame_generator u_generator (
    .clk(clk), .rst_n(rst_n), .frame_valid_i(frame_valid), .frame_ready_o(frame_ready),
    .frame_type_i(frame_type), .frame_capture_id_i(32'd0), .frame_flags_i(32'd0),
    .payload_word_count_i(payload_word_count), .payload0_i(32'h19f), .payload1_i(32'd0),
    .payload2_i(32'd12000000), .payload3_i(32'd240000000),
    .payload4_i(32'd0), .payload5_i(32'd0), .payload6_i(32'd0), .payload7_i(32'd0),
    .payload8_i(32'd0), .payload9_i(32'd0), .payload10_i(32'd0), .payload11_i(32'd0),
    .payload12_i(32'd0), .payload13_i(32'd0), .payload14_i(32'd0), .payload15_i(32'd0),
    .tx_word_valid_o(tx_valid), .tx_word_ready_i(tx_ready), .tx_word_data_o(tx_data),
    .tx_frame_start_o(), .tx_frame_end_o(), .tx_frame_type_o(), .tx_frame_seq_o(), .debug_state_o()
  );

  lockstep_rx_command_parser u_parser (
    .clk(clk), .rst_n(rst_n), .rx_word_valid_i(tx_valid), .rx_word_ready_o(tx_ready),
    .rx_word_data_i(tx_data), .rx_be_valid_i(4'hf), .cmd_valid_o(cmd_valid),
    .cmd_ready_i(cmd_ready), .cmd_type_o(cmd_type), .cmd_seq_o(), .cmd_capture_id_o(),
    .cmd_payload_words_o(cmd_payload_words), .cmd_payload0_o(cmd_payload0),
    .cmd_payload1_o(cmd_payload1), .cmd_payload2_o(cmd_payload2), .cmd_payload3_o(cmd_payload3),
    .cmd_payload4_o(), .cmd_payload5_o(), .cmd_payload6_o(), .cmd_payload7_o(),
    .cmd_payload8_o(), .cmd_payload9_o(), .cmd_payload10_o(), .cmd_payload11_o(),
    .cmd_payload12_o(), .error_valid_o(error_valid), .error_ready_i(1'b1),
    .error_code_o(), .error_type_o(), .error_seq_o(), .error_detail0_o(),
    .error_detail1_o(), .debug_state_o()
  );

  always #5 clk = !clk;

  task send_frame;
    begin
      while (!frame_ready) @(posedge clk);
      frame_valid = 1'b1;
      @(posedge clk);
      #2;
      frame_valid = 1'b0;
      while (!cmd_valid && !error_valid) @(posedge clk);
      #2;
    end
  endtask

  initial begin
    clk = 1'b0;
    rst_n = 1'b0;
    frame_valid = 1'b0;
    frame_type = 16'h0006;
    payload_word_count = 32'd4;
    cmd_ready = 1'b0;
    repeat (4) @(posedge clk);
    rst_n = 1'b1;
    send_frame();
    if (error_valid || cmd_type != 16'h0006 || cmd_payload_words != 32'd4 ||
        cmd_payload0 != 32'h19f || cmd_payload1 != 32'd0 ||
        cmd_payload2 != 32'd12000000 || cmd_payload3 != 32'd240000000) begin
      $display("FAIL CONFIG_EVENTS v3 parser contract");
      $finish;
    end
    cmd_ready = 1'b1;
    @(posedge clk);
    #2;
    cmd_ready = 1'b0;

    frame_type = 16'h0007;
    payload_word_count = 32'd0;
    send_frame();
    if (error_valid || cmd_type != 16'h0007 || cmd_payload_words != 32'd0) begin
      $display("FAIL START_EVENT_STREAM v3 parser contract");
      $finish;
    end
    cmd_ready = 1'b1;
    @(posedge clk);
    $display("PASS tb_lockstep_event_config_parser");
    $finish;
  end

endmodule
