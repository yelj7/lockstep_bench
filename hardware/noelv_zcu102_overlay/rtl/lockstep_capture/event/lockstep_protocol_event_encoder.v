/**********************************************************
* 文件名: lockstep_protocol_event_encoder.v
* 日期: 2026-07-19
* 版本: 1.3
* 更新记录: 输出协议线级 busy，供 program_done quiet guard 防止截断在途事务。
* 描述: 生成 AHB、UART、SPI、CAN、I2C、JTAG 和 mismatch 事件，ETH/USB 保持空。
**********************************************************/

`timescale 1ns/1ps

module lockstep_protocol_event_encoder (
  clk,
  rst_n,
  capture_start_i,
  capture_active_i,
  capture_id_i,
  timestamp_i,
  source_enable_mask_i,
  sample_valid_i,
  sample_i,
  source_push_o,
  source_record_o,
  protocol_busy_o
);
  parameter UDLY = 1;

  input             clk;
  input             rst_n;
  input             capture_start_i;
  input             capture_active_i;
  input  [31:0]     capture_id_i;
  input  [63:0]     timestamp_i;
  input  [8:0]      source_enable_mask_i;
  input             sample_valid_i;
  input  [1023:0]   sample_i;
  output [8:0]      source_push_o;
  output [9*512-1:0] source_record_o;
  output            protocol_busy_o;

  localparam integer PROBE_BASE = 512;
  localparam integer UART_BASE = PROBE_BASE + 0;
  localparam integer SPI_BASE = PROBE_BASE + 32;
  localparam integer CAN_BASE = PROBE_BASE + 64;
  localparam integer I2C_BASE = PROBE_BASE + 96;
  localparam integer JTAG_BASE = PROBE_BASE + 224;

  reg [31:0] local_sequence_r [0:8];
  reg [4:0] mismatch_prev_r;
  reg ahb_read_valid_r;
  reg [65:0] ahb_read_signature_r;
  wire [8:0] source_push_w;
  wire [255:0] ahb_payload_w;
  wire [255:0] uart_payload_w;
  wire [255:0] spi_payload_w;
  wire [255:0] can_payload_w;
  wire [255:0] i2c_payload_w;
  wire [255:0] jtag_payload_w;
  wire [255:0] mismatch_payload_w;
  wire [4:0] mismatch_current_w;
  wire [4:0] mismatch_change_w;
  wire [63:0] event_timestamp_w;
  wire ahb_data_transfer_w;
  wire [65:0] ahb_read_signature_w;
  wire ahb_read_changed_w;
  integer sequence_index;

  assign mismatch_current_w = sample_i[506:502];
  assign mismatch_change_w = mismatch_current_w ^ mismatch_prev_r;
  assign event_timestamp_w = timestamp_i;
  assign ahb_data_transfer_w = sample_i[442] && sample_i[429] && sample_i[425];
  assign ahb_read_signature_w = {sample_i[431:430], sample_i[223:192], sample_i[63:32]};
  assign ahb_read_changed_w = capture_start_i || !ahb_read_valid_r ||
                              (ahb_read_signature_w != ahb_read_signature_r);
  assign source_push_w[0] = (capture_active_i || capture_start_i) && sample_valid_i && source_enable_mask_i[0] &&
                            ahb_data_transfer_w && (sample_i[416] || ahb_read_changed_w);
  assign source_push_w[1] = (capture_active_i || capture_start_i) && sample_valid_i && source_enable_mask_i[1] &&
                            (sample_i[UART_BASE+6] || sample_i[UART_BASE+7]);
  assign source_push_w[2] = (capture_active_i || capture_start_i) && sample_valid_i && source_enable_mask_i[2] &&
                            sample_i[SPI_BASE+5];
  assign source_push_w[3] = (capture_active_i || capture_start_i) && sample_valid_i && source_enable_mask_i[3] &&
                            sample_i[CAN_BASE+31];
  assign source_push_w[4] = (capture_active_i || capture_start_i) && sample_valid_i && source_enable_mask_i[4] &&
                            (sample_i[I2C_BASE+2] || sample_i[I2C_BASE+3] ||
                             sample_i[I2C_BASE+6]);
  assign source_push_w[5] = 1'b0;
  assign source_push_w[6] = 1'b0;
  assign source_push_w[7] = (capture_active_i || capture_start_i) && sample_valid_i && source_enable_mask_i[7] &&
                            (sample_i[JTAG_BASE+8] || sample_i[JTAG_BASE+9]);
  assign source_push_w[8] = (capture_active_i || capture_start_i) && sample_valid_i && source_enable_mask_i[8] &&
                            (mismatch_change_w != 5'd0);
  assign source_push_o = source_push_w;
  assign protocol_busy_o = capture_active_i && sample_valid_i &&
                           ((source_enable_mask_i[1] &&
                             (!sample_i[UART_BASE] || !sample_i[UART_BASE+1])) ||
                            (source_enable_mask_i[2] && !sample_i[SPI_BASE+3]) ||
                            (source_enable_mask_i[3] &&
                             (!sample_i[CAN_BASE] || !sample_i[CAN_BASE+1])) ||
                            (source_enable_mask_i[4] && sample_i[I2C_BASE+30]) ||
                            (source_enable_mask_i[5] &&
                             (sample_i[PROBE_BASE+128] || sample_i[PROBE_BASE+130])) ||
                            (source_enable_mask_i[7] &&
                             (sample_i[JTAG_BASE] || sample_i[JTAG_BASE+4])));

  assign ahb_payload_w = {128'd0,
                          14'd0, sample_i[431:430], sample_i[436], sample_i[428:425],
                          sample_i[424:422], sample_i[421:419], sample_i[418:417],
                          sample_i[416], sample_i[429],
                          sample_i[223:192], sample_i[95:64], sample_i[63:32]};
  assign uart_payload_w = {224'd0, 24'd0, sample_i[UART_BASE+7:UART_BASE]};
  assign spi_payload_w = {224'd0, 24'd0, sample_i[SPI_BASE+7:SPI_BASE]};
  assign can_payload_w = {224'd0, 24'd0, sample_i[CAN_BASE+7:CAN_BASE]};
  assign i2c_payload_w = {224'd0, 24'd0, sample_i[I2C_BASE+7:I2C_BASE]};
  assign jtag_payload_w = {224'd0, 20'd0, sample_i[JTAG_BASE+11:JTAG_BASE]};
  assign mismatch_payload_w = {224'd0, 22'd0, mismatch_change_w, mismatch_current_w};

  lockstep_event_record_builder u_ahb_record (
    .timestamp_i(event_timestamp_w), .capture_id_i(capture_id_i),
    .local_sequence_i(capture_start_i ? 32'd0 : local_sequence_r[0]), .protocol_id_i(8'd0),
    .event_type_i(8'd1), .source_kind_i(8'd2), .flags_i(8'd0),
    .event_reason_mask_i(9'h001), .payload_length_i(32'd16),
    .payload_i(ahb_payload_w), .record_o(source_record_o[1*512-1:0*512])
  );
  lockstep_event_record_builder u_uart_record (
    .timestamp_i(event_timestamp_w), .capture_id_i(capture_id_i),
    .local_sequence_i(capture_start_i ? 32'd0 : local_sequence_r[1]), .protocol_id_i(8'd1),
    .event_type_i(8'd1), .source_kind_i(8'd0), .flags_i(8'd0),
    .event_reason_mask_i(9'h002), .payload_length_i(32'd1),
    .payload_i(uart_payload_w), .record_o(source_record_o[2*512-1:1*512])
  );
  lockstep_event_record_builder u_spi_record (
    .timestamp_i(event_timestamp_w), .capture_id_i(capture_id_i),
    .local_sequence_i(capture_start_i ? 32'd0 : local_sequence_r[2]), .protocol_id_i(8'd2),
    .event_type_i(8'd1), .source_kind_i(8'd0), .flags_i(8'd0),
    .event_reason_mask_i(9'h004), .payload_length_i(32'd1),
    .payload_i(spi_payload_w), .record_o(source_record_o[3*512-1:2*512])
  );
  lockstep_event_record_builder u_can_record (
    .timestamp_i(event_timestamp_w), .capture_id_i(capture_id_i),
    .local_sequence_i(capture_start_i ? 32'd0 : local_sequence_r[3]), .protocol_id_i(8'd3),
    .event_type_i(8'd1), .source_kind_i(8'd0), .flags_i(8'd0),
    .event_reason_mask_i(9'h008), .payload_length_i(32'd1),
    .payload_i(can_payload_w), .record_o(source_record_o[4*512-1:3*512])
  );
  lockstep_event_record_builder u_i2c_record (
    .timestamp_i(event_timestamp_w), .capture_id_i(capture_id_i),
    .local_sequence_i(capture_start_i ? 32'd0 : local_sequence_r[4]), .protocol_id_i(8'd4),
    .event_type_i(8'd1), .source_kind_i(8'd0), .flags_i(8'd0),
    .event_reason_mask_i(9'h010), .payload_length_i(32'd1),
    .payload_i(i2c_payload_w), .record_o(source_record_o[5*512-1:4*512])
  );
  assign source_record_o[6*512-1:5*512] = 512'd0;
  assign source_record_o[7*512-1:6*512] = 512'd0;
  lockstep_event_record_builder u_jtag_record (
    .timestamp_i(event_timestamp_w), .capture_id_i(capture_id_i),
    .local_sequence_i(capture_start_i ? 32'd0 : local_sequence_r[7]), .protocol_id_i(8'd7),
    .event_type_i(8'd1), .source_kind_i(8'd0),
    .flags_i({7'd0, sample_i[JTAG_BASE+9]}),
    .event_reason_mask_i(9'h080), .payload_length_i(32'd2),
    .payload_i(jtag_payload_w), .record_o(source_record_o[8*512-1:7*512])
  );
  lockstep_event_record_builder u_mismatch_record (
    .timestamp_i(event_timestamp_w), .capture_id_i(capture_id_i),
    .local_sequence_i(capture_start_i ? 32'd0 : local_sequence_r[8]), .protocol_id_i(8'd8),
    .event_type_i(8'd1), .source_kind_i(8'd3), .flags_i(8'd0),
    .event_reason_mask_i(9'h100), .payload_length_i(32'd2),
    .payload_i(mismatch_payload_w), .record_o(source_record_o[9*512-1:8*512])
  );

  // 每协议本地序号在事件产生时递增，FIFO 丢失会在序号中留下可审计缺口。
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      mismatch_prev_r <= #UDLY 5'd0;
      ahb_read_valid_r <= #UDLY 1'b0;
      ahb_read_signature_r <= #UDLY 66'd0;
      for (sequence_index = 0; sequence_index < 9; sequence_index = sequence_index + 1) begin
        local_sequence_r[sequence_index] <= #UDLY 32'd0;
      end
    end else if (capture_start_i) begin
      mismatch_prev_r <= #UDLY mismatch_current_w;
      ahb_read_valid_r <= #UDLY ahb_data_transfer_w && !sample_i[416];
      ahb_read_signature_r <= #UDLY ahb_read_signature_w;
      for (sequence_index = 0; sequence_index < 9; sequence_index = sequence_index + 1) begin
        local_sequence_r[sequence_index] <= #UDLY
          (source_push_w[sequence_index] ? 32'd1 : 32'd0);
      end
    end else begin
      mismatch_prev_r <= #UDLY mismatch_current_w;
      if (capture_active_i && sample_valid_i && ahb_data_transfer_w && !sample_i[416]) begin
        ahb_read_valid_r <= #UDLY 1'b1;
        ahb_read_signature_r <= #UDLY ahb_read_signature_w;
      end
      for (sequence_index = 0; sequence_index < 9; sequence_index = sequence_index + 1) begin
        if (source_push_w[sequence_index]) begin
          local_sequence_r[sequence_index] <= #UDLY local_sequence_r[sequence_index] + 32'd1;
        end
      end
    end
  end

endmodule
