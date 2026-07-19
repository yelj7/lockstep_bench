/**********************************************************
* 文件名: tb_lockstep_ft601_hello.v
* 日期: 2026-07-17
* 版本: 1.0
* 更新记录: 新增 FT601 同步 FIFO 边界的 HELLO 往返测试。
* 描述: 模拟主机向 FT601 写入完整 HELLO_REQ，验证采样域保持复位时
*       FT601 协议域仍能解析命令并返回 HELLO_RSP。
**********************************************************/

`timescale 1ns/1ps

module tb_lockstep_ft601_hello;
  reg           ft_clk;
  reg           rst_n;
  reg           sample_clk;
  reg           sample_rst_n;
  reg           sample_valid_i;
  reg  [1023:0] sample_i;
  reg  [31:0]   sample_abs_index_i;
  reg           ft_txe_n_i;
  wire          ft_rxf_n_i;
  wire [31:0]   ft_data_io;
  wire [3:0]    ft_be_io;
  wire          ft_oe_n_o;
  wire          ft_rd_n_o;
  wire          ft_wr_n_o;
  wire          ft_siwu_n_o;
  wire          ft_wakeup_n_o;

  reg  [31:0] host_tx_words_r [0:7];
  reg  [31:0] device_rx_words_r [0:31];
  integer      host_tx_index_r;
  integer      device_rx_count_r;
  integer      failures_r;

  assign ft_rxf_n_i = (host_tx_index_r < 8) ? 1'b0 : 1'b1;
  assign ft_data_io = (!ft_oe_n_o && (host_tx_index_r < 8)) ?
                      host_tx_words_r[host_tx_index_r] : 32'hzzzzzzzz;
  assign ft_be_io = (!ft_oe_n_o && (host_tx_index_r < 8)) ? 4'hf : 4'hz;

  lockstep_ft601_external_sample_top dut (
    .ft_clk             (ft_clk),
    .rst_n              (rst_n),
    .sample_clk         (sample_clk),
    .sample_rst_n       (sample_rst_n),
    .sample_valid_i     (sample_valid_i),
    .sample_i           (sample_i),
    .sample_abs_index_i (sample_abs_index_i),
    .ft_txe_n_i         (ft_txe_n_i),
    .ft_rxf_n_i         (ft_rxf_n_i),
    .ft_data_io         (ft_data_io),
    .ft_be_io           (ft_be_io),
    .ft_oe_n_o          (ft_oe_n_o),
    .ft_rd_n_o          (ft_rd_n_o),
    .ft_wr_n_o          (ft_wr_n_o),
    .ft_siwu_n_o        (ft_siwu_n_o),
    .ft_wakeup_n_o      (ft_wakeup_n_o),
    .debug_ft601_state_o()
  );

  initial begin
    ft_clk = 1'b0;
    forever #7.5 ft_clk = !ft_clk;
  end

  initial begin
    sample_clk = 1'b0;
    forever #4.166 sample_clk = !sample_clk;
  end

  task fail_check;
    input [1023:0] message_i;
    begin
      failures_r = failures_r + 1;
      $display("FAIL time=%0t message=%0s host_words=%0d device_words=%0d",
               $time, message_i, host_tx_index_r, device_rx_count_r);
    end
  endtask

  always @(posedge ft_clk) begin
    if (rst_n && !ft_rd_n_o && !ft_oe_n_o && (host_tx_index_r < 8)) begin
      host_tx_index_r <= host_tx_index_r + 1;
    end
    if (rst_n && !ft_wr_n_o && !ft_txe_n_i) begin
      if (device_rx_count_r < 32) begin
        device_rx_words_r[device_rx_count_r] <= ft_data_io;
      end
      device_rx_count_r <= device_rx_count_r + 1;
    end
    if (!ft_rd_n_o && !ft_wr_n_o) begin
      fail_check("FT601 RD# and WR# overlapped");
    end
  end

  initial begin
    host_tx_words_r[0] = 32'h3243534c;
    host_tx_words_r[1] = 32'h00010002;
    host_tx_words_r[2] = 32'd32;
    host_tx_words_r[3] = 32'd0;
    host_tx_words_r[4] = 32'd0;
    host_tx_words_r[5] = 32'd0;
    host_tx_words_r[6] = 32'd0;
    host_tx_words_r[7] = 32'hdc581e56;

    rst_n = 1'b0;
    sample_rst_n = 1'b0;
    sample_valid_i = 1'b0;
    sample_i = 1024'd0;
    sample_abs_index_i = 32'd0;
    ft_txe_n_i = 1'b0;
    host_tx_index_r = 0;
    device_rx_count_r = 0;
    failures_r = 0;

    repeat (5) @(posedge ft_clk);
    rst_n = 1'b1;

    repeat (500) begin
      @(posedge ft_clk);
      if (device_rx_count_r >= 16) begin
        if (device_rx_words_r[0] !== 32'h3243534c) begin
          fail_check("HELLO_RSP magic mismatch");
        end
        if (device_rx_words_r[1] !== 32'h80010002) begin
          fail_check("HELLO_RSP version/type mismatch");
        end
        if (device_rx_words_r[2] !== 32'd32) begin
          fail_check("HELLO_RSP header length mismatch");
        end
        if (device_rx_words_r[3] !== 32'd32) begin
          fail_check("HELLO_RSP payload length mismatch");
        end
        if (device_rx_words_r[8] !== 32'd2) begin
          fail_check("HELLO_RSP protocol version mismatch");
        end
        if (device_rx_words_r[9] !== 32'd1024) begin
          fail_check("HELLO_RSP physical channel count mismatch");
        end
        if (device_rx_words_r[10] !== 32'd1024) begin
          fail_check("HELLO_RSP sample word width mismatch");
        end
        if (failures_r == 0) begin
          $display("PASS tb_lockstep_ft601_hello");
        end else begin
          $display("FAIL tb_lockstep_ft601_hello failures=%0d", failures_r);
        end
        $finish;
      end
    end

    fail_check("timed out waiting for HELLO_RSP");
    $finish;
  end
endmodule
