/**********************************************************
* 文件名: lockstep_capture_protocol_v4.vh
* 日期: 2026-07-22
* 版本: 4.0
* 更新记录: 定义 512-bit 连续窗口与 128-bit 低速变化事件合同。
* 描述: 固化统一 4096-sample 时间轴、紧凑记录、来源能力和结束统计。
**********************************************************/
`ifndef LOCKSTEP_CAPTURE_PROTOCOL_V4_VH
`define LOCKSTEP_CAPTURE_PROTOCOL_V4_VH

localparam integer LOCKSTEP_PROTOCOL_VERSION_V4 = 4;
localparam integer LOCKSTEP_V4_CONTINUOUS_SAMPLE_BITS = 512;
localparam integer LOCKSTEP_V4_CONTINUOUS_SAMPLE_BYTES = 64;
localparam integer LOCKSTEP_V4_LOGICAL_VCD_BITS = 1024;
localparam integer LOCKSTEP_V4_LOW_SPEED_STATE_BITS = 16;
localparam integer LOCKSTEP_V4_WINDOW_SAMPLES = 4096;
localparam integer LOCKSTEP_V4_PRETRIGGER_SAMPLES = 2047;
localparam integer LOCKSTEP_V4_POSTTRIGGER_SAMPLES = 2048;

localparam [8:0] LOCKSTEP_V4_IMPLEMENTED_SOURCE_MASK = 9'h19f;
localparam [8:0] LOCKSTEP_V4_CONTINUOUS_SOURCE_MASK = 9'h101;
localparam [8:0] LOCKSTEP_V4_SPARSE_SOURCE_MASK = 9'h09e;
localparam [8:0] LOCKSTEP_V4_DESIGN_GAP_MASK = 9'h060;

localparam [15:0] LOCKSTEP_FRAME_EVENT_META_V4 = 16'h8103;
localparam [15:0] LOCKSTEP_FRAME_EVENT_DATA_V4 = 16'h8104;
localparam [15:0] LOCKSTEP_FRAME_EVENT_END_V4 = 16'h8105;

localparam integer LOCKSTEP_V4_CAPTURE_META_WORDS = 16;
localparam integer LOCKSTEP_V4_EVENT_META_WORDS = 16;
localparam integer LOCKSTEP_V4_EVENT_RECORD_WORDS = 4;
localparam integer LOCKSTEP_V4_EVENT_RECORD_BYTES = 16;
localparam integer LOCKSTEP_V4_EVENT_END_WORDS = 16;
localparam [7:0] LOCKSTEP_V4_EVENT_LAYOUT_CHANGE128 = 8'd1;

localparam [31:0] LOCKSTEP_V4_EVENT_END_WINDOW_COMPLETE = 32'd0;
localparam [31:0] LOCKSTEP_V4_EVENT_END_HOST_STOP = 32'd1;
localparam [31:0] LOCKSTEP_V4_EVENT_END_WATCHDOG = 32'd2;
localparam [31:0] LOCKSTEP_V4_EVENT_END_OVERFLOW = 32'd3;
localparam [31:0] LOCKSTEP_V4_EVENT_END_HARD_TIMEOUT = 32'd4;
localparam [31:0] LOCKSTEP_V4_EVENT_END_FATAL_ERROR = 32'd5;

`endif
