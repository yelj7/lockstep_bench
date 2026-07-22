/**********************************************************
* 文件名: tb_lockstep_sparse_change_window_cdc.v
* 日期: 2026-07-22
* 版本: 1.0
* 更新记录: 初版，覆盖窗口边界、满环、回绕、连续 ARM、STOP 和记录字段。
* 描述: 验证 16-bit sparse change ring 的采样域记录、冻结快照和读域顺序。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_sparse_change_window_cdc;
  reg sample_clk;
  reg sample_rst_n;
  reg arm_pulse;
  reg stop_pulse;
  reg trigger_pulse;
  reg capture_done_pulse;
  reg sample_valid;
  reg [31:0] sample_abs_index;
  reg [63:0] sample_timestamp;
  reg [15:0] state;
  reg read_clk;
  reg read_rst_n;
  reg read_req;
  reg [11:0] read_index;
  wire meta_valid;
  wire [12:0] ring_record_count;
  wire [31:0] observed_count;
  wire [15:0] window_initial_state;
  wire [15:0] final_state;
  wire [63:0] window_origin_tick;
  wire [63:0] trigger_tick;
  wire [63:0] window_end_exclusive_tick;
  wire read_ready;
  wire read_valid;
  wire [127:0] read_record;

  integer sample_number;
  integer timeout_count;
  reg [127:0] checked_record;

  lockstep_sparse_change_window_cdc dut (
    .sample_clk                 (sample_clk),
    .sample_rst_n               (sample_rst_n),
    .arm_pulse_i                (arm_pulse),
    .stop_pulse_i               (stop_pulse),
    .trigger_pulse_i            (trigger_pulse),
    .capture_done_pulse_i       (capture_done_pulse),
    .sample_valid_i             (sample_valid),
    .sample_abs_index_i         (sample_abs_index),
    .sample_timestamp_i         (sample_timestamp),
    .state_i                    (state),
    .read_clk                   (read_clk),
    .read_rst_n                 (read_rst_n),
    .meta_valid_o               (meta_valid),
    .ring_record_count_o        (ring_record_count),
    .observed_count_o           (observed_count),
    .window_initial_state_o     (window_initial_state),
    .final_state_o              (final_state),
    .window_origin_tick_o       (window_origin_tick),
    .trigger_tick_o             (trigger_tick),
    .window_end_exclusive_tick_o(window_end_exclusive_tick),
    .read_req_i                 (read_req),
    .read_index_i               (read_index),
    .read_ready_o               (read_ready),
    .read_valid_o               (read_valid),
    .read_record_o              (read_record)
  );

  always #5 sample_clk = !sample_clk;
  always #7 read_clk = !read_clk;

  task fail;
    input [1023:0] message;
    begin
      $display("FAIL time=%0t message=%0s count=%0d observed=%0d", $time,
               message, ring_record_count, observed_count);
      $finish;
    end
  endtask

  task arm_capture;
    input [15:0] initial_state;
    begin
      @(negedge sample_clk);
      state = initial_state;
      arm_pulse = 1'b1;
      @(posedge sample_clk);
      #2;
      arm_pulse = 1'b0;
      repeat (5) @(posedge read_clk);
      #2;
      if (meta_valid) fail("ARM 后旧 metadata 未失效");
    end
  endtask

  task drive_sample;
    input [31:0] index_value;
    input [63:0] tick_value;
    input [15:0] state_value;
    input trigger_value;
    input done_value;
    begin
      @(negedge sample_clk);
      sample_abs_index = index_value;
      sample_timestamp = tick_value;
      state = state_value;
      trigger_pulse = trigger_value;
      capture_done_pulse = done_value;
      sample_valid = 1'b1;
      @(posedge sample_clk);
      #2;
      sample_valid = 1'b0;
      trigger_pulse = 1'b0;
      capture_done_pulse = 1'b0;
    end
  endtask

  task wait_for_meta;
    begin
      timeout_count = 0;
      while (!meta_valid && timeout_count < 20000) begin
        @(posedge read_clk);
        #2;
        timeout_count = timeout_count + 1;
      end
      if (!meta_valid) fail("等待冻结 metadata 超时");
    end
  endtask

  task fetch_record;
    input [11:0] logical_index;
    begin
      @(negedge read_clk);
      read_index = logical_index;
      read_req = 1'b1;
      #1;
      if (!read_ready) fail("合法记录索引未 ready");
      @(posedge read_clk);
      #2;
      read_req = 1'b0;
      if (!read_valid) fail("读请求未返回 valid");
      checked_record = read_record;
    end
  endtask

  task check_record;
    input [31:0] expected_index;
    input [31:0] expected_sequence;
    input [15:0] expected_state;
    input [7:0] expected_source;
    input [15:0] expected_change;
    begin
      if (checked_record[31:0] != expected_index ||
          checked_record[63:32] != expected_sequence ||
          checked_record[79:64] != expected_state ||
          checked_record[83:80] != 4'd0 ||
          checked_record[91:84] != expected_source ||
          checked_record[95:92] != 4'd0 ||
          checked_record[111:96] != expected_change ||
          checked_record[127:112] != 16'd0) begin
        $display("FAIL time=%0t record=%032h expected index=%08h seq=%0d state=%04h source=%02h change=%04h",
                 $time, checked_record, expected_index, expected_sequence,
                 expected_state, expected_source, expected_change);
        $finish;
      end
    end
  endtask

  initial begin
    sample_clk = 1'b0;
    sample_rst_n = 1'b0;
    arm_pulse = 1'b0;
    stop_pulse = 1'b0;
    trigger_pulse = 1'b0;
    capture_done_pulse = 1'b0;
    sample_valid = 1'b0;
    sample_abs_index = 32'd0;
    sample_timestamp = 64'd0;
    state = 16'd0;
    read_clk = 1'b0;
    read_rst_n = 1'b0;
    read_req = 1'b0;
    read_index = 12'd0;
    sample_number = 0;
    timeout_count = 0;
    checked_record = 128'd0;

    #31;
    sample_rst_n = 1'b1;
    read_rst_n = 1'b1;

    // 无变化窗口仍发布完整时间轴，但记录数和观察数均为零。
    arm_capture(16'h55aa);
    drive_sample(32'd10000, 64'd50000, 16'h55aa, 1'b1, 1'b0);
    drive_sample(32'd12048, 64'd52048, 16'h55aa, 1'b0, 1'b1);
    wait_for_meta;
    if (ring_record_count != 13'd0 || observed_count != 32'd0 ||
        window_initial_state != 16'h55aa || final_state != 16'h55aa ||
        window_origin_tick != 64'd47953 ||
        trigger_tick != 64'd50000 || window_end_exclusive_tick != 64'd52049) begin
      fail("无变化窗口 metadata 错误");
    end

    // 窗口 start 前旧变化必须剔除，start/trigger/end 三边界均保留。
    arm_capture(16'h0000);
    drive_sample(32'd1999, 64'd6999, 16'h0001, 1'b0, 1'b0);
    drive_sample(32'd2000, 64'd7000, 16'hffff, 1'b0, 1'b0);
    drive_sample(32'd4047, 64'd9047, 16'hfff0, 1'b1, 1'b0);
    drive_sample(32'd4048, 64'd9048, 16'hfcf0, 1'b0, 1'b0);
    drive_sample(32'd4049, 64'd9049, 16'hf0f0, 1'b0, 1'b0);
    drive_sample(32'd6095, 64'd11095, 16'h000f, 1'b0, 1'b1);
    wait_for_meta;
    if (ring_record_count != 13'd5 || observed_count != 32'd6 ||
        window_initial_state != 16'h0001 || final_state != 16'h000f ||
        window_origin_tick != 64'd7000 ||
        trigger_tick != 64'd9047 || window_end_exclusive_tick != 64'd11096) begin
      fail("窗口边界 metadata 或 capture_done 同拍变化错误");
    end
    fetch_record(12'd0);
    check_record(32'd2000, 32'd1, 16'hffff, 8'h9e, 16'hfffe);
    fetch_record(12'd1);
    check_record(32'd4047, 32'd2, 16'hfff0, 8'h02, 16'h000f);
    fetch_record(12'd2);
    check_record(32'd4048, 32'd3, 16'hfcf0, 8'h08, 16'h0300);
    fetch_record(12'd3);
    check_record(32'd4049, 32'd4, 16'hf0f0, 8'h10, 16'h0c00);
    fetch_record(12'd4);
    check_record(32'd6095, 32'd5, 16'h000f, 8'h86, 16'hf0ff);

    // 最坏每拍变化恰好填满 4096 条，顺序和 sequence 不得回退。
    arm_capture(16'h0000);
    for (sample_number = 0; sample_number < 4096; sample_number = sample_number + 1) begin
      drive_sample(32'd100000 + sample_number,
                   64'd200000 + sample_number,
                   sample_number[0] ? 16'h0000 : 16'hffff,
                   sample_number == 2047,
                   sample_number == 4095);
    end
    wait_for_meta;
    if (ring_record_count != 13'd4096 || observed_count != 32'd4096 ||
        final_state != 16'h0000) begin
      fail("满环每拍变化统计错误");
    end
    fetch_record(12'd0);
    check_record(32'd100000, 32'd0, 16'hffff, 8'h9e, 16'hffff);
    fetch_record(12'd4095);
    check_record(32'd104095, 32'd4095, 16'h0000, 8'h9e, 16'hffff);

    // 32-bit index 在 trigger 后自然回绕，窗口内顺序仍按环中先后保持。
    arm_capture(16'h0000);
    drive_sample(32'hfffff7ff, 64'd300000, 16'h0001, 1'b0, 1'b0);
    drive_sample(32'hfffff800, 64'd300001, 16'h0011, 1'b0, 1'b0);
    drive_sample(32'hffffffff, 64'd302048, 16'h0111, 1'b1, 1'b0);
    drive_sample(32'h000007ff, 64'd304096, 16'h1111, 1'b0, 1'b1);
    wait_for_meta;
    if (ring_record_count != 13'd3 || observed_count != 32'd4) begin
      fail("32-bit 回绕窗口过滤错误");
    end
    fetch_record(12'd0);
    check_record(32'hfffff800, 32'd1, 16'h0011, 8'h04, 16'h0010);
    fetch_record(12'd2);
    check_record(32'h000007ff, 32'd3, 16'h1111, 8'h80, 16'h1000);

    // 连续 ARM 必须清零 sequence；STOP 必须丢弃采集且不发布 metadata。
    arm_capture(16'h0000);
    drive_sample(32'd5000, 64'd400000, 16'h0002, 1'b1, 1'b0);
    drive_sample(32'd7048, 64'd402048, 16'h0003, 1'b0, 1'b1);
    wait_for_meta;
    if (ring_record_count != 13'd2 || observed_count != 32'd2) begin
      fail("连续 ARM 第二次采集计数错误");
    end
    fetch_record(12'd0);
    check_record(32'd5000, 32'd0, 16'h0002, 8'h02, 16'h0002);

    arm_capture(16'h0000);
    drive_sample(32'd9000, 64'd500000, 16'h0001, 1'b0, 1'b0);
    @(negedge sample_clk);
    stop_pulse = 1'b1;
    @(posedge sample_clk);
    #2;
    stop_pulse = 1'b0;
    repeat (8) @(posedge read_clk);
    #2;
    if (meta_valid) fail("STOP 后仍发布 metadata");

    $display("PASS tb_lockstep_sparse_change_window_cdc");
    $finish;
  end

endmodule
