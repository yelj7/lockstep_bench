/**********************************************************
* 文件名: tb_lockstep_capture_absolute_index.v
* 日期: 2026-07-22
* 版本: 1.2
* 更新记录: 固定 2047 点预触发，并覆盖 32-bit 绝对索引自然回绕。
* 描述: 验证统一窗口始终使用外部全局采样索引和固定 trigger 位置。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_capture_absolute_index;
  reg sample_clk;
  reg read_clk;
  reg sample_rst_n;
  reg read_rst_n;
  reg arm_i;
  reg [31:0] trigger_addr_i;
  reg sample_valid_i;
  reg drive_samples_r;
  reg [1023:0] sample_i;
  reg [31:0] sample_abs_index_i;
  wire arm_ready_o;
  wire arm_accepted_o;
  wire busy_o;
  wire done_o;
  wire [31:0] meta_pretrigger_count_o;
  wire [31:0] meta_window_start_abs_index_o;
  wire [31:0] meta_trigger_abs_index_o;
  wire [31:0] meta_trigger_sample_index_o;
  integer accepted_count_r;
  integer failures_r;

  lockstep_wide_capture_window_cdc #(
    .PROBE_SAMPLE_BITS(1024),
    .MAX_PROBE_SAMPLE_BITS(1024),
    .LANE_BITS(128),
    .LANE_INDEX_BITS(4),
    .PROTOCOL_COUNT(9),
    .SAMPLE_ADDR_WIDTH(12),
    .AHB_TRIGGER_MODE(0)
  ) dut (
    .sample_clk(sample_clk),
    .sample_rst_n(sample_rst_n),
    .read_clk(read_clk),
    .read_rst_n(read_rst_n),
    .arm_i(arm_i),
    .stop_i(1'b0),
    .trigger_timeout_samples_i(32'hffffffff),
    .protocol_enable_i(9'h1ff),
    .trigger_addr_i(trigger_addr_i),
    .trigger_mismatch_enable_i(1'b0),
    .trigger_mismatch_mask_i(5'd0),
    .sample_valid_i(sample_valid_i),
    .sample_i(sample_i),
    .sample_abs_index_i(sample_abs_index_i),
    .arm_ready_o(arm_ready_o),
    .arm_accepted_o(arm_accepted_o),
    .busy_o(busy_o),
    .done_o(done_o),
    .done_pulse_o(),
    .upload_allowed_o(),
    .param_error_o(),
    .pretrigger_ready_o(),
    .meta_valid_o(),
    .meta_protocol_enable_o(),
    .meta_protocol_count_o(),
    .meta_sample_bits_o(),
    .meta_lane_count_o(),
    .meta_max_lane_count_o(),
    .meta_window_sample_count_o(),
    .meta_pretrigger_count_o(meta_pretrigger_count_o),
    .meta_trigger_count_o(),
    .meta_post_after_trigger_count_o(),
    .meta_window_start_abs_index_o(meta_window_start_abs_index_o),
    .meta_trigger_abs_index_o(meta_trigger_abs_index_o),
    .meta_trigger_sample_index_o(meta_trigger_sample_index_o),
    .samples_seen_o(),
    .post_after_trigger_seen_o(),
    .abort_pulse_o(),
    .watchdog_pulse_o(),
    .trigger_seen_o(),
    .watchdog_expired_o(),
    .read_req_i(1'b0),
    .read_sample_index_i(12'd0),
    .read_lane_index_i(4'd0),
    .read_ready_o(),
    .read_valid_o(),
    .read_error_o(),
    .read_data_o(),
    .read_sample_index_o(),
    .read_lane_index_o(),
    .read_last_lane_o(),
    .read_last_sample_o(),
    .state_o()
  );

  always #5 sample_clk = !sample_clk;
  always #7 read_clk = !read_clk;

  always @(posedge read_clk) begin
    if (arm_accepted_o) accepted_count_r = accepted_count_r + 1;
  end

  always @(negedge sample_clk) begin
    if (drive_samples_r) sample_abs_index_i = sample_abs_index_i + 32'd1;
  end

  task fail_check;
    input [1023:0] message_i;
    begin
      failures_r = failures_r + 1;
      $display("FAIL time=%0t message=%0s window_start=%08x trigger_abs=%08x trigger_local=%0d pre=%0d",
               $time, message_i, meta_window_start_abs_index_o,
               meta_trigger_abs_index_o, meta_trigger_sample_index_o,
               meta_pretrigger_count_o);
    end
  endtask

  task run_capture;
    input [31:0] base_index_i;
    input [31:0] trigger_offset_i;
    integer accepted_before_v;
    integer wait_count_v;
    reg [31:0] expected_trigger_v;
    begin
      expected_trigger_v = base_index_i + trigger_offset_i;
      trigger_addr_i = expected_trigger_v;
      accepted_before_v = accepted_count_r;
      while (!arm_ready_o) @(posedge read_clk);
      arm_i = 1'b1;
      @(posedge read_clk);
      #1;
      arm_i = 1'b0;
      wait_count_v = 0;
      while ((accepted_count_r == accepted_before_v) && (wait_count_v < 1000)) begin
        @(posedge read_clk);
        wait_count_v = wait_count_v + 1;
      end
      if (accepted_count_r != (accepted_before_v + 1)) begin
        fail_check("ARM 未被恰好接受一次");
      end
      wait_count_v = 0;
      while (!busy_o && (wait_count_v < 1000)) begin
        @(posedge read_clk);
        wait_count_v = wait_count_v + 1;
      end
      if (!busy_o) fail_check("采样域未进入采集状态");

      @(posedge sample_clk);
      #1;
      sample_abs_index_i = base_index_i - 32'd1;
      drive_samples_r = 1'b1;
      sample_valid_i = 1'b1;
      wait_count_v = 0;
      while (!done_o && (wait_count_v < 10000)) begin
        @(posedge read_clk);
        wait_count_v = wait_count_v + 1;
      end
      sample_valid_i = 1'b0;
      drive_samples_r = 1'b0;
      if (!done_o) begin
        fail_check("采集未在窗口长度内完成");
      end else begin
        if (meta_trigger_abs_index_o !== expected_trigger_v)
          fail_check("触发绝对索引未保持外部时间轴");
        if (meta_pretrigger_count_o !== trigger_offset_i ||
            meta_trigger_sample_index_o !== trigger_offset_i)
          fail_check("触发局部索引与 pre-count 不一致");
        if (meta_window_start_abs_index_o !== base_index_i)
          fail_check("窗口起点未使用外部绝对索引");
      end
      repeat (4) @(posedge read_clk);
    end
  endtask

  initial begin
    sample_clk = 1'b0;
    read_clk = 1'b0;
    sample_rst_n = 1'b0;
    read_rst_n = 1'b0;
    arm_i = 1'b0;
    trigger_addr_i = 32'd0;
    sample_valid_i = 1'b0;
    drive_samples_r = 1'b0;
    sample_i = 1024'd0;
    sample_abs_index_i = 32'd0;
    accepted_count_r = 0;
    failures_r = 0;

    repeat (4) @(posedge sample_clk);
    sample_rst_n = 1'b1;
    read_rst_n = 1'b1;
    repeat (8) @(posedge read_clk);

    run_capture(32'h12345600, 32'd2047);
    run_capture(32'hfffffc00, 32'd2047);

    if (accepted_count_r != 2) fail_check("连续两次 ARM 接受次数错误");
    if (failures_r == 0) begin
      $display("PASS tb_lockstep_capture_absolute_index");
    end else begin
      $display("FAIL tb_lockstep_capture_absolute_index failures=%0d", failures_r);
    end
    $finish;
  end
endmodule
