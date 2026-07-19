/**********************************************************
* 文件名: tb_lockstep_protocol_probe_real_only.v
* 日期: 2026-07-17
* 版本: 1.0
* 更新记录: 新增生产探针禁止伪协议活动的回归测试。
* 描述: 验证采样索引变化不会生成 SPI、CAN、I2C、ETH 或 USB 伪事务，
*       并验证启用协议后输出只跟随真实输入线。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_protocol_probe_real_only;
  reg clk;
  reg rst_n;
  reg sample_valid_i;
  reg [31:0] sample_abs_index_i;
  reg uart_rx_i;
  reg uart_tx_i;
  reg uart_ctsn_i;
  reg uart_rtsn_i;
  reg spi_core_enabled_i;
  reg spi_sclk_i;
  reg spi_mosi_i;
  reg spi_miso_i;
  reg spi_cs_n_i;
  reg jtag_tck_i;
  reg jtag_tms_i;
  reg jtag_tdi_i;
  reg jtag_tdo_i;
  reg rv_jtag_tck_i;
  reg rv_jtag_tms_i;
  reg rv_jtag_tdi_i;
  reg rv_jtag_tdo_i;
  reg can_core_enabled_i;
  reg can_rx_i;
  reg can_tx_i;
  reg i2c_core_enabled_i;
  reg i2c_scl_i;
  reg i2c_sda_i;
  reg eth_core_enabled_i;
  reg eth_tx_en_i;
  reg eth_tx_er_i;
  reg eth_rx_dv_i;
  reg eth_rx_er_i;
  reg [7:0] eth_txd_i;
  reg [7:0] eth_rxd_i;
  wire [511:0] probe_o;
  integer failures_r;
  integer cycle_r;

  lockstep_protocol_probe_pack dut (
    .clk(clk),
    .rst_n(rst_n),
    .sample_valid_i(sample_valid_i),
    .sample_abs_index_i(sample_abs_index_i),
    .uart_rx_i(uart_rx_i),
    .uart_tx_i(uart_tx_i),
    .uart_ctsn_i(uart_ctsn_i),
    .uart_rtsn_i(uart_rtsn_i),
    .spi_core_enabled_i(spi_core_enabled_i),
    .spi_sclk_i(spi_sclk_i),
    .spi_mosi_i(spi_mosi_i),
    .spi_miso_i(spi_miso_i),
    .spi_cs_n_i(spi_cs_n_i),
    .jtag_tck_i(jtag_tck_i),
    .jtag_tms_i(jtag_tms_i),
    .jtag_tdi_i(jtag_tdi_i),
    .jtag_tdo_i(jtag_tdo_i),
    .rv_jtag_tck_i(rv_jtag_tck_i),
    .rv_jtag_tms_i(rv_jtag_tms_i),
    .rv_jtag_tdi_i(rv_jtag_tdi_i),
    .rv_jtag_tdo_i(rv_jtag_tdo_i),
    .can_core_enabled_i(can_core_enabled_i),
    .can_rx_i(can_rx_i),
    .can_tx_i(can_tx_i),
    .i2c_core_enabled_i(i2c_core_enabled_i),
    .i2c_scl_i(i2c_scl_i),
    .i2c_sda_i(i2c_sda_i),
    .eth_core_enabled_i(eth_core_enabled_i),
    .eth_tx_en_i(eth_tx_en_i),
    .eth_tx_er_i(eth_tx_er_i),
    .eth_rx_dv_i(eth_rx_dv_i),
    .eth_rx_er_i(eth_rx_er_i),
    .eth_txd_i(eth_txd_i),
    .eth_rxd_i(eth_rxd_i),
    .probe_o(probe_o)
  );

  initial begin
    clk = 1'b0;
    forever #5 clk = !clk;
  end

  task fail_check;
    input [1023:0] message_i;
    begin
      failures_r = failures_r + 1;
      $display("FAIL time=%0t message=%0s probe=0x%0128x", $time, message_i, probe_o);
    end
  endtask

  initial begin
    rst_n = 1'b0;
    sample_valid_i = 1'b1;
    sample_abs_index_i = 32'd0;
    uart_rx_i = 1'b1;
    uart_tx_i = 1'b1;
    uart_ctsn_i = 1'b1;
    uart_rtsn_i = 1'b1;
    spi_core_enabled_i = 1'b0;
    spi_sclk_i = 1'b0;
    spi_mosi_i = 1'b0;
    spi_miso_i = 1'b0;
    spi_cs_n_i = 1'b1;
    jtag_tck_i = 1'b0;
    jtag_tms_i = 1'b0;
    jtag_tdi_i = 1'b0;
    jtag_tdo_i = 1'b0;
    rv_jtag_tck_i = 1'b0;
    rv_jtag_tms_i = 1'b0;
    rv_jtag_tdi_i = 1'b0;
    rv_jtag_tdo_i = 1'b0;
    can_core_enabled_i = 1'b0;
    can_rx_i = 1'b1;
    can_tx_i = 1'b1;
    i2c_core_enabled_i = 1'b0;
    i2c_scl_i = 1'b1;
    i2c_sda_i = 1'b1;
    eth_core_enabled_i = 1'b0;
    eth_tx_en_i = 1'b0;
    eth_tx_er_i = 1'b0;
    eth_rx_dv_i = 1'b0;
    eth_rx_er_i = 1'b0;
    eth_txd_i = 8'd0;
    eth_rxd_i = 8'd0;
    failures_r = 0;

    repeat (4) @(posedge clk);
    rst_n = 1'b1;
    repeat (4) @(posedge clk);

    for (cycle_r = 0; cycle_r < 128; cycle_r = cycle_r + 1) begin
      @(negedge clk);
      sample_abs_index_i = sample_abs_index_i + 32'd1;
      @(posedge clk);
      #2;
      if (probe_o[223:32] !== {32'h00000000, 32'h00000000,
                               32'h00000000, 32'h00000003,
                               32'h00000003, 32'h00000008}) begin
        fail_check("disabled protocols generated activity from sample index");
      end
      if (probe_o[256 + 11] !== 1'b0) begin
        fail_check("loopback activity status must remain zero");
      end
    end

    spi_core_enabled_i = 1'b1;
    spi_cs_n_i = 1'b0;
    spi_sclk_i = 1'b1;
    spi_mosi_i = 1'b1;
    spi_miso_i = 1'b0;
    repeat (3) @(posedge clk);
    #2;
    if (probe_o[32] !== 1'b1 || probe_o[33] !== 1'b1 ||
        probe_o[34] !== 1'b0 || probe_o[35] !== 1'b0) begin
      fail_check("SPI probe did not follow real input lines");
    end
    if (probe_o[256 + 13] !== 1'b1) begin
      fail_check("SPI real-source capability was not set");
    end

    if (failures_r == 0) begin
      $display("PASS tb_lockstep_protocol_probe_real_only");
    end else begin
      $display("FAIL tb_lockstep_protocol_probe_real_only failures=%0d", failures_r);
    end
    $finish;
  end
endmodule
