/**********************************************************
* 文件名: tb_lockstep_event_frame_source.v
* 日期: 2026-07-19
* 版本: 1.1
* 更新记录: 固定主结束原因与 overflow 独立上报语义。
* 描述: 验证 META/DATA/END 顺序、反压、记录映射和结束统计。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_event_frame_source;
  reg clk;
  reg rst_n;
  reg start;
  reg collection_done;
  reg [8:0] overflow_mask;
  reg [9*32-1:0] dropped_count;
  reg event_valid;
  reg [511:0] event_record;
  reg frame_ready;
  wire event_ready;
  wire frame_valid;
  wire [15:0] frame_type;
  wire [31:0] payload_word_count;
  wire [31:0] payload0;
  wire [31:0] payload1;
  wire [31:0] payload2;
  wire [31:0] payload3;
  wire [31:0] payload4;
  wire [31:0] payload5;
  wire [31:0] payload14;
  wire [31:0] payload15;
  wire done;

  lockstep_event_frame_source dut (
    .clk(clk), .rst_n(rst_n), .start_i(start), .capture_id_i(32'h42),
    .sample_rate_hz_i(32'd120000000), .implemented_source_mask_i(9'h19f),
    .enabled_source_mask_i(9'h183), .design_gap_mask_i(9'h060),
    .watchdog_ticks_i(32'd100), .hard_timeout_ticks_i(32'd1000),
    .collection_done_i(collection_done), .collection_end_reason_i(32'd2),
    .overflow_mask_i(overflow_mask), .dropped_count_i(dropped_count),
    .event_valid_i(event_valid), .event_ready_o(event_ready), .event_record_i(event_record),
    .frame_valid_o(frame_valid), .frame_ready_i(frame_ready), .frame_type_o(frame_type),
    .frame_capture_id_o(), .frame_flags_o(), .payload_word_count_o(payload_word_count),
    .payload0_o(payload0), .payload1_o(payload1), .payload2_o(payload2),
    .payload3_o(payload3), .payload4_o(payload4), .payload5_o(payload5),
    .payload6_o(), .payload7_o(), .payload8_o(), .payload9_o(),
    .payload10_o(), .payload11_o(), .payload12_o(), .payload13_o(),
    .payload14_o(payload14), .payload15_o(payload15), .done_o(done), .state_o()
  );

  always #5 clk = !clk;

  task fail;
    input [1023:0] message;
    begin
      $display("FAIL time=%0t message=%0s", $time, message);
      $finish;
    end
  endtask

  task accept_frame;
    input [15:0] expected_type;
    begin
      while (!frame_valid) @(negedge clk);
      if (frame_type != expected_type || payload_word_count != 32'd16) fail("帧类型或长度错误");
      frame_ready = 1'b1;
      @(posedge clk);
      #2;
      frame_ready = 1'b0;
    end
  endtask

  initial begin
    clk = 1'b0;
    rst_n = 1'b0;
    start = 1'b0;
    collection_done = 1'b0;
    overflow_mask = 9'h002;
    dropped_count = {9*32{1'b0}};
    dropped_count[2*32-1:1*32] = 32'd1;
    event_valid = 1'b0;
    event_record = 512'd0;
    frame_ready = 1'b0;
    repeat (3) @(posedge clk);
    rst_n = 1'b1;
    start = 1'b1;
    @(posedge clk);
    #2;
    start = 1'b0;

    while (!frame_valid) @(negedge clk);
    if (frame_type != 16'h8103 || payload0 != 32'd120000000 ||
        payload1 != 32'h19f || payload2 != 32'h183 || payload3 != 32'h060 || payload4 != 32'd64) begin
      fail("EVENT_META 字段错误");
    end
    accept_frame(16'h8103);

    event_record = 512'h11;
    event_valid = 1'b1;
    while (frame_type != 16'h8104) @(negedge clk);
    repeat (3) begin
      @(negedge clk);
      if (!frame_valid || payload0 != 32'h11 || event_ready) fail("EVENT_DATA 反压不稳定");
    end
    accept_frame(16'h8104);
    event_valid = 1'b0;

    @(posedge clk);
    event_record = 512'h22;
    event_valid = 1'b1;
    accept_frame(16'h8104);
    event_valid = 1'b0;
    collection_done = 1'b1;

    while (!frame_valid || frame_type != 16'h8105) @(negedge clk);
    if (payload0 != 32'd2 || payload1 != 32'h2 || payload2 != 32'd3 ||
        payload3 != 32'd2 || payload4 != 32'd1 || payload5 != 32'd0 ||
        payload14 != 32'h183 || payload15 != 32'h19f) begin
      $display("EVENT_END actual=%0d,%0h,%0d,%0d,%0d,%0d,%0h,%0h",
               payload0, payload1, payload2, payload3, payload4, payload5, payload14, payload15);
      fail("EVENT_END 统计或 overflow 语义错误");
    end
    accept_frame(16'h8105);
    collection_done = 1'b0;
    if (!done) fail("事件帧源未完成");

    $display("PASS tb_lockstep_event_frame_source");
    $finish;
  end

endmodule
