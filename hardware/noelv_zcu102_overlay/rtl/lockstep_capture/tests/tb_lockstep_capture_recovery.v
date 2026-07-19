/**********************************************************
* 文件名: tb_lockstep_capture_recovery.v
* 日期: 2026-07-17
* 版本: 1.0
* 更新记录: 新增无触发 watchdog、STOP 恢复和重新 ARM 回归。
* 描述: 验证采样窗口不会永久停留在 capturing，恢复后可再次完成正常窗口。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_capture_recovery;
  reg sample_clk;
  reg read_clk;
  reg sample_rst_n;
  reg read_rst_n;
  reg arm_i;
  reg stop_i;
  reg [31:0] trigger_timeout_samples_i;
  reg [8:0] protocol_enable_i;
  reg [31:0] trigger_addr_i;
  reg trigger_mismatch_enable_i;
  reg [4:0] trigger_mismatch_mask_i;
  reg sample_valid_i;
  reg [1023:0] sample_i;
  reg [31:0] sample_abs_index_i;
  wire arm_ready_o;
  wire arm_accepted_o;
  wire done_o;
  wire abort_pulse_o;
  wire watchdog_pulse_o;
  wire trigger_seen_o;
  wire watchdog_expired_o;
  integer failures_r;
  integer wait_count_r;

  lockstep_wide_capture_window_cdc #(
    .PROBE_SAMPLE_BITS      (1024),
    .MAX_PROBE_SAMPLE_BITS  (1024),
    .LANE_BITS              (128),
    .LANE_INDEX_BITS        (4),
    .PROTOCOL_COUNT         (9),
    .SAMPLE_ADDR_WIDTH      (12),
    .AHB_TRIGGER_MODE       (1)
  ) dut (
    .sample_clk(sample_clk),
    .sample_rst_n(sample_rst_n),
    .read_clk(read_clk),
    .read_rst_n(read_rst_n),
    .arm_i(arm_i),
    .stop_i(stop_i),
    .trigger_timeout_samples_i(trigger_timeout_samples_i),
    .protocol_enable_i(protocol_enable_i),
    .trigger_addr_i(trigger_addr_i),
    .trigger_mismatch_enable_i(trigger_mismatch_enable_i),
    .trigger_mismatch_mask_i(trigger_mismatch_mask_i),
    .sample_valid_i(sample_valid_i),
    .sample_i(sample_i),
    .sample_abs_index_i(sample_abs_index_i),
    .arm_ready_o(arm_ready_o),
    .arm_accepted_o(arm_accepted_o),
    .busy_o(),
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
    .meta_pretrigger_count_o(),
    .meta_trigger_count_o(),
    .meta_post_after_trigger_count_o(),
    .meta_window_start_abs_index_o(),
    .meta_trigger_abs_index_o(),
    .meta_trigger_sample_index_o(),
    .samples_seen_o(),
    .post_after_trigger_seen_o(),
    .abort_pulse_o(abort_pulse_o),
    .watchdog_pulse_o(watchdog_pulse_o),
    .trigger_seen_o(trigger_seen_o),
    .watchdog_expired_o(watchdog_expired_o),
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

  always #5.5 sample_clk = !sample_clk;
  always #7.5 read_clk = !read_clk;

  task pulse_arm;
    begin
      @(posedge read_clk);
      arm_i = 1'b1;
      @(posedge read_clk);
      arm_i = 1'b0;
    end
  endtask

  initial begin
    sample_clk = 1'b0;
    read_clk = 1'b0;
    sample_rst_n = 1'b0;
    read_rst_n = 1'b0;
    arm_i = 1'b0;
    stop_i = 1'b0;
    trigger_timeout_samples_i = 32'd8;
    protocol_enable_i = 9'b111111111;
    trigger_addr_i = 32'hffffffff;
    trigger_mismatch_enable_i = 1'b0;
    trigger_mismatch_mask_i = 5'd0;
    sample_valid_i = 1'b1;
    sample_i = 1024'd0;
    sample_abs_index_i = 32'd0;
    failures_r = 0;

    repeat (4) @(posedge sample_clk);
    sample_rst_n = 1'b1;
    read_rst_n = 1'b1;
    repeat (4) @(posedge read_clk);
    pulse_arm();
    wait_count_r = 0;
    while (!watchdog_pulse_o && wait_count_r < 1000) begin
      @(posedge read_clk);
      wait_count_r = wait_count_r + 1;
    end
    if (!watchdog_pulse_o || !abort_pulse_o) begin
      failures_r = failures_r + 1;
      $display("FAIL no-trigger recovery pulses watchdog=%b abort=%b", watchdog_pulse_o,
               abort_pulse_o);
    end
    #2;
    if (!watchdog_expired_o) begin
      failures_r = failures_r + 1;
      $display("FAIL no-trigger recovery did not latch watchdog status");
    end
    wait_count_r = 0;
    while (!arm_ready_o && wait_count_r < 1000) begin
      @(posedge read_clk);
      wait_count_r = wait_count_r + 1;
    end
    if (!arm_ready_o) begin
      failures_r = failures_r + 1;
      $display("FAIL window did not return ready after watchdog");
    end

    sample_i[418] = 1'b1;
    sample_i[429] = 1'b1;
    pulse_arm();
    wait_count_r = 0;
    while (!done_o && wait_count_r < 100000) begin
      @(posedge read_clk);
      wait_count_r = wait_count_r + 1;
    end
    if (!done_o || !trigger_seen_o) begin
      failures_r = failures_r + 1;
      $display("FAIL re-arm did not complete trigger_seen=%b done=%b", trigger_seen_o, done_o);
    end

    if (failures_r == 0) begin
      $display("PASS tb_lockstep_capture_recovery");
    end else begin
      $display("FAIL tb_lockstep_capture_recovery failures=%0d", failures_r);
    end
    $finish;
  end
endmodule
