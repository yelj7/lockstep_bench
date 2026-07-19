/**********************************************************
* 文件名: tb_lockstep_event_async_fifo.v
* 日期: 2026-07-19
* 版本: 1.1
* 更新记录: 增加深度 8 满容量和第 9 条反压回归。
* 描述: 验证满容量、跨时钟事件顺序、数据稳定性和无误丢失。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_event_async_fifo;
  reg write_clk;
  reg write_rst_n;
  reg write_valid;
  reg [511:0] write_data;
  reg read_clk;
  reg read_rst_n;
  reg read_ready;
  wire write_ready;
  wire write_drop;
  wire read_valid;
  wire [511:0] read_data;
  integer sent;
  integer received;
  integer read_cycle;
  reg [511:0] stalled_data;
  reg stall_active;
  integer fill_index;

  lockstep_event_async_fifo #(
    .ADDR_WIDTH (3),
    .DEPTH      (8)
  ) dut (
    .write_clk     (write_clk),
    .write_rst_n   (write_rst_n),
    .write_valid_i (write_valid),
    .write_ready_o (write_ready),
    .write_data_i  (write_data),
    .write_drop_o  (write_drop),
    .read_clk      (read_clk),
    .read_rst_n    (read_rst_n),
    .read_valid_o  (read_valid),
    .read_ready_i  (read_ready),
    .read_data_o   (read_data)
  );

  always #4 write_clk = !write_clk;
  always #7 read_clk = !read_clk;

  task fail;
    input [1023:0] message;
    begin
      $display("FAIL time=%0t message=%0s sent=%0d received=%0d", $time, message, sent, received);
      $finish;
    end
  endtask

  always @(negedge read_clk) begin
    if (read_rst_n) begin
      read_cycle = read_cycle + 1;
      read_ready = ((read_cycle % 4) != 0);
    end
  end

  always @(posedge read_clk) begin
    if (read_rst_n) begin
      if (stall_active && (!read_valid || read_data != stalled_data)) begin
        fail("反压期间跨域数据变化");
      end
      stall_active = read_valid && !read_ready;
      if (stall_active) stalled_data = read_data;
      if (read_valid && read_ready) begin
        if (read_data[31:0] != received) fail("跨域事件顺序或数据错误");
        received = received + 1;
      end
    end
  end

  initial begin
    write_clk = 1'b0;
    write_rst_n = 1'b0;
    write_valid = 1'b0;
    write_data = 512'd0;
    read_clk = 1'b0;
    read_rst_n = 1'b0;
    read_ready = 1'b0;
    sent = 0;
    received = 0;
    read_cycle = 0;
    stalled_data = 512'd0;
    stall_active = 1'b0;

    #31;
    write_rst_n = 1'b1;
    for (fill_index = 0; fill_index < 8; fill_index = fill_index + 1) begin
      @(negedge write_clk);
      if (!write_ready) fail("FIFO 在写满 8 条之前提前反压");
      write_data = fill_index;
      write_valid = 1'b1;
      @(posedge write_clk);
      #2;
      write_valid = 1'b0;
    end
    #2;
    if (write_ready) fail("FIFO 写入 8 条后仍未报告满");
    @(negedge write_clk);
    write_valid = 1'b1;
    write_data = 32'hdeadbeef;
    @(posedge write_clk);
    #2;
    write_valid = 1'b0;
    if (!write_drop) fail("FIFO 第 9 条写入未被阻塞并报告 drop");

    write_rst_n = 1'b0;
    repeat (2) @(posedge write_clk);
    sent = 0;
    #3;
    write_rst_n = 1'b1;
    read_rst_n = 1'b1;
    while (sent < 40) begin
      @(negedge write_clk);
      if (write_ready) begin
        write_data = sent;
        write_valid = 1'b1;
        sent = sent + 1;
      end else begin
        write_valid = 1'b0;
      end
      @(posedge write_clk);
      #2;
      write_valid = 1'b0;
      if (write_drop) fail("遵守 ready 时不应产生 drop");
    end
    while (received < 40) begin
      @(posedge read_clk);
      if ($time > 20000) fail("异步 FIFO 排空超时");
    end
    repeat (4) @(posedge read_clk);
    if (read_valid) fail("异步 FIFO 排空后 valid 未清除");
    $display("PASS tb_lockstep_event_async_fifo");
    $finish;
  end

endmodule
