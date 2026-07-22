/**********************************************************
* 文件名: lockstep_protocol_probe_pack.v
* 日期: 2026-07-17
* 版本: 1.0
* 更新记录: 删除所有片上伪协议源，只保留真实连接信号和能力状态。
* 描述: 将 NOEL-V 的 UART、SPI、CAN、I2C、ETH 和 JTAG 真实探针
*       打包到 1024-bit 采样合同的高 512-bit；未实例化的 USB 与
*       未启用协议保持静态，供上位机标记 design_gap。
**********************************************************/

`timescale 1ns/1ps

module lockstep_protocol_probe_pack (
  clk,
  rst_n,
  sample_valid_i,
  sample_abs_index_i,
  uart_rx_i,
  uart_tx_i,
  uart_ctsn_i,
  uart_rtsn_i,
  spi_core_enabled_i,
  spi_sclk_i,
  spi_mosi_i,
  spi_miso_i,
  spi_cs_n_i,
  jtag_tck_i,
  jtag_tms_i,
  jtag_tdi_i,
  jtag_tdo_i,
  rv_jtag_tck_i,
  rv_jtag_tms_i,
  rv_jtag_tdi_i,
  rv_jtag_tdo_i,
  can_core_enabled_i,
  can_rx_i,
  can_tx_i,
  i2c_core_enabled_i,
  i2c_scl_i,
  i2c_sda_i,
  eth_core_enabled_i,
  eth_tx_en_i,
  eth_tx_er_i,
  eth_rx_dv_i,
  eth_rx_er_i,
  eth_txd_i,
  eth_rxd_i,
  probe_o
);
  parameter UDLY = 1;

  input           clk;
  input           rst_n;
  input           sample_valid_i;
  input  [31:0]   sample_abs_index_i;
  input           uart_rx_i;
  input           uart_tx_i;
  input           uart_ctsn_i;
  input           uart_rtsn_i;
  input           spi_core_enabled_i;
  input           spi_sclk_i;
  input           spi_mosi_i;
  input           spi_miso_i;
  input           spi_cs_n_i;
  input           jtag_tck_i;
  input           jtag_tms_i;
  input           jtag_tdi_i;
  input           jtag_tdo_i;
  input           rv_jtag_tck_i;
  input           rv_jtag_tms_i;
  input           rv_jtag_tdi_i;
  input           rv_jtag_tdo_i;
  input           can_core_enabled_i;
  input           can_rx_i;
  input           can_tx_i;
  input           i2c_core_enabled_i;
  input           i2c_scl_i;
  input           i2c_sda_i;
  input           eth_core_enabled_i;
  input           eth_tx_en_i;
  input           eth_tx_er_i;
  input           eth_rx_dv_i;
  input           eth_rx_er_i;
  input  [7:0]    eth_txd_i;
  input  [7:0]    eth_rxd_i;
  output [511:0]  probe_o;

  localparam integer UART_BASE = 0;
  localparam integer SPI_BASE = 32;
  localparam integer CAN_BASE = 64;
  localparam integer I2C_BASE = 96;
  localparam integer ETH_BASE = 128;
  localparam integer USB_BASE = 192;
  localparam integer JTAG_BASE = 224;
  localparam integer STATUS_BASE = 256;

  reg uart_rx_d1_r;
  reg uart_rx_d2_r;
  reg uart_rx_prev_r;
  reg uart_tx_d1_r;
  reg uart_tx_d2_r;
  reg uart_tx_prev_r;
  reg uart_ctsn_d1_r;
  reg uart_ctsn_d2_r;
  reg uart_rtsn_d1_r;
  reg uart_rtsn_d2_r;
  reg spi_sclk_d1_r;
  reg spi_sclk_d2_r;
  reg spi_sclk_prev_r;
  reg spi_mosi_d1_r;
  reg spi_mosi_d2_r;
  reg spi_miso_d1_r;
  reg spi_miso_d2_r;
  reg spi_cs_n_d1_r;
  reg spi_cs_n_d2_r;
  reg spi_cs_n_prev_r;
  reg can_rx_d1_r;
  reg can_rx_d2_r;
  reg can_rx_prev_r;
  reg can_tx_d1_r;
  reg can_tx_d2_r;
  reg can_tx_prev_r;
  reg i2c_scl_d1_r;
  reg i2c_scl_d2_r;
  reg i2c_scl_prev_r;
  reg i2c_sda_d1_r;
  reg i2c_sda_d2_r;
  reg i2c_sda_prev_r;
  reg eth_tx_en_d1_r;
  reg eth_tx_en_d2_r;
  reg eth_tx_en_prev_r;
  reg eth_tx_er_d1_r;
  reg eth_tx_er_d2_r;
  reg eth_rx_dv_d1_r;
  reg eth_rx_dv_d2_r;
  reg eth_rx_dv_prev_r;
  reg eth_rx_er_d1_r;
  reg eth_rx_er_d2_r;
  reg [7:0] eth_txd_d1_r;
  reg [7:0] eth_txd_d2_r;
  reg [7:0] eth_rxd_d1_r;
  reg [7:0] eth_rxd_d2_r;
  reg jtag_tck_d1_r;
  reg jtag_tck_d2_r;
  reg jtag_tck_prev_r;
  reg jtag_tms_d1_r;
  reg jtag_tms_d2_r;
  reg jtag_tdi_d1_r;
  reg jtag_tdi_d2_r;
  reg jtag_tdo_d1_r;
  reg jtag_tdo_d2_r;
  reg rv_jtag_tck_d1_r;
  reg rv_jtag_tck_d2_r;
  reg rv_jtag_tck_prev_r;
  reg rv_jtag_tms_d1_r;
  reg rv_jtag_tms_d2_r;
  reg rv_jtag_tdi_d1_r;
  reg rv_jtag_tdi_d2_r;
  reg rv_jtag_tdo_d1_r;
  reg rv_jtag_tdo_d2_r;
  reg [511:0] probe_r;

  wire uart_tx_toggle_w;
  wire uart_rx_toggle_w;
  wire spi_sclk_rise_w;
  wire spi_activity_w;
  wire can_activity_w;
  wire i2c_scl_rise_w;
  wire i2c_start_w;
  wire i2c_stop_w;
  wire i2c_activity_w;
  wire eth_frame_start_w;
  wire eth_frame_end_w;
  wire eth_activity_w;
  wire jtag_tck_rise_w;
  wire rv_jtag_tck_rise_w;
  wire jtag_activity_w;
  wire real_activity_w;

  assign probe_o = probe_r;
  assign uart_tx_toggle_w = uart_tx_prev_r ^ uart_tx_d2_r;
  assign uart_rx_toggle_w = uart_rx_prev_r ^ uart_rx_d2_r;
  assign spi_sclk_rise_w = !spi_sclk_prev_r && spi_sclk_d2_r;
  assign spi_activity_w = spi_core_enabled_i &&
                          ((!spi_cs_n_d2_r) || (spi_sclk_prev_r ^ spi_sclk_d2_r));
  assign can_activity_w = can_core_enabled_i &&
                          ((can_rx_prev_r ^ can_rx_d2_r) || (can_tx_prev_r ^ can_tx_d2_r));
  assign i2c_scl_rise_w = !i2c_scl_prev_r && i2c_scl_d2_r;
  assign i2c_start_w = i2c_sda_prev_r && !i2c_sda_d2_r && i2c_scl_d2_r;
  assign i2c_stop_w = !i2c_sda_prev_r && i2c_sda_d2_r && i2c_scl_d2_r;
  assign i2c_activity_w = i2c_core_enabled_i &&
                          ((i2c_scl_prev_r ^ i2c_scl_d2_r) ||
                           (i2c_sda_prev_r ^ i2c_sda_d2_r));
  assign eth_frame_start_w = !eth_tx_en_prev_r && eth_tx_en_d2_r;
  assign eth_frame_end_w = eth_tx_en_prev_r && !eth_tx_en_d2_r;
  assign eth_activity_w = eth_core_enabled_i &&
                          (eth_tx_en_d2_r || eth_rx_dv_d2_r || eth_tx_er_d2_r || eth_rx_er_d2_r);
  assign jtag_tck_rise_w = !jtag_tck_prev_r && jtag_tck_d2_r;
  assign rv_jtag_tck_rise_w = !rv_jtag_tck_prev_r && rv_jtag_tck_d2_r;
  assign jtag_activity_w = (jtag_tck_prev_r ^ jtag_tck_d2_r) ||
                           (rv_jtag_tck_prev_r ^ rv_jtag_tck_d2_r);
  assign real_activity_w = uart_tx_toggle_w || uart_rx_toggle_w || spi_activity_w ||
                           can_activity_w || i2c_activity_w || eth_activity_w || jtag_activity_w;

  // 所有外部协议线在采样时钟域内经过两级同步，避免异步输入直接进入打包逻辑。
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      uart_rx_d1_r <= #UDLY 1'b1;
      uart_rx_d2_r <= #UDLY 1'b1;
      uart_rx_prev_r <= #UDLY 1'b1;
      uart_tx_d1_r <= #UDLY 1'b1;
      uart_tx_d2_r <= #UDLY 1'b1;
      uart_tx_prev_r <= #UDLY 1'b1;
      uart_ctsn_d1_r <= #UDLY 1'b1;
      uart_ctsn_d2_r <= #UDLY 1'b1;
      uart_rtsn_d1_r <= #UDLY 1'b1;
      uart_rtsn_d2_r <= #UDLY 1'b1;
      spi_sclk_d1_r <= #UDLY 1'b0;
      spi_sclk_d2_r <= #UDLY 1'b0;
      spi_sclk_prev_r <= #UDLY 1'b0;
      spi_mosi_d1_r <= #UDLY 1'b0;
      spi_mosi_d2_r <= #UDLY 1'b0;
      spi_miso_d1_r <= #UDLY 1'b0;
      spi_miso_d2_r <= #UDLY 1'b0;
      spi_cs_n_d1_r <= #UDLY 1'b1;
      spi_cs_n_d2_r <= #UDLY 1'b1;
      spi_cs_n_prev_r <= #UDLY 1'b1;
      can_rx_d1_r <= #UDLY 1'b1;
      can_rx_d2_r <= #UDLY 1'b1;
      can_rx_prev_r <= #UDLY 1'b1;
      can_tx_d1_r <= #UDLY 1'b1;
      can_tx_d2_r <= #UDLY 1'b1;
      can_tx_prev_r <= #UDLY 1'b1;
      i2c_scl_d1_r <= #UDLY 1'b1;
      i2c_scl_d2_r <= #UDLY 1'b1;
      i2c_scl_prev_r <= #UDLY 1'b1;
      i2c_sda_d1_r <= #UDLY 1'b1;
      i2c_sda_d2_r <= #UDLY 1'b1;
      i2c_sda_prev_r <= #UDLY 1'b1;
      eth_tx_en_d1_r <= #UDLY 1'b0;
      eth_tx_en_d2_r <= #UDLY 1'b0;
      eth_tx_en_prev_r <= #UDLY 1'b0;
      eth_tx_er_d1_r <= #UDLY 1'b0;
      eth_tx_er_d2_r <= #UDLY 1'b0;
      eth_rx_dv_d1_r <= #UDLY 1'b0;
      eth_rx_dv_d2_r <= #UDLY 1'b0;
      eth_rx_dv_prev_r <= #UDLY 1'b0;
      eth_rx_er_d1_r <= #UDLY 1'b0;
      eth_rx_er_d2_r <= #UDLY 1'b0;
      eth_txd_d1_r <= #UDLY 8'd0;
      eth_txd_d2_r <= #UDLY 8'd0;
      eth_rxd_d1_r <= #UDLY 8'd0;
      eth_rxd_d2_r <= #UDLY 8'd0;
      jtag_tck_d1_r <= #UDLY 1'b0;
      jtag_tck_d2_r <= #UDLY 1'b0;
      jtag_tck_prev_r <= #UDLY 1'b0;
      jtag_tms_d1_r <= #UDLY 1'b1;
      jtag_tms_d2_r <= #UDLY 1'b1;
      jtag_tdi_d1_r <= #UDLY 1'b0;
      jtag_tdi_d2_r <= #UDLY 1'b0;
      jtag_tdo_d1_r <= #UDLY 1'b0;
      jtag_tdo_d2_r <= #UDLY 1'b0;
      rv_jtag_tck_d1_r <= #UDLY 1'b0;
      rv_jtag_tck_d2_r <= #UDLY 1'b0;
      rv_jtag_tck_prev_r <= #UDLY 1'b0;
      rv_jtag_tms_d1_r <= #UDLY 1'b1;
      rv_jtag_tms_d2_r <= #UDLY 1'b1;
      rv_jtag_tdi_d1_r <= #UDLY 1'b0;
      rv_jtag_tdi_d2_r <= #UDLY 1'b0;
      rv_jtag_tdo_d1_r <= #UDLY 1'b0;
      rv_jtag_tdo_d2_r <= #UDLY 1'b0;
    end else begin
      uart_rx_d1_r <= #UDLY uart_rx_i;
      uart_rx_d2_r <= #UDLY uart_rx_d1_r;
      uart_rx_prev_r <= #UDLY uart_rx_d2_r;
      uart_tx_d1_r <= #UDLY uart_tx_i;
      uart_tx_d2_r <= #UDLY uart_tx_d1_r;
      uart_tx_prev_r <= #UDLY uart_tx_d2_r;
      uart_ctsn_d1_r <= #UDLY uart_ctsn_i;
      uart_ctsn_d2_r <= #UDLY uart_ctsn_d1_r;
      uart_rtsn_d1_r <= #UDLY uart_rtsn_i;
      uart_rtsn_d2_r <= #UDLY uart_rtsn_d1_r;
      spi_sclk_d1_r <= #UDLY spi_sclk_i;
      spi_sclk_d2_r <= #UDLY spi_sclk_d1_r;
      spi_sclk_prev_r <= #UDLY spi_sclk_d2_r;
      spi_mosi_d1_r <= #UDLY spi_mosi_i;
      spi_mosi_d2_r <= #UDLY spi_mosi_d1_r;
      spi_miso_d1_r <= #UDLY spi_miso_i;
      spi_miso_d2_r <= #UDLY spi_miso_d1_r;
      spi_cs_n_d1_r <= #UDLY spi_cs_n_i;
      spi_cs_n_d2_r <= #UDLY spi_cs_n_d1_r;
      spi_cs_n_prev_r <= #UDLY spi_cs_n_d2_r;
      can_rx_d1_r <= #UDLY can_rx_i;
      can_rx_d2_r <= #UDLY can_rx_d1_r;
      can_rx_prev_r <= #UDLY can_rx_d2_r;
      can_tx_d1_r <= #UDLY can_tx_i;
      can_tx_d2_r <= #UDLY can_tx_d1_r;
      can_tx_prev_r <= #UDLY can_tx_d2_r;
      i2c_scl_d1_r <= #UDLY i2c_scl_i;
      i2c_scl_d2_r <= #UDLY i2c_scl_d1_r;
      i2c_scl_prev_r <= #UDLY i2c_scl_d2_r;
      i2c_sda_d1_r <= #UDLY i2c_sda_i;
      i2c_sda_d2_r <= #UDLY i2c_sda_d1_r;
      i2c_sda_prev_r <= #UDLY i2c_sda_d2_r;
      eth_tx_en_d1_r <= #UDLY eth_tx_en_i;
      eth_tx_en_d2_r <= #UDLY eth_tx_en_d1_r;
      eth_tx_en_prev_r <= #UDLY eth_tx_en_d2_r;
      eth_tx_er_d1_r <= #UDLY eth_tx_er_i;
      eth_tx_er_d2_r <= #UDLY eth_tx_er_d1_r;
      eth_rx_dv_d1_r <= #UDLY eth_rx_dv_i;
      eth_rx_dv_d2_r <= #UDLY eth_rx_dv_d1_r;
      eth_rx_dv_prev_r <= #UDLY eth_rx_dv_d2_r;
      eth_rx_er_d1_r <= #UDLY eth_rx_er_i;
      eth_rx_er_d2_r <= #UDLY eth_rx_er_d1_r;
      eth_txd_d1_r <= #UDLY eth_txd_i;
      eth_txd_d2_r <= #UDLY eth_txd_d1_r;
      eth_rxd_d1_r <= #UDLY eth_rxd_i;
      eth_rxd_d2_r <= #UDLY eth_rxd_d1_r;
      jtag_tck_d1_r <= #UDLY jtag_tck_i;
      jtag_tck_d2_r <= #UDLY jtag_tck_d1_r;
      jtag_tck_prev_r <= #UDLY jtag_tck_d2_r;
      jtag_tms_d1_r <= #UDLY jtag_tms_i;
      jtag_tms_d2_r <= #UDLY jtag_tms_d1_r;
      jtag_tdi_d1_r <= #UDLY jtag_tdi_i;
      jtag_tdi_d2_r <= #UDLY jtag_tdi_d1_r;
      jtag_tdo_d1_r <= #UDLY jtag_tdo_i;
      jtag_tdo_d2_r <= #UDLY jtag_tdo_d1_r;
      rv_jtag_tck_d1_r <= #UDLY rv_jtag_tck_i;
      rv_jtag_tck_d2_r <= #UDLY rv_jtag_tck_d1_r;
      rv_jtag_tck_prev_r <= #UDLY rv_jtag_tck_d2_r;
      rv_jtag_tms_d1_r <= #UDLY rv_jtag_tms_i;
      rv_jtag_tms_d2_r <= #UDLY rv_jtag_tms_d1_r;
      rv_jtag_tdi_d1_r <= #UDLY rv_jtag_tdi_i;
      rv_jtag_tdi_d2_r <= #UDLY rv_jtag_tdi_d1_r;
      rv_jtag_tdo_d1_r <= #UDLY rv_jtag_tdo_i;
      rv_jtag_tdo_d2_r <= #UDLY rv_jtag_tdo_d1_r;
    end
  end

  // 固定布局只描述真实线级值；解码字段由上位机根据原始信号生成。
  always @(*) begin
    probe_r = 512'd0;

    probe_r[UART_BASE + 0] = uart_tx_d2_r;
    probe_r[UART_BASE + 1] = uart_rx_d2_r;
    probe_r[UART_BASE + 2] = uart_ctsn_d2_r;
    probe_r[UART_BASE + 3] = uart_rtsn_d2_r;
    probe_r[UART_BASE + 4] = uart_tx_prev_r && !uart_tx_d2_r;
    probe_r[UART_BASE + 5] = uart_rx_prev_r && !uart_rx_d2_r;
    probe_r[UART_BASE + 6] = uart_tx_toggle_w;
    probe_r[UART_BASE + 7] = uart_rx_toggle_w;

    probe_r[SPI_BASE + 0] = spi_core_enabled_i ? spi_sclk_d2_r : 1'b0;
    probe_r[SPI_BASE + 1] = spi_core_enabled_i ? spi_mosi_d2_r : 1'b0;
    probe_r[SPI_BASE + 2] = spi_core_enabled_i ? spi_miso_d2_r : 1'b0;
    probe_r[SPI_BASE + 3] = spi_core_enabled_i ? spi_cs_n_d2_r : 1'b1;
    probe_r[SPI_BASE + 4] = spi_core_enabled_i && spi_cs_n_prev_r && !spi_cs_n_d2_r;
    probe_r[SPI_BASE + 5] = spi_core_enabled_i && spi_sclk_rise_w;
    probe_r[SPI_BASE + 31] = spi_activity_w;

    probe_r[CAN_BASE + 0] = can_core_enabled_i ? can_rx_d2_r : 1'b1;
    probe_r[CAN_BASE + 1] = can_core_enabled_i ? can_tx_d2_r : 1'b1;
    probe_r[CAN_BASE + 4] = can_core_enabled_i && !can_rx_d2_r;
    probe_r[CAN_BASE + 31] = can_activity_w;

    probe_r[I2C_BASE + 0] = i2c_core_enabled_i ? i2c_scl_d2_r : 1'b1;
    probe_r[I2C_BASE + 1] = i2c_core_enabled_i ? i2c_sda_d2_r : 1'b1;
    probe_r[I2C_BASE + 2] = i2c_core_enabled_i && i2c_start_w;
    probe_r[I2C_BASE + 3] = i2c_core_enabled_i && i2c_stop_w;
    probe_r[I2C_BASE + 6] = i2c_core_enabled_i && i2c_scl_rise_w;
    probe_r[I2C_BASE + 29] = i2c_activity_w;
    probe_r[I2C_BASE + 30] = i2c_core_enabled_i && (!i2c_scl_d2_r || !i2c_sda_d2_r);

    probe_r[ETH_BASE + 0] = eth_core_enabled_i && eth_tx_en_d2_r;
    probe_r[ETH_BASE + 1] = eth_core_enabled_i && eth_tx_er_d2_r;
    probe_r[ETH_BASE + 2] = eth_core_enabled_i && eth_rx_dv_d2_r;
    probe_r[ETH_BASE + 3] = eth_core_enabled_i && eth_rx_er_d2_r;
    probe_r[ETH_BASE + 6] = eth_core_enabled_i && eth_frame_start_w;
    probe_r[ETH_BASE + 7] = eth_core_enabled_i && eth_frame_end_w;
    probe_r[ETH_BASE + 15:ETH_BASE + 8] = eth_core_enabled_i ? eth_txd_d2_r : 8'd0;
    probe_r[ETH_BASE + 23:ETH_BASE + 16] = eth_core_enabled_i ? eth_rxd_d2_r : 8'd0;

    probe_r[USB_BASE + 31:USB_BASE] = 32'd0;

    probe_r[JTAG_BASE + 0] = jtag_tck_d2_r;
    probe_r[JTAG_BASE + 1] = jtag_tms_d2_r;
    probe_r[JTAG_BASE + 2] = jtag_tdi_d2_r;
    probe_r[JTAG_BASE + 3] = jtag_tdo_d2_r;
    probe_r[JTAG_BASE + 4] = rv_jtag_tck_d2_r;
    probe_r[JTAG_BASE + 5] = rv_jtag_tms_d2_r;
    probe_r[JTAG_BASE + 6] = rv_jtag_tdi_d2_r;
    probe_r[JTAG_BASE + 7] = rv_jtag_tdo_d2_r;
    probe_r[JTAG_BASE + 8] = jtag_tck_rise_w;
    probe_r[JTAG_BASE + 9] = rv_jtag_tck_rise_w;
    probe_r[JTAG_BASE + 10] = jtag_tck_prev_r ^ jtag_tck_d2_r;
    probe_r[JTAG_BASE + 11] = rv_jtag_tck_prev_r ^ rv_jtag_tck_d2_r;
    probe_r[JTAG_BASE + 30] = jtag_activity_w;
    probe_r[JTAG_BASE + 31] = 1'b1;

    probe_r[STATUS_BASE + 0] = 1'b1;
    probe_r[STATUS_BASE + 1] = 1'b0;
    probe_r[STATUS_BASE + 2] = can_core_enabled_i;
    probe_r[STATUS_BASE + 3] = 1'b0;
    probe_r[STATUS_BASE + 4] = 1'b0;
    probe_r[STATUS_BASE + 5] = eth_core_enabled_i;
    probe_r[STATUS_BASE + 6] = 1'b0;
    probe_r[STATUS_BASE + 7] = 1'b0;
    probe_r[STATUS_BASE + 8] = 1'b1;
    probe_r[STATUS_BASE + 9] = real_activity_w;
    probe_r[STATUS_BASE + 10] = real_activity_w;
    probe_r[STATUS_BASE + 11] = 1'b0;
    probe_r[STATUS_BASE + 12] = sample_valid_i;
    probe_r[STATUS_BASE + 13] = spi_core_enabled_i;
    probe_r[STATUS_BASE + 14] = i2c_core_enabled_i;
    probe_r[STATUS_BASE + 31:STATUS_BASE + 15] = 17'd0;

    probe_r[511:288] = 224'd0;
  end

endmodule
