/**********************************************************
* 文件名: tb_lockstep_low_speed_state_sync.v
* 日期: 2026-07-22
* 版本: 1.0
* 更新记录: 初版，验证真实低速探针映射、同步延迟和 disabled idle 值。
* 描述: 检查 UART、SPI、CAN、I2C、RV-JTAG 的固定 16-bit 状态合同。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_low_speed_state_sync;
  reg clk;
  reg rst_n;
  reg [15:0] raw_state;
  reg spi_enabled;
  reg can_enabled;
  reg i2c_enabled;
  wire [15:0] state;

  lockstep_low_speed_state_sync dut (
    .clk(clk),
    .rst_n(rst_n),
    .uart_tx_i(raw_state[0]),
    .uart_rx_i(raw_state[1]),
    .uart_ctsn_i(raw_state[2]),
    .uart_rtsn_i(raw_state[3]),
    .spi_core_enabled_i(spi_enabled),
    .spi_sclk_i(raw_state[4]),
    .spi_mosi_i(raw_state[5]),
    .spi_miso_i(raw_state[6]),
    .spi_cs_n_i(raw_state[7]),
    .can_core_enabled_i(can_enabled),
    .can_rx_i(raw_state[8]),
    .can_tx_i(raw_state[9]),
    .i2c_core_enabled_i(i2c_enabled),
    .i2c_scl_i(raw_state[10]),
    .i2c_sda_i(raw_state[11]),
    .rv_jtag_tck_i(raw_state[12]),
    .rv_jtag_tms_i(raw_state[13]),
    .rv_jtag_tdi_i(raw_state[14]),
    .rv_jtag_tdo_i(raw_state[15]),
    .state_o(state)
  );

  always #5 clk = !clk;

  task fail;
    input [1023:0] message_i;
    begin
      $display("FAIL time=%0t message=%0s state=%04x raw=%04x", $time, message_i, state, raw_state);
      $finish;
    end
  endtask

  task wait_sync;
    begin
      repeat (2) @(posedge clk);
      #2;
    end
  endtask

  initial begin
    #10000;
    $display("FAIL tb_lockstep_low_speed_state_sync timeout");
    $finish;
  end

  initial begin
    clk = 1'b0;
    rst_n = 1'b0;
    raw_state = 16'h2f8f;
    spi_enabled = 1'b1;
    can_enabled = 1'b1;
    i2c_enabled = 1'b1;
    repeat (3) @(posedge clk);
    rst_n = 1'b1;
    wait_sync;
    if (state !== 16'h2f8f) fail("reset idle mapping mismatch");

    raw_state = 16'ha55a;
    @(posedge clk);
    #2;
    if (state === 16'ha55a) fail("second synchronizer stage was bypassed");
    @(posedge clk);
    #2;
    if (state !== 16'ha55a) fail("16-bit real-probe mapping mismatch");

    raw_state = 16'hffff;
    spi_enabled = 1'b0;
    can_enabled = 1'b0;
    i2c_enabled = 1'b0;
    wait_sync;
    if (state[7:4] !== 4'b1000 || state[9:8] !== 2'b11 || state[11:10] !== 2'b11)
      fail("disabled protocol idle levels mismatch");
    if (state[3:0] !== 4'hf || state[15:12] !== 4'hf)
      fail("UART or RV-JTAG was incorrectly gated");

    $display("PASS tb_lockstep_low_speed_state_sync");
    $finish;
  end
endmodule
