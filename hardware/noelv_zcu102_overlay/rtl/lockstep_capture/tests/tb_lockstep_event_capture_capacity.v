/**********************************************************
* 文件名: tb_lockstep_event_capture_capacity.v
* 日期: 2026-07-20
* 版本: 1.0
* 更新记录: 新增 AHB/JTAG 并发突发容量与无丢失回归。
* 描述: 模拟板级长反压后排空 364 条事件，验证独立 FIFO 容量和顺序。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_event_capture_capacity;
  localparam integer DATA_WIDTH = 512;
  localparam integer AHB_EVENTS = 64;
  localparam integer JTAG_EVENTS = 300;

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
  integer index;
  integer seen_total;
  integer seen_ahb;
  integer seen_jtag;
  integer timeout_cycles;

  lockstep_event_capture_core #(
    .AHB_FIFO_DEPTH       (512),
    .AHB_FIFO_ADDR_WIDTH  (9),
    .JTAG_FIFO_DEPTH      (512),
    .JTAG_FIFO_ADDR_WIDTH (9)
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
    source_enable_mask = 9'h081;
    source_push = 9'd0;
    source_record = {9*DATA_WIDTH{1'b0}};
    event_ready = 1'b0;
    seen_total = 0;
    seen_ahb = 0;
    seen_jtag = 0;

    repeat (3) @(posedge clk);
    rst_n = 1'b1;
    capture_start = 1'b1;
    @(posedge clk);
    #2;
    capture_start = 1'b0;

    // 下游完全阻塞期间制造与板上失败同量级的 JTAG 和 AHB 突发。
    for (index = 0; index < JTAG_EVENTS; index = index + 1) begin
      source_record[8*DATA_WIDTH-1:7*DATA_WIDTH] = index;
      source_push = 9'h080;
      if (index < AHB_EVENTS) begin
        source_record[DATA_WIDTH-1:0] = index;
        source_push = source_push | 9'h001;
      end
      @(posedge clk);
      #2;
      if (source_drop != 9'd0) fail("容量范围内不应丢失事件");
    end
    source_push = 9'd0;

    if (overflow_mask != 9'd0 || dropped_count != {9*32{1'b0}})
      fail("突发缓存阶段出现 overflow 或 dropped");
    if (accepted_count[1*32-1:0*32] != AHB_EVENTS ||
        accepted_count[8*32-1:7*32] != JTAG_EVENTS)
      fail("突发 accepted 计数错误");

    event_ready = 1'b1;
    timeout_cycles = 0;
    while ((seen_total < AHB_EVENTS + JTAG_EVENTS) && (timeout_cycles < 1000)) begin
      @(negedge clk);
      timeout_cycles = timeout_cycles + 1;
      if (event_valid && event_ready) begin
        if (event_source == 0) begin
          if (event_record[31:0] != seen_ahb) fail("AHB 事件顺序错误");
          seen_ahb = seen_ahb + 1;
        end else if (event_source == 7) begin
          if (event_record[31:0] != seen_jtag) fail("JTAG 事件顺序错误");
          seen_jtag = seen_jtag + 1;
        end else begin
          fail("出现未启用协议事件");
        end
        seen_total = seen_total + 1;
      end
    end

    if (seen_total != AHB_EVENTS + JTAG_EVENTS ||
        seen_ahb != AHB_EVENTS || seen_jtag != JTAG_EVENTS)
      fail("突发事件未完整排空");
    @(posedge clk);
    #2;
    if (emitted_count[1*32-1:0*32] != AHB_EVENTS ||
        emitted_count[8*32-1:7*32] != JTAG_EVENTS)
      fail("排空 emitted 计数错误");
    if (overflow_mask != 9'd0 || dropped_count != {9*32{1'b0}})
      fail("排空后出现 overflow 或 dropped");

    $display("PASS tb_lockstep_event_capture_capacity");
    $finish;
  end

endmodule
