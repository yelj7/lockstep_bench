/**********************************************************
* 文件名: lockstep_capture_protocol_v3.vh
* 日期: 2026-07-19
* 版本: 3.0
* 更新记录: 新增长时间协议事件流合同。
* 描述: 在 v2 连续采样帧基础上定义 v3 配置、事件帧和固定记录格式。
**********************************************************/
`ifndef LOCKSTEP_CAPTURE_PROTOCOL_V3_VH
`define LOCKSTEP_CAPTURE_PROTOCOL_V3_VH

localparam integer LOCKSTEP_PROTOCOL_VERSION_V3 = 3;
localparam integer LOCKSTEP_CAPTURE_CONFIG_V3_BYTES = 68;
localparam integer LOCKSTEP_CAPTURE_CONFIG_V3_EVENT_ENABLE_MASK_OFFSET = 52;
localparam integer LOCKSTEP_CAPTURE_CONFIG_V3_EVENT_LIMIT_OFFSET = 56;
localparam integer LOCKSTEP_CAPTURE_CONFIG_V3_EVENT_WATCHDOG_TICKS_OFFSET = 60;
localparam integer LOCKSTEP_CAPTURE_CONFIG_V3_EVENT_HARD_TIMEOUT_TICKS_OFFSET = 64;
localparam [15:0] LOCKSTEP_FRAME_CONFIG_EVENTS = 16'h0006;
localparam integer LOCKSTEP_PAYLOAD_BYTES_CONFIG_EVENTS = 16;
localparam [15:0] LOCKSTEP_FRAME_START_EVENT_STREAM = 16'h0007;
localparam integer LOCKSTEP_PAYLOAD_BYTES_START_EVENT_STREAM = 0;

localparam [15:0] LOCKSTEP_FRAME_EVENT_META = 16'h8103;
localparam [15:0] LOCKSTEP_FRAME_EVENT_DATA = 16'h8104;
localparam [15:0] LOCKSTEP_FRAME_EVENT_END = 16'h8105;

localparam integer LOCKSTEP_EVENT_META_BYTES = 64;
localparam integer LOCKSTEP_EVENT_RECORD_BYTES = 64;
localparam integer LOCKSTEP_EVENT_RECORD_WORDS = 16;
localparam integer LOCKSTEP_EVENT_END_BYTES = 64;

localparam [8:0] LOCKSTEP_EVENT_REASON_AHB = 9'h001;
localparam [8:0] LOCKSTEP_EVENT_REASON_UART = 9'h002;
localparam [8:0] LOCKSTEP_EVENT_REASON_SPI = 9'h004;
localparam [8:0] LOCKSTEP_EVENT_REASON_CAN = 9'h008;
localparam [8:0] LOCKSTEP_EVENT_REASON_I2C = 9'h010;
localparam [8:0] LOCKSTEP_EVENT_REASON_ETH = 9'h020;
localparam [8:0] LOCKSTEP_EVENT_REASON_USB = 9'h040;
localparam [8:0] LOCKSTEP_EVENT_REASON_JTAG = 9'h080;
localparam [8:0] LOCKSTEP_EVENT_REASON_MISMATCH = 9'h100;

localparam [7:0] LOCKSTEP_EVENT_SOURCE_RAW_LINE = 8'd0;
localparam [7:0] LOCKSTEP_EVENT_SOURCE_DECODED_HINT = 8'd1;
localparam [7:0] LOCKSTEP_EVENT_SOURCE_BUS_TRANSFER = 8'd2;
localparam [7:0] LOCKSTEP_EVENT_SOURCE_COMPARATOR = 8'd3;

localparam [31:0] LOCKSTEP_EVENT_END_PROGRAM_DONE = 32'd0;
localparam [31:0] LOCKSTEP_EVENT_END_HOST_STOP = 32'd1;
localparam [31:0] LOCKSTEP_EVENT_END_WATCHDOG = 32'd2;
localparam [31:0] LOCKSTEP_EVENT_END_OVERFLOW = 32'd3;
localparam [31:0] LOCKSTEP_EVENT_END_HARD_TIMEOUT = 32'd4;
localparam [31:0] LOCKSTEP_EVENT_END_FATAL_ERROR = 32'd5;

`endif
