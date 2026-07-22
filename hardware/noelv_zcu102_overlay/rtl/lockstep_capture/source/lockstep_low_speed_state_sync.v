/**********************************************************
* 文件名: lockstep_low_speed_state_sync.v
* 日期: 2026-07-22
* 版本: 1.0
* 更新记录: 初版，替代 512-bit 低速协议逐拍探针。
* 描述: 将真实 UART、SPI、CAN、I2C 和 RV-JTAG 线同步到 sample 时钟域，
*       输出固定 16-bit 变化检测状态。
**********************************************************/

`timescale 1ns/1ps

module lockstep_low_speed_state_sync (
  clk,
  rst_n,
  uart_rx_i,
  uart_tx_i,
  uart_ctsn_i,
  uart_rtsn_i,
  spi_core_enabled_i,
  spi_sclk_i,
  spi_mosi_i,
  spi_miso_i,
  spi_cs_n_i,
  can_core_enabled_i,
  can_rx_i,
  can_tx_i,
  i2c_core_enabled_i,
  i2c_scl_i,
  i2c_sda_i,
  rv_jtag_tck_i,
  rv_jtag_tms_i,
  rv_jtag_tdi_i,
  rv_jtag_tdo_i,
  state_o
);
  parameter UDLY = 1;

  input         clk;
  input         rst_n;
  input         uart_rx_i;
  input         uart_tx_i;
  input         uart_ctsn_i;
  input         uart_rtsn_i;
  input         spi_core_enabled_i;
  input         spi_sclk_i;
  input         spi_mosi_i;
  input         spi_miso_i;
  input         spi_cs_n_i;
  input         can_core_enabled_i;
  input         can_rx_i;
  input         can_tx_i;
  input         i2c_core_enabled_i;
  input         i2c_scl_i;
  input         i2c_sda_i;
  input         rv_jtag_tck_i;
  input         rv_jtag_tms_i;
  input         rv_jtag_tdi_i;
  input         rv_jtag_tdo_i;
  output [15:0] state_o;

  (* ASYNC_REG = "TRUE" *) reg [15:0] state_d1_r;
  (* ASYNC_REG = "TRUE" *) reg [15:0] state_d2_r;
  wire [15:0] input_state_w;

  assign input_state_w[0] = uart_tx_i;
  assign input_state_w[1] = uart_rx_i;
  assign input_state_w[2] = uart_ctsn_i;
  assign input_state_w[3] = uart_rtsn_i;
  assign input_state_w[4] = spi_core_enabled_i ? spi_sclk_i : 1'b0;
  assign input_state_w[5] = spi_core_enabled_i ? spi_mosi_i : 1'b0;
  assign input_state_w[6] = spi_core_enabled_i ? spi_miso_i : 1'b0;
  assign input_state_w[7] = spi_core_enabled_i ? spi_cs_n_i : 1'b1;
  assign input_state_w[8] = can_core_enabled_i ? can_rx_i : 1'b1;
  assign input_state_w[9] = can_core_enabled_i ? can_tx_i : 1'b1;
  assign input_state_w[10] = i2c_core_enabled_i ? i2c_scl_i : 1'b1;
  assign input_state_w[11] = i2c_core_enabled_i ? i2c_sda_i : 1'b1;
  assign input_state_w[12] = rv_jtag_tck_i;
  assign input_state_w[13] = rv_jtag_tms_i;
  assign input_state_w[14] = rv_jtag_tdi_i;
  assign input_state_w[15] = rv_jtag_tdo_i;
  assign state_o = state_d2_r;

  // 每根异步协议线独立经过两级同步，时间戳取第二级可见的 sample index。
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      state_d1_r <= #UDLY 16'b0010_1111_1000_1111;
      state_d2_r <= #UDLY 16'b0010_1111_1000_1111;
    end else begin
      state_d1_r <= #UDLY input_state_w;
      state_d2_r <= #UDLY state_d1_r;
    end
  end

endmodule
