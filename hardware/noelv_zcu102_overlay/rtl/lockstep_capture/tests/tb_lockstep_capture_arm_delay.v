/**********************************************************
* 文件名: tb_lockstep_capture_arm_delay.v
* 日期: 2026-07-17
* 版本: 1.0
* 更新记录: 将 ARM 延迟握手回归固化到当前 1024-bit 产品测试目录。
* 描述: 验证 read_clk 域 ARM 请求在 sample_clk 域就绪前不会丢失，
*       且一次请求只产生一次接受确认。
**********************************************************/

`timescale 1ps/1ps

module tb_lockstep_capture_arm_delay;
  localparam integer PROBE_SAMPLE_BITS = 1024;
  localparam integer LANE_BITS = 128;
  localparam integer SAMPLE_ADDR_WIDTH = 12;

  reg sample_clk;
  reg read_clk;
  reg sample_rst_n;
  reg read_rst_n;
  reg arm_i;
  reg [8:0] protocol_enable_i;
  reg [31:0] trigger_addr_i;
  reg trigger_mismatch_enable_i;
  reg [4:0] trigger_mismatch_mask_i;
  reg sample_valid_i;
  reg [PROBE_SAMPLE_BITS-1:0] sample_i;
  reg [31:0] sample_abs_index_i;
  wire arm_ready_o;
  wire arm_accepted_o;
  integer accepted_count_r;
  integer failures_r;

  always #5000 sample_clk = !sample_clk;
  always #7000 read_clk = !read_clk;

  lockstep_wide_capture_window_cdc #(
    .PROBE_SAMPLE_BITS(PROBE_SAMPLE_BITS),
    .MAX_PROBE_SAMPLE_BITS(PROBE_SAMPLE_BITS),
    .LANE_BITS(LANE_BITS),
    .LANE_INDEX_BITS(4),
    .PROTOCOL_COUNT(9),
    .SAMPLE_ADDR_WIDTH(SAMPLE_ADDR_WIDTH)
  ) dut (
    .sample_clk(sample_clk),
    .sample_rst_n(sample_rst_n),
    .read_clk(read_clk),
    .read_rst_n(read_rst_n),
    .arm_i(arm_i),
    .stop_i(1'b0),
    .trigger_timeout_samples_i(32'hffffffff),
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
    .done_o(),
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

  always @(posedge read_clk) begin
    if (arm_accepted_o) begin
      accepted_count_r = accepted_count_r + 1;
    end
  end

  initial begin
    sample_clk = 1'b0;
    read_clk = 1'b0;
    sample_rst_n = 1'b0;
    read_rst_n = 1'b0;
    arm_i = 1'b0;
    protocol_enable_i = 9'h1ff;
    trigger_addr_i = 32'hffffffff;
    trigger_mismatch_enable_i = 1'b0;
    trigger_mismatch_mask_i = 5'd0;
    sample_valid_i = 1'b0;
    sample_i = 1024'd0;
    sample_abs_index_i = 32'd0;
    accepted_count_r = 0;
    failures_r = 0;

    repeat (4) @(posedge read_clk);
    read_rst_n = 1'b1;
    @(negedge read_clk);
    arm_i = 1'b1;
    @(negedge read_clk);
    arm_i = 1'b0;
    repeat (8) @(posedge read_clk);
    if (accepted_count_r != 0) begin
      $display("FAIL ARM accepted before sample domain reset release");
      failures_r = failures_r + 1;
    end

    sample_rst_n = 1'b1;
    repeat (20) @(posedge read_clk);
    if (accepted_count_r != 1) begin
      $display("FAIL ARM acceptance count=%0d ready=%b", accepted_count_r, arm_ready_o);
      failures_r = failures_r + 1;
    end

    repeat (20) @(posedge read_clk);
    if (accepted_count_r != 1) begin
      $display("FAIL ARM request was accepted more than once count=%0d", accepted_count_r);
      failures_r = failures_r + 1;
    end

    if (failures_r == 0) begin
      $display("PASS tb_lockstep_capture_arm_delay");
    end else begin
      $display("FAIL tb_lockstep_capture_arm_delay failures=%0d", failures_r);
    end
    $finish;
  end
endmodule
