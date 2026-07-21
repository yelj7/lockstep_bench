/**********************************************************
* 文件名: tb_lockstep_event_capture_capacity.v
* 日期: 2026-07-20
* 版本: 1.1
* 更新记录: 增加九路同时 1100 条阻塞突发及完整排空回归。
* 描述: 验证独立 FIFO 容量、顺序、公平性以及事件流与 4096 点周期窗口解耦。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_event_capture_capacity;
  localparam integer DATA_WIDTH = 512;
  localparam integer AHB_EVENTS = 64;
  localparam integer JTAG_EVENTS = 300;
  localparam integer ALL_SOURCE_EVENTS = 1100;

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
  integer long_ahb_count;
  integer source_id;
  integer seen_by_source [0:8];

  lockstep_event_capture_core #(
    .AHB_FIFO_DEPTH           (4096),
    .AHB_FIFO_ADDR_WIDTH      (12),
    .UART_FIFO_DEPTH          (2048),
    .UART_FIFO_ADDR_WIDTH     (11),
    .SPI_FIFO_DEPTH           (2048),
    .SPI_FIFO_ADDR_WIDTH      (11),
    .CAN_FIFO_DEPTH           (2048),
    .CAN_FIFO_ADDR_WIDTH      (11),
    .I2C_FIFO_DEPTH           (2048),
    .I2C_FIFO_ADDR_WIDTH      (11),
    .ETH_FIFO_DEPTH           (2048),
    .ETH_FIFO_ADDR_WIDTH      (11),
    .USB_FIFO_DEPTH           (2048),
    .USB_FIFO_ADDR_WIDTH      (11),
    .JTAG_FIFO_DEPTH          (4096),
    .JTAG_FIFO_ADDR_WIDTH     (12),
    .MISMATCH_FIFO_DEPTH      (2048),
    .MISMATCH_FIFO_ADDR_WIDTH (11)
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

    // 九路同时缓存超过板级实测 1043 条峰值，随后必须公平且无损排空。
    capture_start = 1'b1;
    source_enable_mask = 9'h1ff;
    event_ready = 1'b0;
    @(posedge clk);
    #2;
    capture_start = 1'b0;
    for (index = 0; index < ALL_SOURCE_EVENTS; index = index + 1) begin
      for (source_id = 0; source_id < 9; source_id = source_id + 1)
        source_record[source_id*DATA_WIDTH +: DATA_WIDTH] = index;
      source_push = 9'h1ff;
      @(posedge clk);
      #2;
      if (source_accept != 9'h1ff || source_drop != 9'd0)
        fail("九路 1100 条阻塞突发不应溢出");
    end
    source_push = 9'd0;
    for (source_id = 0; source_id < 9; source_id = source_id + 1)
      if (accepted_count[source_id*32 +: 32] != ALL_SOURCE_EVENTS)
        fail("九路阻塞突发 accepted 计数错误");
    if (overflow_mask != 9'd0 || dropped_count != {9*32{1'b0}})
      fail("九路阻塞突发出现 overflow 或 dropped");
    event_ready = 1'b1;
    seen_total = 0;
    for (source_id = 0; source_id < 9; source_id = source_id + 1)
      seen_by_source[source_id] = 0;
    timeout_cycles = 0;
    while ((seen_total < 9*ALL_SOURCE_EVENTS) && (timeout_cycles < 12000)) begin
      @(negedge clk);
      timeout_cycles = timeout_cycles + 1;
      if (event_valid && event_ready) begin
        if (event_source > 8 || event_record[31:0] != seen_by_source[event_source])
          fail("九路阻塞突发排空顺序错误");
        seen_by_source[event_source] = seen_by_source[event_source] + 1;
        seen_total = seen_total + 1;
      end
    end
    @(posedge clk);
    #2;
    if (seen_total != 9*ALL_SOURCE_EVENTS)
      fail("九路阻塞突发总数未完整排空");
    for (source_id = 0; source_id < 9; source_id = source_id + 1)
      if (seen_by_source[source_id] != ALL_SOURCE_EVENTS ||
          emitted_count[source_id*32 +: 32] != ALL_SOURCE_EVENTS)
        fail("九路阻塞突发分源计数未完整排空");
    if (overflow_mask != 9'd0 || dropped_count != {9*32{1'b0}})
      fail("九路阻塞突发排空后出现 overflow 或 dropped");

    // AHB 超过周期窗口容量后，后续低速协议仍必须可见。
    capture_start = 1'b1;
    source_enable_mask = 9'h01f;
    @(posedge clk);
    #2;
    capture_start = 1'b0;
    event_ready = 1'b1;
    for (long_ahb_count = 0; long_ahb_count < 4100; long_ahb_count = long_ahb_count + 1) begin
      source_record[DATA_WIDTH-1:0] = long_ahb_count;
      source_push = 9'h001;
      @(posedge clk);
      #2;
      if (!source_accept[0] || source_drop != 9'd0)
        fail("无限长事件流中的 AHB 事件未被接收");
    end
    source_record[2*DATA_WIDTH-1:1*DATA_WIDTH] = 512'h1111;
    source_record[3*DATA_WIDTH-1:2*DATA_WIDTH] = 512'h2222;
    source_record[4*DATA_WIDTH-1:3*DATA_WIDTH] = 512'h3333;
    source_record[5*DATA_WIDTH-1:4*DATA_WIDTH] = 512'h4444;
    source_push = 9'h01f;
    @(posedge clk);
    #2;
    if ((source_accept & 9'h01f) != 9'h01f || source_drop != 9'd0)
      fail("超过 4096 条 AHB 后低速协议未被并发接收");
    source_push = 9'd0;
    @(posedge clk);
    #2;
    if (accepted_count[1*32-1:0*32] != 32'd4101 ||
        accepted_count[2*32-1:1*32] != 32'd1 ||
        accepted_count[3*32-1:2*32] != 32'd1 ||
        accepted_count[4*32-1:3*32] != 32'd1 ||
        accepted_count[5*32-1:4*32] != 32'd1)
      fail("长事件流协议计数错误");

    $display("PASS tb_lockstep_event_capture_capacity");
    $finish;
  end

endmodule
