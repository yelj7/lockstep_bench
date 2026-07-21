/**********************************************************
* 文件名: tb_lockstep_protocol_event_encoder.v
* 日期: 2026-07-19
* 版本: 1.2
* 更新记录: 增加 I2C START、STOP、SCL 上升沿准入和普通边沿抑制回归。
* 描述: 验证各真实事件的准入、时间戳和 ETH/USB design gap。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_protocol_event_encoder;
  reg clk;
  reg rst_n;
  reg capture_start;
  reg capture_active;
  reg [31:0] capture_id;
  reg [63:0] timestamp;
  reg [8:0] source_enable_mask;
  reg sample_valid;
  reg [1023:0] sample;
  wire [8:0] source_push;
  wire [9*512-1:0] source_record;

  lockstep_protocol_event_encoder dut (
    .clk(clk), .rst_n(rst_n), .capture_start_i(capture_start),
    .capture_active_i(capture_active), .capture_id_i(capture_id),
    .timestamp_i(timestamp), .source_enable_mask_i(source_enable_mask),
    .sample_valid_i(sample_valid), .sample_i(sample),
    .source_push_o(source_push), .source_record_o(source_record)
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
    capture_active = 1'b0;
    capture_id = 32'h12345678;
    timestamp = 64'h1122334455667788;
    source_enable_mask = 9'h1ff;
    sample_valid = 1'b1;
    sample = 1024'd0;
    repeat (3) @(posedge clk);
    rst_n = 1'b1;
    capture_start = 1'b1;
    sample[512+6] = 1'b1;
    #2;
    if (source_record[1*512+63:1*512] != timestamp) begin
      fail("触发周期事件时间戳被错误清零");
    end
    @(posedge clk);
    #2;
    capture_start = 1'b0;
    capture_active = 1'b1;
    sample = 1024'd0;

    sample[442] = 1'b1;
    sample[429] = 1'b1;
    sample[425] = 1'b1;
    sample[63:32] = 32'h80000100;
    sample[512+6] = 1'b1;
    sample[512+224+8] = 1'b1;
    sample[502] = 1'b1;
    #2;
    if (source_push != 9'h183) fail("真实事件源位图错误");
    if (source_record[63:0] != timestamp || source_record[95:64] != capture_id ||
        source_record[135:128] != 8'd0 || source_record[191:160] != 32'h1) begin
      fail("AHB 事件公共字段错误");
    end
    if (source_record[256 +: 32] != 32'h80000100) fail("AHB 地址 payload 错误");
    if (source_record[7*512+135:7*512+128] != 8'd7 ||
        source_record[7*512+191:7*512+160] != 32'h80) begin
      fail("JTAG 事件字段错误");
    end
    if (source_push[5] || source_push[6] ||
        source_record[6*512-1:5*512] != 512'd0 ||
        source_record[7*512-1:6*512] != 512'd0) begin
      fail("ETH/USB 不得生成伪事件");
    end

    @(posedge clk);
    #2;
    sample = 1024'd0;
    #2;
    if (source_push != 9'h100) fail("mismatch 恢复变化必须单独记录");
    if (source_record[8*512+127:8*512+96] != 32'd1) fail("mismatch 本地序号未递增");

    @(posedge clk);
    #2;
    sample = 1024'd0;
    sample[442] = 1'b1;
    sample[429] = 1'b1;
    sample[425] = 1'b1;
    sample[63:32] = 32'h80000100;
    #2;
    if (source_push[0]) fail("duplicate stable AHB read must be suppressed");
    repeat (5000) begin
      @(posedge clk);
      #2;
      if (source_push[0]) fail("long stable AHB polling must remain suppressed");
    end

    sample[223:192] = 32'h00000001;
    #2;
    if (!source_push[0]) fail("changed AHB read data must be emitted");
    @(posedge clk);
    #2;

    sample[416] = 1'b1;
    #2;
    if (!source_push[0]) fail("every AHB data write must be emitted");

    sample[416] = 1'b0;
    sample[425] = 1'b0;
    sample[63:32] = 32'h00000004;
    #2;
    if (source_push[0]) fail("AHB instruction fetch must not enter semantic event stream");

    sample = 1024'd0;
    sample[512+32+31] = 1'b1;
    #2;
    if (source_push[2]) fail("SPI active level must not emit every sample cycle");
    repeat (5000) begin
      @(posedge clk);
      #2;
      if (source_push[2]) fail("long SPI active level must remain suppressed");
    end
    sample[512+32+5] = 1'b1;
    #2;
    if (!source_push[2]) fail("SPI SCLK rising edge must be emitted");

    sample = 1024'd0;
    sample[512+96+29] = 1'b1;
    #2;
    if (source_push[4]) fail("I2C generic activity must not emit non-sampling edges");
    sample[512+96+6] = 1'b1;
    #2;
    if (!source_push[4]) fail("I2C SCL rising edge must be emitted");
    sample[512+96+6] = 1'b0;
    sample[512+96+2] = 1'b1;
    #2;
    if (!source_push[4]) fail("I2C START must be emitted");
    sample[512+96+2] = 1'b0;
    sample[512+96+3] = 1'b1;
    #2;
    if (!source_push[4]) fail("I2C STOP must be emitted");

    $display("PASS tb_lockstep_protocol_event_encoder");
    $finish;
  end

endmodule
