/**********************************************************
* 文件名: tb_lockstep_rx_command_parser_recovery.v
* 日期: 2026-07-22
* 版本: 1.0
* 更新记录: 新增截断帧超时、魔数重同步、字节使能和复位恢复回归。
* 描述: 验证命令解析器在异常输入后可恢复接收完整有效命令。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_rx_command_parser_recovery;
  reg         clk;
  reg         rst_n;
  reg         rx_word_valid;
  wire        rx_word_ready;
  reg  [31:0] rx_word_data;
  reg  [3:0]  rx_be_valid;
  wire        cmd_valid;
  reg         cmd_ready;
  wire [15:0] cmd_type;
  wire [31:0] cmd_seq;
  wire [31:0] cmd_payload_words;
  wire        error_valid;
  reg         error_ready;
  wire [31:0] error_code;
  wire [15:0] error_type;
  wire [31:0] error_seq;
  wire [31:0] error_detail0;
  wire [31:0] error_detail1;
  wire [31:0] debug_state;

  reg [31:0] frame_words_r [0:11];
  integer failures_r;
  integer mask_r;

  localparam [31:0] MAGIC = 32'h3243534c;
  localparam [31:0] ERR_BAD_MAGIC = 32'd1;
  localparam [31:0] ERR_BAD_BYTE_ENABLE = 32'd15;
  localparam [31:0] ERR_RX_TIMEOUT = 32'd16;

  lockstep_rx_command_parser #(
    .RX_GAP_TIMEOUT_CYCLES(4)
  ) dut (
    .clk(clk),
    .rst_n(rst_n),
    .rx_word_valid_i(rx_word_valid),
    .rx_word_ready_o(rx_word_ready),
    .rx_word_data_i(rx_word_data),
    .rx_be_valid_i(rx_be_valid),
    .cmd_valid_o(cmd_valid),
    .cmd_ready_i(cmd_ready),
    .cmd_type_o(cmd_type),
    .cmd_seq_o(cmd_seq),
    .cmd_capture_id_o(),
    .cmd_payload_words_o(cmd_payload_words),
    .cmd_payload0_o(),
    .cmd_payload1_o(),
    .cmd_payload2_o(),
    .cmd_payload3_o(),
    .cmd_payload4_o(),
    .cmd_payload5_o(),
    .cmd_payload6_o(),
    .cmd_payload7_o(),
    .cmd_payload8_o(),
    .cmd_payload9_o(),
    .cmd_payload10_o(),
    .cmd_payload11_o(),
    .cmd_payload12_o(),
    .error_valid_o(error_valid),
    .error_ready_i(error_ready),
    .error_code_o(error_code),
    .error_type_o(error_type),
    .error_seq_o(error_seq),
    .error_detail0_o(error_detail0),
    .error_detail1_o(error_detail1),
    .debug_state_o(debug_state)
  );

  always #5 clk = !clk;

  initial begin
    #100000;
    $display("FAIL tb_lockstep_rx_command_parser_recovery simulation timeout");
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
      $display("FAIL time=%0t message=%0s parser_state=%0d cmd_valid=%0d error_valid=%0d error_code=%0d detail0=%0d detail1=%0d",
               $time, message_i, debug_state, cmd_valid, error_valid,
               error_code, error_detail0, error_detail1);
    end
  endtask

  task build_empty_frame;
    input [15:0] type_i;
    input [31:0] sequence_i;
    reg [31:0] crc_v;
    integer word_index_v;
    begin
      frame_words_r[0] = MAGIC;
      frame_words_r[1] = {type_i, 16'd2};
      frame_words_r[2] = 32'd32;
      frame_words_r[3] = 32'd0;
      frame_words_r[4] = sequence_i;
      frame_words_r[5] = 32'd0;
      frame_words_r[6] = 32'd0;
      frame_words_r[7] = 32'd0;
      crc_v = 32'hffffffff;
      for (word_index_v = 0; word_index_v < 8; word_index_v = word_index_v + 1)
        crc_v = crc32_word(crc_v, frame_words_r[word_index_v]);
      frame_words_r[7] = crc_v ^ 32'hffffffff;
    end
  endtask

  task build_event_config_frame;
    input [31:0] sequence_i;
    reg [31:0] crc_v;
    integer word_index_v;
    begin
      frame_words_r[0] = MAGIC;
      frame_words_r[1] = {16'h0006, 16'd3};
      frame_words_r[2] = 32'd32;
      frame_words_r[3] = 32'd16;
      frame_words_r[4] = sequence_i;
      frame_words_r[5] = 32'd0;
      frame_words_r[6] = 32'd0;
      frame_words_r[7] = 32'd0;
      frame_words_r[8] = 32'h0000019f;
      frame_words_r[9] = 32'd0;
      frame_words_r[10] = 32'd12000000;
      frame_words_r[11] = 32'd240000000;
      crc_v = 32'hffffffff;
      for (word_index_v = 0; word_index_v < 12; word_index_v = word_index_v + 1)
        crc_v = crc32_word(crc_v, frame_words_r[word_index_v]);
      frame_words_r[7] = crc_v ^ 32'hffffffff;
    end
  endtask

  task send_word;
    input [31:0] data_i;
    input [3:0] be_i;
    begin
      while (!rx_word_ready) @(posedge clk);
      rx_word_data = data_i;
      rx_be_valid = be_i;
      rx_word_valid = 1'b1;
      @(posedge clk);
      #2;
      rx_word_valid = 1'b0;
    end
  endtask

  task send_frame;
    input integer word_count_i;
    integer word_index_v;
    begin
      for (word_index_v = 0; word_index_v < word_count_i; word_index_v = word_index_v + 1)
        send_word(frame_words_r[word_index_v], 4'hf);
    end
  endtask

  task accept_error;
    begin
      error_ready = 1'b1;
      @(posedge clk);
      #2;
      error_ready = 1'b0;
      @(posedge clk);
      #2;
    end
  endtask

  task accept_command;
    begin
      cmd_ready = 1'b1;
      @(posedge clk);
      #2;
      cmd_ready = 1'b0;
      @(posedge clk);
      #2;
    end
  endtask

  task expect_empty_command;
    input [15:0] type_i;
    input [31:0] sequence_i;
    begin
      build_empty_frame(type_i, sequence_i);
      send_frame(8);
      if (!cmd_valid || error_valid || cmd_type != type_i ||
          cmd_seq != sequence_i || cmd_payload_words != 32'd0)
        fail_check("valid empty command did not recover");
      accept_command();
    end
  endtask

  task reset_parser;
    begin
      rx_word_valid = 1'b0;
      cmd_ready = 1'b0;
      error_ready = 1'b0;
      rst_n = 1'b0;
      #2;
      if (cmd_valid || error_valid)
        fail_check("reset did not clear output valid signals asynchronously");
      repeat (2) @(posedge clk);
      rst_n = 1'b1;
      repeat (2) @(posedge clk);
      #2;
      if (!rx_word_ready || debug_state != 32'd0 || cmd_valid || error_valid)
        fail_check("reset did not restore header synchronization state");
    end
  endtask

  initial begin
    clk = 1'b0;
    rst_n = 1'b0;
    rx_word_valid = 1'b0;
    rx_word_data = 32'd0;
    rx_be_valid = 4'hf;
    cmd_ready = 1'b0;
    error_ready = 1'b0;
    failures_r = 0;

    reset_parser();

    // 三个头字后等待：前三个空闲周期不得超时，第四个周期必须报错。
    build_empty_frame(16'h0005, 32'h101);
    send_word(frame_words_r[0], 4'hf);
    send_word(frame_words_r[1], 4'hf);
    send_word(frame_words_r[2], 4'hf);
    repeat (3) begin
      @(posedge clk);
      #2;
      if (error_valid) fail_check("header timeout fired before configured gap");
    end
    @(posedge clk);
    #2;
    if (!error_valid || error_code != ERR_RX_TIMEOUT ||
        error_detail0 != 32'd3 || error_detail1 != 32'd8 ||
        error_type != 16'h0005 || error_seq != 32'd0)
      fail_check("truncated header timeout metadata mismatch");
    repeat (2) begin
      @(posedge clk);
      #2;
      if (!error_valid || rx_word_ready || error_code != ERR_RX_TIMEOUT ||
          error_detail0 != 32'd3 || error_detail1 != 32'd8)
        fail_check("timeout error did not remain stable under backpressure");
    end
    accept_error();
    expect_empty_command(16'h0005, 32'h102);

    // payload 收到一个字后超时，详情记录已收 1 字、期望 4 字。
    build_event_config_frame(32'h201);
    send_frame(9);
    repeat (4) @(posedge clk);
    #2;
    if (!error_valid || error_code != ERR_RX_TIMEOUT ||
        error_detail0 != 32'd1 || error_detail1 != 32'd4 ||
        error_type != 16'h0006 || error_seq != 32'h201)
      fail_check("truncated payload timeout metadata mismatch");
    accept_error();
    expect_empty_command(16'h0005, 32'h202);

    // 错误魔数必须在第一个字立即报告，并背压保存紧随其后的有效魔数。
    send_word(32'hdeadbeef, 4'hf);
    if (!error_valid || error_code != ERR_BAD_MAGIC ||
        error_type != 16'd0 || error_seq != 32'd0 ||
        error_detail0 != 32'hdeadbeef || error_detail1 != MAGIC)
      fail_check("bad magic was not rejected immediately with stable metadata");
    build_empty_frame(16'h0005, 32'h301);
    rx_word_data = frame_words_r[0];
    rx_be_valid = 4'hf;
    rx_word_valid = 1'b1;
    repeat (2) begin
      @(posedge clk);
      #2;
      if (rx_word_ready) fail_check("parser accepted next magic while error was backpressured");
    end
    error_ready = 1'b1;
    @(posedge clk);
    #2;
    error_ready = 1'b0;
    @(posedge clk);
    #2;
    rx_word_valid = 1'b0;
    send_word(frame_words_r[1], 4'hf);
    send_word(frame_words_r[2], 4'hf);
    send_word(frame_words_r[3], 4'hf);
    send_word(frame_words_r[4], 4'hf);
    send_word(frame_words_r[5], 4'hf);
    send_word(frame_words_r[6], 4'hf);
    send_word(frame_words_r[7], 4'hf);
    if (!cmd_valid || error_valid || cmd_seq != 32'h301)
      fail_check("valid command following bad magic was not recovered");
    accept_command();

    // 所有非全字使能都必须拒绝，且不得泄漏上一帧 type/seq。
    for (mask_r = 0; mask_r < 15; mask_r = mask_r + 1) begin
      send_word(MAGIC, mask_r[3:0]);
      if (!error_valid || error_code != ERR_BAD_BYTE_ENABLE ||
          error_detail0 != mask_r || error_type != 16'd0 || error_seq != 32'd0)
        fail_check("invalid byte-enable mask metadata mismatch");
      accept_error();
    end

    build_empty_frame(16'h0005, 32'h400);
    send_word(frame_words_r[0], 4'hf);
    send_word(32'hffff0002, 4'h3);
    if (!error_valid || error_code != ERR_BAD_BYTE_ENABLE ||
        error_type != 16'd0 || error_seq != 32'd0)
      fail_check("partial type word was exposed as trusted metadata");
    accept_error();

    build_empty_frame(16'h0005, 32'h400);
    send_word(frame_words_r[0], 4'hf);
    send_word(frame_words_r[1], 4'hf);
    send_word(frame_words_r[2], 4'hf);
    send_word(frame_words_r[3], 4'hf);
    send_word(32'hffffffff, 4'he);
    if (!error_valid || error_code != ERR_BAD_BYTE_ENABLE ||
        error_type != 16'h0005 || error_seq != 32'd0)
      fail_check("partial sequence word was exposed as trusted metadata");
    accept_error();

    expect_empty_command(16'h0005, 32'h401);

    // payload 内的非全字使能保留已确认的 type/seq，并整帧丢弃后恢复。
    build_event_config_frame(32'h402);
    send_frame(8);
    send_word(frame_words_r[8], 4'h7);
    if (!error_valid || error_code != ERR_BAD_BYTE_ENABLE ||
        error_detail0 != 32'd7 || error_type != 16'h0006 || error_seq != 32'h402)
      fail_check("payload byte-enable error metadata mismatch");
    repeat (2) begin
      @(posedge clk);
      #2;
      if (!error_valid || rx_word_ready || error_code != ERR_BAD_BYTE_ENABLE ||
          error_detail0 != 32'd7 || error_type != 16'h0006 || error_seq != 32'h402)
        fail_check("byte-enable error did not remain stable under backpressure");
    end
    accept_error();
    expect_empty_command(16'h0005, 32'h403);

    // 每个字间隔 timeout-1 周期仍应完成，不得发生边界误报。
    build_empty_frame(16'h0005, 32'h501);
    for (mask_r = 0; mask_r < 8; mask_r = mask_r + 1) begin
      send_word(frame_words_r[mask_r], 4'hf);
      if (mask_r != 7) begin
        repeat (3) @(posedge clk);
        #2;
        if (error_valid) fail_check("near-timeout valid stream was rejected");
      end
    end
    if (!cmd_valid || cmd_seq != 32'h501) fail_check("near-timeout frame did not complete");
    accept_command();

    // 头、payload、命令保持和错误保持状态中的复位都必须恢复到帧头同步。
    build_empty_frame(16'h0005, 32'h601);
    send_word(frame_words_r[0], 4'hf);
    send_word(frame_words_r[1], 4'hf);
    reset_parser();
    expect_empty_command(16'h0005, 32'h602);

    build_event_config_frame(32'h603);
    send_frame(9);
    reset_parser();
    expect_empty_command(16'h0005, 32'h604);

    build_empty_frame(16'h0005, 32'h605);
    send_frame(8);
    if (!cmd_valid) fail_check("command hold setup failed");
    reset_parser();
    expect_empty_command(16'h0005, 32'h606);

    send_word(32'hbad0bad0, 4'hf);
    if (!error_valid) fail_check("error hold setup failed");
    reset_parser();
    expect_empty_command(16'h0005, 32'h607);

    if (failures_r == 0)
      $display("PASS tb_lockstep_rx_command_parser_recovery");
    else
      $display("FAIL tb_lockstep_rx_command_parser_recovery failures=%0d", failures_r);
    $finish;
  end
endmodule
