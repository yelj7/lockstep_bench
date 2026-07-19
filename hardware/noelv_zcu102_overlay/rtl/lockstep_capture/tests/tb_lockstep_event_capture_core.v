/**********************************************************
* 文件名: tb_lockstep_event_capture_core.v
* 日期: 2026-07-19
* 版本: 1.0
* 更新记录: 新增事件 FIFO、公平性、反压和溢出回归。
* 描述: 验证九路事件核心的数据完整性、轮询次序、稳定性和统计合同。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_event_capture_core;
  localparam integer DATA_WIDTH = 512;

  reg clk;
  reg rst_n;
  reg capture_start;
  reg [8:0] source_enable_mask;
  reg [8:0] source_push;
  reg [9*DATA_WIDTH-1:0] source_record;
  reg event_ready;
  wire [8:0] source_accept;
  wire [8:0] source_drop;
  wire event_valid;
  wire [DATA_WIDTH-1:0] event_record;
  wire [3:0] event_source;
  wire [8:0] overflow_mask;
  wire [9*32-1:0] accepted_count;
  wire [9*32-1:0] emitted_count;
  wire [9*32-1:0] dropped_count;
  reg [DATA_WIDTH-1:0] stalled_record;
  integer index;
  integer seen_count;
  reg [8:0] seen_mask;

  lockstep_event_capture_core #(
    .FIFO_DEPTH      (2),
    .FIFO_ADDR_WIDTH (1)
  ) dut (
    .clk                  (clk),
    .rst_n                (rst_n),
    .capture_start_i      (capture_start),
    .source_enable_mask_i (source_enable_mask),
    .source_push_i        (source_push),
    .source_record_i      (source_record),
    .source_accept_o      (source_accept),
    .source_drop_o        (source_drop),
    .event_valid_o        (event_valid),
    .event_ready_i        (event_ready),
    .event_record_o       (event_record),
    .event_source_o       (event_source),
    .overflow_mask_o      (overflow_mask),
    .accepted_count_o     (accepted_count),
    .emitted_count_o      (emitted_count),
    .dropped_count_o      (dropped_count)
  );

  always #5 clk = !clk;

  task fail;
    input [1023:0] message;
    begin
      $display("FAIL time=%0t message=%0s", $time, message);
      $finish;
    end
  endtask

  initial begin
    clk = 1'b0;
    rst_n = 1'b0;
    capture_start = 1'b0;
    source_enable_mask = 9'h1ff;
    source_push = 9'd0;
    source_record = {9*DATA_WIDTH{1'b0}};
    event_ready = 1'b0;
    seen_count = 0;
    seen_mask = 9'd0;

    repeat (3) @(posedge clk);
    rst_n = 1'b1;
    capture_start = 1'b1;
    @(posedge clk);
    capture_start = 1'b0;

    // 九路同时写入唯一编号，轮询必须在九次握手内覆盖全部源。
    for (index = 0; index < 9; index = index + 1) begin
      source_record[index*DATA_WIDTH +: DATA_WIDTH] = index + 32'h100;
    end
    source_push = 9'h1ff;
    @(posedge clk);
    #2;
    source_push = 9'd0;
    event_ready = 1'b1;
    while (seen_count < 9) begin
      @(negedge clk);
      if (event_valid && event_ready) begin
        if (seen_mask[event_source]) fail("同一源在公平性窗口内重复授权");
        if (event_record[31:0] != event_source + 32'h100) fail("事件数据与源编号不一致");
        seen_mask[event_source] = 1'b1;
        seen_count = seen_count + 1;
      end
    end
    if (seen_mask != 9'h1ff) fail("九路公平性覆盖失败");

    // 反压期间输出必须保持不变。
    event_ready = 1'b0;
    source_record[DATA_WIDTH-1:0] = 512'habc;
    source_push = 9'h001;
    @(posedge clk);
    #2;
    source_push = 9'd0;
    if (!event_valid) fail("反压测试未产生有效事件");
    stalled_record = event_record;
    repeat (4) begin
      @(posedge clk);
      #2;
      if (!event_valid || event_record != stalled_record) fail("反压期间事件发生变化");
    end
    event_ready = 1'b1;
    @(posedge clk);
    #2;

    // 深度为 2，持续阻塞时第三次写入必须显式 drop 并置 overflow。
    capture_start = 1'b1;
    @(posedge clk);
    #2;
    capture_start = 1'b0;
    event_ready = 1'b0;
    for (index = 0; index < 3; index = index + 1) begin
      source_record[2*DATA_WIDTH +: DATA_WIDTH] = index + 32'h200;
      source_push = 9'h004;
      @(posedge clk);
      #2;
      source_push = 9'd0;
      @(posedge clk);
      #2;
    end
    if (!overflow_mask[2]) fail("FIFO 满未置 overflow");
    if (dropped_count[3*32-1:2*32] != 32'd1) fail("dropped 计数错误");
    if (accepted_count[3*32-1:2*32] != 32'd2) fail("accepted 计数错误");

    $display("PASS tb_lockstep_event_capture_core");
    $finish;
  end

endmodule
