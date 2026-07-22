/**********************************************************
* 文件名: lockstep_command_state_machine.v
* 日期: 2026-07-17
* 版本: 0.5
* 更新记录:
*   0.1 初始版：有限采集控制器的命令状态验证单元。
*   0.2 重构：拆分数据通路为 5 个功能寄存器组，修复 arm/stop 脉冲模式，
*       更新文件头为中文模板，添加中文注释。
*   0.3 进一步拆分大块：设备生命周期/采集追踪分离，帧头/payload 分离。
*   0.4 消除空分支：cmd_error_idle_w / cmd_fire_catchall_w 组合逻辑替代
*       无内容分支，各块 if-else 链只保留有实际写入的分支。
*   0.5 配置成功后返回 STATUS_RSP，并等待采样域 ARM 接受后再确认。
*   0.6 增加 START_EVENT_STREAM 无响应单周期释放脉冲。
*   0.7 接入 POSTTRIGGER 与 DRAINING 生命周期状态。
*   0.8 统一设备状态更新优先级，采集完成优先于并发状态转换。
*   0.9 采样域未就绪时拒绝 ARM，活动期掉线立即上报致命错误并允许重配置。
* 描述: LOCKSTEP 命令状态机控制器——命令合法性检查、设备状态管理、配置转发、
*       延迟错误记录、上行帧响应生成。对接上位机命令帧，驱动采集核心
*       配置/arm/stop，并生成 HELLO_RSP / STATUS_RSP / ERROR_RSP 等
*       上行帧。
**********************************************************/

`timescale 1ns/1ps

module lockstep_command_state_machine (
  clk,
  rst_n,
  cmd_valid_i,
  cmd_ready_o,
  cmd_type_i,
  cmd_seq_i,
  cmd_payload_words_i,
  cmd_payload0_i,
  cmd_payload1_i,
  cmd_payload2_i,
  cmd_payload3_i,
  cmd_payload4_i,
  cmd_payload5_i,
  cmd_payload6_i,
  cmd_payload7_i,
  cmd_payload8_i,
  cmd_payload9_i,
  cmd_payload10_i,
  cmd_payload11_i,
  cmd_payload12_i,
  cmd_error_valid_i,
  cmd_error_ready_o,
  cmd_error_code_i,
  cmd_error_type_i,
  cmd_error_seq_i,
  cmd_error_detail0_i,
  cmd_error_detail1_i,
  cfg_valid_o,
  cfg_ready_i,
  cfg_sample_rate_hz_o,
  cfg_sample_count_o,
  cfg_pretrigger_count_o,
  cfg_posttrigger_count_o,
  cfg_channel_mask_o,
  cfg_input_invert_mask_o,
  cfg_physical_channels_o,
  cfg_sample_word_bits_o,
  cfg_trigger_mask_o,
  cfg_trigger_value_o,
  cfg_trigger_edge_rise_o,
  cfg_trigger_edge_fall_o,
  cfg_mode_o,
  cfg_trigger_timeout_samples_o,
  cfg_event_enable_mask_o,
  cfg_event_limit_o,
  cfg_event_watchdog_ticks_o,
  cfg_event_hard_timeout_ticks_o,
  cfg_error_valid_i,
  cfg_error_code_i,
  cfg_error_detail0_i,
  cfg_error_detail1_i,
  arm_o,
  arm_accepted_i,
  capture_domain_ready_i,
  event_stream_start_o,
  stop_o,
  capture_trigger_seen_i,
  capture_draining_i,
  capture_frame_done_i,
  capture_samples_captured_i,
  capture_samples_uploaded_i,
  capture_device_status_flags_i,
  capture_debug_flags_i,
  capture_window_state_i,
  capture_pretrigger_samples_i,
  capture_posttrigger_samples_i,
  capture_frame_source_state_i,
  capture_tx_generator_state_i,
  capture_ft601_state_i,
  capture_tx_bytes_i,
  frame_valid_o,
  frame_ready_i,
  frame_type_o,
  frame_capture_id_o,
  frame_flags_o,
  payload_word_count_o,
  payload0_o,
  payload1_o,
  payload2_o,
  payload3_o,
  payload4_o,
  payload5_o,
  payload6_o,
  payload7_o,
  payload8_o,
  payload9_o,
  payload10_o,
  payload11_o,
  payload12_o,
  payload13_o,
  payload14_o,
  payload15_o,
  capture_id_o,
  device_state_o,
  last_error_code_o,
  debug_state_o
);
  parameter UDLY                           = 1;
  parameter [31:0] SUPPORTED_MODES          = 32'h00000001;   // 位掩码：bit0=有限采集模式
  parameter [31:0] MIN_SAMPLE_RATE_HZ       = 32'd1000000;    // 最低支持采样率 1 MHz
  parameter [31:0] MAX_VERIFIED_SAMPLE_RATE_HZ = 32'd0;       // 最高已验证采样率（0=不限制）
  parameter [31:0] MAX_SAMPLE_COUNT         = 32'd1024;       // 最大采样数
  parameter [31:0] HELLO_PHYSICAL_CHANNELS  = 32'd16;
  parameter [31:0] HELLO_SAMPLE_WORD_BITS   = 32'd16;

  input         clk;
  input         rst_n;
  input         cmd_valid_i;
  output        cmd_ready_o;
  input  [15:0] cmd_type_i;
  input  [31:0] cmd_seq_i;
  input  [31:0] cmd_payload_words_i;
  input  [31:0] cmd_payload0_i;
  input  [31:0] cmd_payload1_i;
  input  [31:0] cmd_payload2_i;
  input  [31:0] cmd_payload3_i;
  input  [31:0] cmd_payload4_i;
  input  [31:0] cmd_payload5_i;
  input  [31:0] cmd_payload6_i;
  input  [31:0] cmd_payload7_i;
  input  [31:0] cmd_payload8_i;
  input  [31:0] cmd_payload9_i;
  input  [31:0] cmd_payload10_i;
  input  [31:0] cmd_payload11_i;
  input  [31:0] cmd_payload12_i;
  input         cmd_error_valid_i;
  output        cmd_error_ready_o;
  input  [31:0] cmd_error_code_i;
  input  [15:0] cmd_error_type_i;
  input  [31:0] cmd_error_seq_i;
  input  [31:0] cmd_error_detail0_i;
  input  [31:0] cmd_error_detail1_i;
  output        cfg_valid_o;
  input         cfg_ready_i;
  output [31:0] cfg_sample_rate_hz_o;
  output [31:0] cfg_sample_count_o;
  output [31:0] cfg_pretrigger_count_o;
  output [31:0] cfg_posttrigger_count_o;
  output [31:0] cfg_channel_mask_o;
  output [31:0] cfg_input_invert_mask_o;
  output [15:0] cfg_physical_channels_o;
  output [15:0] cfg_sample_word_bits_o;
  output [31:0] cfg_trigger_mask_o;
  output [31:0] cfg_trigger_value_o;
  output [31:0] cfg_trigger_edge_rise_o;
  output [31:0] cfg_trigger_edge_fall_o;
  output [31:0] cfg_mode_o;
  output [31:0] cfg_trigger_timeout_samples_o;
  output [31:0] cfg_event_enable_mask_o;
  output [31:0] cfg_event_limit_o;
  output [31:0] cfg_event_watchdog_ticks_o;
  output [31:0] cfg_event_hard_timeout_ticks_o;
  input         cfg_error_valid_i;
  input  [31:0] cfg_error_code_i;
  input  [31:0] cfg_error_detail0_i;
  input  [31:0] cfg_error_detail1_i;
  output        arm_o;
  input         arm_accepted_i;
  input         capture_domain_ready_i;
  output        event_stream_start_o;
  output        stop_o;
  input         capture_trigger_seen_i;
  input         capture_draining_i;
  input         capture_frame_done_i;
  input  [31:0] capture_samples_captured_i;
  input  [31:0] capture_samples_uploaded_i;
  input  [31:0] capture_device_status_flags_i;
  input  [31:0] capture_debug_flags_i;
  input  [31:0] capture_window_state_i;
  input  [31:0] capture_pretrigger_samples_i;
  input  [31:0] capture_posttrigger_samples_i;
  input  [31:0] capture_frame_source_state_i;
  input  [31:0] capture_tx_generator_state_i;
  input  [31:0] capture_ft601_state_i;
  input  [31:0] capture_tx_bytes_i;
  output        frame_valid_o;
  input         frame_ready_i;
  output [15:0] frame_type_o;
  output [31:0] frame_capture_id_o;
  output [31:0] frame_flags_o;
  output [31:0] payload_word_count_o;
  output [31:0] payload0_o;
  output [31:0] payload1_o;
  output [31:0] payload2_o;
  output [31:0] payload3_o;
  output [31:0] payload4_o;
  output [31:0] payload5_o;
  output [31:0] payload6_o;
  output [31:0] payload7_o;
  output [31:0] payload8_o;
  output [31:0] payload9_o;
  output [31:0] payload10_o;
  output [31:0] payload11_o;
  output [31:0] payload12_o;
  output [31:0] payload13_o;
  output [31:0] payload14_o;
  output [31:0] payload15_o;
  output [31:0] capture_id_o;
  output [31:0] device_state_o;
  output [31:0] last_error_code_o;
  output [31:0] debug_state_o;

  `ifdef LOCKSTEP_CAPTURE_PROTOCOL_V2_VH
  `undef LOCKSTEP_CAPTURE_PROTOCOL_V2_VH
  `endif
  `include "lockstep_capture_protocol_v2.vh"
  `ifdef LOCKSTEP_CAPTURE_PROTOCOL_V3_VH
  `undef LOCKSTEP_CAPTURE_PROTOCOL_V3_VH
  `endif
  `include "lockstep_capture_protocol_v3.vh"

  //==================================================================
  // FSM 状态编码
  //==================================================================
  localparam [2:0] ST_IDLE            = 3'd0; // 空闲：等待命令
  localparam [2:0] ST_APPLY_CONFIG    = 3'd1; // 配置转发：向下游发出配置
  localparam [2:0] ST_WAIT_CFG_RESULT = 3'd2; // 等待配置校验结果
  localparam [2:0] ST_SEND_FRAME      = 3'd3; // 发送上行帧
  localparam [2:0] ST_WAIT_ARM_ACCEPT = 3'd4; // 等待采样域接受 ARM

  // 帧发送后的设备状态动作
  localparam [1:0] POST_NONE     = 2'd0; // 无动作
  localparam [1:0] POST_ERROR    = 2'd1; // 发送后进入 ERROR 状态
  localparam [1:0] POST_COMPLETE = 2'd2; // 发送后进入 COMPLETE 状态

  //==================================================================
  // FSM 状态寄存器
  //==================================================================
  reg [2:0] cur_state;
  reg [2:0] nxt_state;

  //==================================================================
  // Block 3a: 设备生命周期寄存器 — 设备状态、帧后动作
  //==================================================================
  reg [1:0]  post_frame_action_r;
  reg [31:0] device_state_r;

  //==================================================================
  // Block 3b: 采集追踪寄存器 — 采集ID、错误码、设备状态标志
  //==================================================================
  reg [31:0] capture_id_r;
  reg [31:0] active_capture_id_r;
  reg [31:0] last_error_code_r;
  reg [31:0] last_device_status_flags_r;

  //==================================================================
  // Block 3c: 配置输出寄存器 — 向下游采集核心转发配置参数
  //==================================================================
  reg [31:0] cfg_sample_rate_hz_o;
  reg [31:0] cfg_sample_count_o;
  reg [31:0] cfg_pretrigger_count_o;
  reg [31:0] cfg_posttrigger_count_o;
  reg [31:0] cfg_channel_mask_o;
  reg [31:0] cfg_input_invert_mask_o;
  reg [15:0] cfg_physical_channels_o;
  reg [15:0] cfg_sample_word_bits_o;
  reg [31:0] cfg_trigger_mask_o;
  reg [31:0] cfg_trigger_value_o;
  reg [31:0] cfg_trigger_edge_rise_o;
  reg [31:0] cfg_trigger_edge_fall_o;
  reg [31:0] cfg_mode_o;
  reg [31:0] cfg_trigger_timeout_samples_o;
  reg [31:0] cfg_event_enable_mask_o;
  reg [31:0] cfg_event_limit_o;
  reg [31:0] cfg_event_watchdog_ticks_o;
  reg [31:0] cfg_event_hard_timeout_ticks_o;

  //==================================================================
  // Block 3d: 脉冲输出寄存器 — arm / stop 单周期脉冲
  //==================================================================
  reg        arm_o;
  reg        event_stream_start_o;
  reg        stop_o;

  //==================================================================
  // Block 3e: 延迟错误寄存器 — 设备忙时暂存错误，采集完成后发出
  //==================================================================
  reg        pending_deferred_error_r;
  reg [31:0] deferred_error_code_r;
  reg [15:0] deferred_error_type_r;
  reg [31:0] deferred_error_seq_r;
  reg [31:0] deferred_error_state_r;
  reg [31:0] deferred_error_detail0_r;
  reg [31:0] deferred_error_detail1_r;
  reg [31:0] deferred_busy_count_r;

  //==================================================================
  // Block 3f: 帧头寄存器 — 帧类型、采集ID、标志位、payload字数
  //==================================================================
  reg [15:0] frame_type_o;
  reg [31:0] frame_capture_id_o;
  reg [31:0] frame_flags_o;
  reg [31:0] payload_word_count_o;

  //==================================================================
  // Block 3g: 帧 payload 寄存器 — 上行帧数据字段（16字）
  //==================================================================
  reg [31:0] payload0_o;
  reg [31:0] payload1_o;
  reg [31:0] payload2_o;
  reg [31:0] payload3_o;
  reg [31:0] payload4_o;
  reg [31:0] payload5_o;
  reg [31:0] payload6_o;
  reg [31:0] payload7_o;
  reg [31:0] payload8_o;
  reg [31:0] payload9_o;
  reg [31:0] payload10_o;
  reg [31:0] payload11_o;
  reg [31:0] payload12_o;
  reg [31:0] payload13_o;
  reg [31:0] payload14_o;
  reg [31:0] payload15_o;

  //==================================================================
  // 组合逻辑：设备忙标志、握手、命令分类
  //==================================================================
  wire        device_active_w;             // 设备正在采集中
  wire        capture_done_with_error_w;   // 采集完成且有延迟错误待发送
  wire        cmd_fire_w;                  // 命令握手完成
  wire        cmd_error_fire_w;            // 命令错误握手完成
  wire        cfg_fire_w;                  // 配置握手完成
  wire        frame_fire_w;                // 帧发送握手完成
  wire        config_supported_w;          // 当前模式在支持列表中
  wire        capture_config_valid_w;      // CONFIG_CAPTURE 字段符合当前固定采集能力
  wire        cmd_hello_w;                 // HELLO_REQ 命令
  wire        cmd_config_w;                // CONFIG_CAPTURE 命令
  wire        cmd_arm_w;                   // ARM_CAPTURE 命令
  wire        cmd_stop_w;                  // STOP_CAPTURE 命令
  wire        cmd_status_w;                // GET_STATUS 命令
  wire        cmd_event_config_w;          // CONFIG_EVENTS 命令
  wire        cmd_event_start_w;           // START_EVENT_STREAM 命令
  wire        event_config_valid_w;
  wire        arm_fire_w;                  // arm 脉冲触发
  wire        stop_fire_w;                 // stop 脉冲触发
  wire        event_start_fire_w;          // 事件流释放脉冲触发
  wire        capture_domain_fault_w;      // 等待/活动采集期间采样域掉线

  // 设备活跃：已 arm 但尚未完成或排空
  assign device_active_w = (device_state_r == LOCKSTEP_STATE_ARMED) ||
                            (device_state_r == LOCKSTEP_STATE_CAPTURING_PRETRIGGER) ||
                            (device_state_r == LOCKSTEP_STATE_CAPTURING_POSTTRIGGER) ||
                            (device_state_r == LOCKSTEP_STATE_DRAINING);

  // 采集完成且有待发的延迟错误：需优先发送 ERROR_RSP
  assign capture_done_with_error_w = capture_frame_done_i && pending_deferred_error_r && device_active_w;

  // 流控就绪：仅在 IDLE 且无待发延迟错误时接收新命令/错误
  assign cmd_error_ready_o = (cur_state == ST_IDLE) && !capture_done_with_error_w;
  assign cmd_ready_o       = (cur_state == ST_IDLE) && !capture_done_with_error_w && !cmd_error_valid_i;

  // 输出有效：在各功能状态下拉高对应 valid
  assign cfg_valid_o   = (cur_state == ST_APPLY_CONFIG);
  assign frame_valid_o = (cur_state == ST_SEND_FRAME);

  // 状态直连输出
  assign capture_id_o     = active_capture_id_r;
  assign device_state_o   = device_state_r;
  assign last_error_code_o = last_error_code_r;
  assign debug_state_o    = {29'd0, cur_state};

  // 握手完成信号
  assign cmd_fire_w       = cmd_valid_i && cmd_ready_o;
  assign cmd_error_fire_w = cmd_error_valid_i && cmd_error_ready_o;
  assign cfg_fire_w       = cfg_valid_o && cfg_ready_i;
  assign frame_fire_w     = frame_valid_o && frame_ready_i;

  // 模式支持检查：位 0 为有限采集模式
  assign config_supported_w = ((SUPPORTED_MODES & 32'h00000001) != 32'd0);
  assign capture_config_valid_w = config_supported_w &&
                                  (cmd_payload_words_i == 32'd13) &&
                                  (cmd_payload0_i == 32'd120000000) &&
                                  (cmd_payload1_i == 32'd4096) &&
                                  (cmd_payload2_i == 32'd2047) &&
                                  (cmd_payload3_i == 32'd2049) &&
                                  ((cmd_payload4_i & ~32'h000001ff) == 32'd0) &&
                                  (cmd_payload5_i == 32'd0) &&
                                  (cmd_payload6_i == 32'h04000400) &&
                                  ((cmd_payload7_i & ~32'h0000001f) == 32'd0) &&
                                  ((cmd_payload9_i & ~32'h00000001) == 32'd0) &&
                                  (cmd_payload10_i == 32'd0) &&
                                  (cmd_payload11_i == 32'd0) &&
                                  (cmd_payload12_i != 32'd0);

  // 命令类型解码
  assign cmd_hello_w  = (cmd_type_i == LOCKSTEP_FRAME_HELLO_REQ);
  assign cmd_config_w = (cmd_type_i == LOCKSTEP_FRAME_CONFIG_CAPTURE);
  assign cmd_arm_w    = (cmd_type_i == LOCKSTEP_FRAME_ARM_CAPTURE);
  assign cmd_stop_w   = (cmd_type_i == LOCKSTEP_FRAME_STOP_CAPTURE);
  assign cmd_status_w = (cmd_type_i == LOCKSTEP_FRAME_GET_STATUS);
  assign cmd_event_config_w = (cmd_type_i == LOCKSTEP_FRAME_CONFIG_EVENTS);
  assign cmd_event_start_w = (cmd_type_i == LOCKSTEP_FRAME_START_EVENT_STREAM);
  assign event_config_valid_w = ((cmd_payload0_i & ~32'h000001ff) == 32'd0) &&
                                ((cmd_payload0_i & ~32'h0000019f) == 32'd0) &&
                                (cmd_payload1_i == 32'd0) &&
                                (cmd_payload2_i != 32'd0) &&
                                (cmd_payload3_i >= cmd_payload2_i);

  // arm/stop 脉冲触发条件（组合逻辑计算，时序块单点赋值）
  assign arm_fire_w  = cmd_fire_w && cmd_arm_w && capture_domain_ready_i &&
                       (device_state_r == LOCKSTEP_STATE_CONFIGURED);
  assign stop_fire_w = cmd_fire_w && cmd_stop_w && device_active_w;
  assign event_start_fire_w = cmd_fire_w && cmd_event_start_w && device_active_w;
  assign capture_domain_fault_w = !capture_domain_ready_i &&
                                  ((cur_state == ST_WAIT_ARM_ACCEPT) || device_active_w);

  // 命令错误空闲触发：设备空闲时收到上位机错误（非延迟）
  wire cmd_error_idle_w;
  // 命令触发 catchall：仅覆盖未知命令或已知命令的非法状态。
  wire cmd_fire_catchall_w;

  assign cmd_error_idle_w    = cmd_error_fire_w && !device_active_w;
  assign cmd_fire_catchall_w = cmd_fire_w
                              && !cmd_status_w
                              && !(cmd_hello_w && !device_active_w)
                              && !(cmd_config_w && !device_active_w && capture_config_valid_w)
                              && !(cmd_event_config_w &&
                                   (device_state_r == LOCKSTEP_STATE_CONFIGURED) &&
                                   event_config_valid_w)
                              && !(cmd_arm_w && capture_domain_ready_i &&
                                   (device_state_r == LOCKSTEP_STATE_CONFIGURED))
                              && !(cmd_stop_w && device_active_w)
                              && !(device_active_w && !cmd_stop_w);

  //==================================================================
  // FSM Block 1: 现态寄存器
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      cur_state <= #UDLY ST_IDLE;
    end else begin
      cur_state <= #UDLY nxt_state;
    end
  end

  //==================================================================
  // FSM Block 2: 次态组合逻辑
  //==================================================================
  always @(*) begin
    nxt_state = cur_state;

    case (cur_state)
      ST_IDLE: begin
        if (capture_domain_fault_w) begin
          nxt_state = ST_SEND_FRAME;
        end else if (capture_done_with_error_w) begin
          // 采集完成时存在延迟错误，优先发送 ERROR_RSP
          nxt_state = ST_SEND_FRAME;
        end else if (cmd_error_fire_w && !device_active_w) begin
          // 收到上位机错误通知且设备空闲，发送 ERROR_RSP
          nxt_state = ST_SEND_FRAME;
        end else if (cmd_fire_w && cmd_status_w) begin
          // 活跃状态也允许查询，状态响应不改变采集流程。
          nxt_state = ST_SEND_FRAME;
        end else if (cmd_fire_w && cmd_event_start_w && device_active_w) begin
          // 事件流释放命令无响应，保持 IDLE。
          nxt_state = ST_IDLE;
        end else if (cmd_fire_w && device_active_w && !cmd_stop_w) begin
          // 设备忙且非停止命令：延迟错误，保持在 IDLE
          nxt_state = ST_IDLE;
        end else if (cmd_fire_w && cmd_stop_w && device_active_w) begin
          // 停止命令且设备活跃：接受停止，保持在 IDLE
          nxt_state = ST_IDLE;
        end else if (cmd_fire_w && cmd_hello_w && !device_active_w) begin
          // HELLO 请求：发送 HELLO_RSP
          nxt_state = ST_SEND_FRAME;
        end else if (cmd_fire_w && cmd_status_w && !device_active_w) begin
          // 状态查询：发送 STATUS_RSP
          nxt_state = ST_SEND_FRAME;
        end else if (cmd_fire_w && cmd_config_w && !device_active_w && capture_config_valid_w) begin
          // 配置命令且模式支持：转发到采集核心
          nxt_state = ST_APPLY_CONFIG;
        end else if (cmd_fire_w && cmd_event_config_w &&
                     (device_state_r == LOCKSTEP_STATE_CONFIGURED) && event_config_valid_w) begin
          nxt_state = ST_SEND_FRAME;
        end else if (cmd_fire_w && cmd_arm_w && (device_state_r == LOCKSTEP_STATE_CONFIGURED)) begin
          // Arm 命令且已配置：等待跨时钟域窗口明确接受
          nxt_state = ST_WAIT_ARM_ACCEPT;
        end else if (cmd_fire_w) begin
          // 无法识别的命令或状态错误：发送 ERROR_RSP
          nxt_state = ST_SEND_FRAME;
        end else begin
          // 无有效命令，保持 IDLE
          nxt_state = ST_IDLE;
        end
      end

      ST_APPLY_CONFIG: begin
        if (cfg_fire_w) begin
          // 配置已被下游接收，等待校验结果
          nxt_state = ST_WAIT_CFG_RESULT;
        end else begin
          // 下游未就绪，保持配置转发状态
          nxt_state = ST_APPLY_CONFIG;
        end
      end

      ST_WAIT_CFG_RESULT: begin
        if (cfg_error_valid_i) begin
          // 配置校验失败：发送 ERROR_RSP
          nxt_state = ST_SEND_FRAME;
        end else begin
          // 配置校验通过：发送 CONFIGURED 状态响应
          nxt_state = ST_SEND_FRAME;
        end
      end

      ST_WAIT_ARM_ACCEPT: begin
        if (!capture_domain_ready_i) begin
          nxt_state = ST_SEND_FRAME;
        end else if (arm_accepted_i) begin
          // 仅在采样域接受 ARM 后确认给主机
          nxt_state = ST_SEND_FRAME;
        end else begin
          nxt_state = ST_WAIT_ARM_ACCEPT;
        end
      end

      ST_SEND_FRAME: begin
        if (frame_fire_w) begin
          // 帧已被下游接收，回到 IDLE
          nxt_state = ST_IDLE;
        end else begin
          // 下游未就绪，保持发送状态
          nxt_state = ST_SEND_FRAME;
        end
      end

      default: begin
        // 非法状态恢复
        nxt_state = ST_IDLE;
      end
    endcase
  end

  //==================================================================
  // FSM Block 3a-1: 帧后动作寄存器 — post_frame_action_r
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      post_frame_action_r <= #UDLY POST_NONE;
    end else begin
      // 命令/错误处理优先级链
      if (capture_done_with_error_w) begin
        post_frame_action_r <= #UDLY POST_COMPLETE;
      end else if (cmd_error_idle_w) begin
        post_frame_action_r <= #UDLY POST_ERROR;
      end else if (cmd_fire_w && cmd_hello_w && !device_active_w) begin
        post_frame_action_r <= #UDLY POST_NONE;
      end else if (cmd_fire_w && cmd_status_w) begin
        post_frame_action_r <= #UDLY POST_NONE;
      end else if (cmd_fire_catchall_w) begin
        post_frame_action_r <= #UDLY POST_ERROR;
      end

      // 配置校验结果（独立于命令处理链）
      if ((cur_state == ST_WAIT_CFG_RESULT) && cfg_error_valid_i) begin
        post_frame_action_r <= #UDLY POST_ERROR;
      end

      if (capture_domain_fault_w) begin
        post_frame_action_r <= #UDLY POST_NONE;
      end

      // 帧发送完毕，清除动作标记
      if (frame_fire_w) begin
        post_frame_action_r <= #UDLY POST_NONE;
      end
    end
  end

  //==================================================================
  // FSM Block 3a-2: 设备状态寄存器 — device_state_r
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      device_state_r <= #UDLY LOCKSTEP_STATE_IDLE;
    end else begin
      // 状态收敛优先于生命周期转换，避免同周期后写覆盖完成态。
      if (capture_domain_fault_w && device_active_w) begin
        device_state_r <= #UDLY LOCKSTEP_STATE_ERROR;
      end else if (frame_fire_w && (post_frame_action_r == POST_ERROR)) begin
        device_state_r <= #UDLY LOCKSTEP_STATE_ERROR;
      end else if (frame_fire_w && (post_frame_action_r == POST_COMPLETE)) begin
        device_state_r <= #UDLY LOCKSTEP_STATE_CONFIGURED;
      end else if (capture_frame_done_i && device_active_w && !pending_deferred_error_r) begin
        device_state_r <= #UDLY LOCKSTEP_STATE_CONFIGURED;
      end else if (cmd_fire_w && cmd_stop_w && device_active_w) begin
        device_state_r <= #UDLY LOCKSTEP_STATE_DRAINING;
      end else if (capture_draining_i && device_active_w) begin
        device_state_r <= #UDLY LOCKSTEP_STATE_DRAINING;
      end else if (capture_trigger_seen_i &&
                   (device_state_r == LOCKSTEP_STATE_CAPTURING_PRETRIGGER)) begin
        device_state_r <= #UDLY LOCKSTEP_STATE_CAPTURING_POSTTRIGGER;
      end else if ((cur_state == ST_WAIT_ARM_ACCEPT) && arm_accepted_i) begin
        device_state_r <= #UDLY LOCKSTEP_STATE_CAPTURING_PRETRIGGER;
      end else if ((cur_state == ST_WAIT_CFG_RESULT) && !cfg_error_valid_i) begin
        device_state_r <= #UDLY LOCKSTEP_STATE_CONFIGURED;
      end
    end
  end

  //==================================================================
  // FSM Block 3b-1: 采集追踪寄存器 — last_device_status_flags_r
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      last_device_status_flags_r <= #UDLY 32'd0;
    end else begin
      // 采集完成（无延迟错误）：锁存状态标志
      if (capture_frame_done_i && device_active_w && !pending_deferred_error_r) begin
        last_device_status_flags_r <= #UDLY capture_device_status_flags_i;
      end

      // 命令/错误处理优先级链
      if (capture_done_with_error_w) begin
        last_device_status_flags_r <= #UDLY capture_device_status_flags_i;
      end else if (cmd_fire_w && cmd_arm_w && (device_state_r == LOCKSTEP_STATE_CONFIGURED)) begin
        last_device_status_flags_r <= #UDLY 32'd0;
      end

      // 配置校验通过：清除状态标志
      if ((cur_state == ST_WAIT_CFG_RESULT) && !cfg_error_valid_i) begin
        last_device_status_flags_r <= #UDLY 32'd0;
      end
      if (capture_domain_fault_w && device_active_w) begin
        last_device_status_flags_r <= #UDLY LOCKSTEP_DEVICE_STATUS_FLAG_FATAL_INTERNAL_ERROR;
      end
    end
  end

  //==================================================================
  // FSM Block 3b-2: 采集追踪寄存器 — last_error_code_r
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      last_error_code_r <= #UDLY LOCKSTEP_ERR_NONE;
    end else begin
      // 命令/错误处理优先级链
      if (capture_done_with_error_w) begin
        last_error_code_r <= #UDLY deferred_error_code_r;
      end else if (cmd_error_idle_w) begin
        last_error_code_r <= #UDLY cmd_error_code_i;
      end else if (cmd_fire_w && cmd_arm_w && (device_state_r == LOCKSTEP_STATE_CONFIGURED)) begin
        last_error_code_r <= #UDLY LOCKSTEP_ERR_NONE;
      end else if (cmd_fire_catchall_w) begin
        if (cmd_config_w && !config_supported_w) begin
          last_error_code_r <= #UDLY LOCKSTEP_ERR_UNSUPPORTED_MODE;
        end else if (cmd_config_w) begin
          last_error_code_r <= #UDLY LOCKSTEP_ERR_BAD_TRIGGER_CONFIG;
        end else if (cmd_event_config_w) begin
          last_error_code_r <= #UDLY LOCKSTEP_ERR_BAD_TRIGGER_CONFIG;
        end else begin
          last_error_code_r <= #UDLY LOCKSTEP_ERR_BAD_STATE;
        end
      end

      // 配置校验结果处理（独立于命令处理链）
      if ((cur_state == ST_WAIT_CFG_RESULT) && cfg_error_valid_i) begin
        last_error_code_r <= #UDLY cfg_error_code_i;
      end else if ((cur_state == ST_WAIT_CFG_RESULT) && !cfg_error_valid_i) begin
        last_error_code_r <= #UDLY LOCKSTEP_ERR_NONE;
      end
      if (capture_domain_fault_w ||
          (cmd_fire_w && cmd_arm_w && !capture_domain_ready_i &&
           (device_state_r == LOCKSTEP_STATE_CONFIGURED))) begin
        last_error_code_r <= #UDLY LOCKSTEP_ERR_INTERNAL;
      end
    end
  end

  //==================================================================
  // FSM Block 3b-3: 采集追踪寄存器 — capture_id_r、active_capture_id_r
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      capture_id_r        <= #UDLY 32'd0;
      active_capture_id_r <= #UDLY 32'd0;
    end else begin
      // Arm 命令：递增采集 ID 并同步到活跃 ID
      if (arm_fire_w) begin
        capture_id_r        <= #UDLY capture_id_r + 32'd1;
        active_capture_id_r <= #UDLY capture_id_r + 32'd1;
      end
    end
  end

  //==================================================================
  // FSM Block 3c: 配置输出寄存器 — 仅在有效 CONFIG_CAPTURE 命令时更新
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      cfg_sample_rate_hz_o    <= #UDLY 32'd0;
      cfg_sample_count_o      <= #UDLY 32'd0;
      cfg_pretrigger_count_o  <= #UDLY 32'd0;
      cfg_posttrigger_count_o <= #UDLY 32'd0;
      cfg_channel_mask_o      <= #UDLY 32'd0;
      cfg_input_invert_mask_o <= #UDLY 32'd0;
      cfg_physical_channels_o <= #UDLY 16'd0;
      cfg_sample_word_bits_o  <= #UDLY 16'd0;
      cfg_trigger_mask_o      <= #UDLY 32'd0;
      cfg_trigger_value_o     <= #UDLY 32'd0;
      cfg_trigger_edge_rise_o <= #UDLY 32'd0;
      cfg_trigger_edge_fall_o <= #UDLY 32'd0;
      cfg_mode_o              <= #UDLY 32'd0;
      cfg_trigger_timeout_samples_o <= #UDLY 32'd0;
      cfg_event_enable_mask_o <= #UDLY 32'd0;
      cfg_event_limit_o <= #UDLY 32'd0;
      cfg_event_watchdog_ticks_o <= #UDLY 32'd12000000;
      cfg_event_hard_timeout_ticks_o <= #UDLY 32'd240000000;
    end else if (cmd_fire_w && cmd_config_w && !device_active_w && capture_config_valid_w) begin
      // 将上位机 payload 字段映射到采集核心配置接口
      cfg_sample_rate_hz_o    <= #UDLY cmd_payload0_i;                    // payload[0]：采样率 Hz
      cfg_sample_count_o      <= #UDLY cmd_payload1_i;                    // payload[1]：请求采样数
      cfg_pretrigger_count_o  <= #UDLY cmd_payload2_i;                    // payload[2]：预触发深度
      cfg_posttrigger_count_o <= #UDLY cmd_payload3_i;                    // payload[3]：后触发深度
      cfg_channel_mask_o      <= #UDLY cmd_payload4_i;                    // payload[4]：通道掩码
      cfg_input_invert_mask_o <= #UDLY cmd_payload5_i;                    // payload[5]：输入反转掩码
      cfg_physical_channels_o <= #UDLY cmd_payload6_i[15:0];              // payload[6] 低 16 位：物理通道数
      cfg_sample_word_bits_o  <= #UDLY cmd_payload6_i[31:16];             // payload[6] 高 16 位：采样位宽
      cfg_trigger_mask_o      <= #UDLY cmd_payload7_i;                    // payload[7]：触发掩码
      cfg_trigger_value_o     <= #UDLY cmd_payload8_i;                    // payload[8]：触发值
      cfg_trigger_edge_rise_o <= #UDLY cmd_payload9_i;                    // payload[9]：上升沿触发
      cfg_trigger_edge_fall_o <= #UDLY cmd_payload10_i;                   // payload[10]：下降沿触发
      cfg_mode_o              <= #UDLY cmd_payload11_i;                   // payload[11]：采集模式
      cfg_trigger_timeout_samples_o <= #UDLY cmd_payload12_i;              // payload[12]：未触发 watchdog 样本数
    end else if (cmd_fire_w && cmd_event_config_w &&
                 (device_state_r == LOCKSTEP_STATE_CONFIGURED) && event_config_valid_w) begin
      cfg_event_enable_mask_o <= #UDLY cmd_payload0_i;
      cfg_event_limit_o <= #UDLY cmd_payload1_i;
      cfg_event_watchdog_ticks_o <= #UDLY cmd_payload2_i;
      cfg_event_hard_timeout_ticks_o <= #UDLY cmd_payload3_i;
    end
  end

  //==================================================================
  // FSM Block 3d: 脉冲输出寄存器 — arm/stop 单周期脉冲，组合计算次态单点赋值
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      arm_o  <= #UDLY 1'b0;
      event_stream_start_o <= #UDLY 1'b0;
      stop_o <= #UDLY 1'b0;
    end else begin
      // arm 脉冲：已配置状态下收到 ARM_CAPTURE，持续一拍
      arm_o  <= #UDLY arm_fire_w;
      // 主机收到 ARM ACK 后释放事件上传，持续一拍且不生成响应。
      event_stream_start_o <= #UDLY event_start_fire_w;
      // stop 脉冲：设备活跃时收到 STOP_CAPTURE，持续一拍
      stop_o <= #UDLY stop_fire_w;
    end
  end

  //==================================================================
  // FSM Block 3e-1: 延迟错误控制寄存器 — pending_deferred_error_r、deferred_busy_count_r
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      pending_deferred_error_r <= #UDLY 1'b0;
      deferred_busy_count_r    <= #UDLY 32'd0;
    end else begin
      // 设备活跃时收到错误 → 置 pending
      if (cmd_error_fire_w && device_active_w && !capture_done_with_error_w) begin
        pending_deferred_error_r <= #UDLY 1'b1;
      end else if (cmd_fire_w && device_active_w && !cmd_stop_w && !cmd_status_w &&
                   !cmd_event_start_w) begin
        // 设备忙且非停止命令 → 置 pending，递增 busy 计数
        pending_deferred_error_r <= #UDLY 1'b1;
        deferred_busy_count_r    <= #UDLY deferred_busy_count_r + 32'd1;
      end else if (cmd_fire_w && cmd_arm_w && (device_state_r == LOCKSTEP_STATE_CONFIGURED)) begin
        // Arm 命令：新一轮采集，清除延迟错误
        pending_deferred_error_r <= #UDLY 1'b0;
        deferred_busy_count_r    <= #UDLY 32'd0;
      end

      // 帧发送完成（采集完成且错误已通知）→ 清除
      if (frame_fire_w && post_frame_action_r == POST_COMPLETE) begin
        pending_deferred_error_r <= #UDLY 1'b0;
        deferred_busy_count_r    <= #UDLY 32'd0;
      end
    end
  end

  //==================================================================
  // FSM Block 3e-2: 延迟错误详情寄存器 — deferred_error_code/type/seq/state/detail
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      deferred_error_code_r    <= #UDLY LOCKSTEP_ERR_NONE;
      deferred_error_type_r    <= #UDLY 16'd0;
      deferred_error_seq_r     <= #UDLY 32'd0;
      deferred_error_state_r   <= #UDLY LOCKSTEP_STATE_IDLE;
      deferred_error_detail0_r <= #UDLY 32'd0;
      deferred_error_detail1_r <= #UDLY 32'd0;
    end else begin
      // 设备活跃时收到错误 → 暂存错误详情
      if (cmd_error_fire_w && device_active_w && !capture_done_with_error_w) begin
        deferred_error_code_r    <= #UDLY cmd_error_code_i;
        deferred_error_type_r    <= #UDLY cmd_error_type_i;
        deferred_error_seq_r     <= #UDLY cmd_error_seq_i;
        deferred_error_state_r   <= #UDLY device_state_r;
        deferred_error_detail0_r <= #UDLY cmd_error_detail0_i;
        deferred_error_detail1_r <= #UDLY cmd_error_detail1_i;
      end else if (cmd_fire_w && device_active_w && !cmd_stop_w && !cmd_status_w &&
                   !cmd_event_start_w) begin
        // 设备忙且非停止命令 → 暂存 BUSY 错误详情
        deferred_error_code_r    <= #UDLY LOCKSTEP_ERR_BUSY;
        deferred_error_type_r    <= #UDLY cmd_type_i;
        deferred_error_seq_r     <= #UDLY cmd_seq_i;
        deferred_error_state_r   <= #UDLY device_state_r;
        deferred_error_detail0_r <= #UDLY deferred_busy_count_r + 32'd1;
        deferred_error_detail1_r <= #UDLY 32'd0;
      end
      // 注意：Arm 命令不清除本组寄存器（仅 3e-1 清除 pending/busy_count）
    end
  end

  //==================================================================
  // FSM Block 3f: 帧头寄存器 — frame_type、capture_id、flags、payload字数
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      frame_type_o        <= #UDLY 16'd0;
      frame_capture_id_o  <= #UDLY 32'd0;
      frame_flags_o       <= #UDLY 32'd0;
      payload_word_count_o <= #UDLY 32'd0;
    end else begin
      // 优先级链：仅更新帧头字段
      if (capture_done_with_error_w) begin
        // 采集完成 + 延迟错误 → ERROR_RSP
        frame_type_o        <= #UDLY LOCKSTEP_FRAME_ERROR_RSP;
        frame_capture_id_o  <= #UDLY active_capture_id_r;
        frame_flags_o       <= #UDLY LOCKSTEP_FRAME_FLAG_ERROR;
        payload_word_count_o <= #UDLY 32'd8;
      end else if (cmd_error_idle_w) begin
        // 空闲时错误 → ERROR_RSP
        frame_type_o        <= #UDLY LOCKSTEP_FRAME_ERROR_RSP;
        frame_capture_id_o  <= #UDLY 32'd0;
        frame_flags_o       <= #UDLY LOCKSTEP_FRAME_FLAG_ERROR;
        payload_word_count_o <= #UDLY 32'd8;
      end else if (cmd_fire_w && cmd_hello_w && !device_active_w) begin
        // HELLO_REQ → HELLO_RSP
        frame_type_o        <= #UDLY LOCKSTEP_FRAME_HELLO_RSP;
        frame_capture_id_o  <= #UDLY 32'd0;
        frame_flags_o       <= #UDLY 32'd0;
        payload_word_count_o <= #UDLY 32'd8;
      end else if (cmd_fire_w && cmd_status_w) begin
        // GET_STATUS → STATUS_RSP
        frame_type_o        <= #UDLY LOCKSTEP_FRAME_STATUS_RSP;
        frame_capture_id_o  <= #UDLY active_capture_id_r;
        frame_flags_o       <= #UDLY 32'd0;
        payload_word_count_o <= #UDLY 32'd16;
      end else if (cmd_fire_w && cmd_event_config_w &&
                   (device_state_r == LOCKSTEP_STATE_CONFIGURED) && event_config_valid_w) begin
        frame_type_o         <= #UDLY LOCKSTEP_FRAME_STATUS_RSP;
        frame_capture_id_o   <= #UDLY active_capture_id_r;
        frame_flags_o        <= #UDLY 32'd0;
        payload_word_count_o <= #UDLY 32'd16;
      end else if (cmd_fire_catchall_w) begin
        // 无法识别的命令或状态不匹配 → ERROR_RSP
        frame_type_o        <= #UDLY LOCKSTEP_FRAME_ERROR_RSP;
        frame_capture_id_o  <= #UDLY 32'd0;
        frame_flags_o       <= #UDLY LOCKSTEP_FRAME_FLAG_ERROR;
        payload_word_count_o <= #UDLY 32'd16;
      end

      // 配置校验结果（独立于命令处理链）
      if ((cur_state == ST_WAIT_CFG_RESULT) && cfg_error_valid_i) begin
        // 配置校验失败 → ERROR_RSP
        frame_type_o        <= #UDLY LOCKSTEP_FRAME_ERROR_RSP;
        frame_capture_id_o  <= #UDLY 32'd0;
        frame_flags_o       <= #UDLY LOCKSTEP_FRAME_FLAG_ERROR;
        payload_word_count_o <= #UDLY 32'd8;
      end else if ((cur_state == ST_WAIT_CFG_RESULT) && !cfg_error_valid_i) begin
        // 配置校验成功 → CONFIGURED STATUS_RSP
        frame_type_o         <= #UDLY LOCKSTEP_FRAME_STATUS_RSP;
        frame_capture_id_o   <= #UDLY 32'd0;
        frame_flags_o        <= #UDLY 32'd0;
        payload_word_count_o <= #UDLY 32'd16;
      end else if ((cur_state == ST_WAIT_ARM_ACCEPT) && arm_accepted_i) begin
        // 采样域接受 ARM → CAPTURING STATUS_RSP
        frame_type_o         <= #UDLY LOCKSTEP_FRAME_STATUS_RSP;
        frame_capture_id_o   <= #UDLY active_capture_id_r;
        frame_flags_o        <= #UDLY 32'd0;
        payload_word_count_o <= #UDLY 32'd16;
      end
      if (capture_domain_fault_w) begin
        frame_type_o         <= #UDLY LOCKSTEP_FRAME_ERROR_RSP;
        frame_capture_id_o   <= #UDLY active_capture_id_r;
        frame_flags_o        <= #UDLY LOCKSTEP_FRAME_FLAG_ERROR;
        payload_word_count_o <= #UDLY 32'd8;
      end
    end
  end

  //==================================================================
  // FSM Block 3g: 帧 payload 寄存器 — payload0~15，按帧类型填充数据字段
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      payload0_o  <= #UDLY 32'd0;
      payload1_o  <= #UDLY 32'd0;
      payload2_o  <= #UDLY 32'd0;
      payload3_o  <= #UDLY 32'd0;
      payload4_o  <= #UDLY 32'd0;
      payload5_o  <= #UDLY 32'd0;
      payload6_o  <= #UDLY 32'd0;
      payload7_o  <= #UDLY 32'd0;
      payload8_o  <= #UDLY 32'd0;
      payload9_o  <= #UDLY 32'd0;
      payload10_o <= #UDLY 32'd0;
      payload11_o <= #UDLY 32'd0;
      payload12_o <= #UDLY 32'd0;
      payload13_o <= #UDLY 32'd0;
      payload14_o <= #UDLY 32'd0;
      payload15_o <= #UDLY 32'd0;
    end else begin
      // 优先级链：仅更新 payload 字段
      if (capture_done_with_error_w) begin
        // 延迟错误 → ERROR_RSP payload
        payload0_o  <= #UDLY deferred_error_code_r;
        payload1_o  <= #UDLY {16'd0, deferred_error_type_r};
        payload2_o  <= #UDLY deferred_error_seq_r;
        payload3_o  <= #UDLY deferred_error_state_r;
        payload4_o  <= #UDLY deferred_error_detail0_r;
        payload5_o  <= #UDLY deferred_error_detail1_r;
        payload6_o  <= #UDLY 32'd0;
        payload7_o  <= #UDLY 32'd0;
        payload8_o  <= #UDLY 32'd0;
        payload9_o  <= #UDLY 32'd0;
      end else if (cmd_error_idle_w) begin
        // 空闲时错误 → ERROR_RSP payload
        payload0_o  <= #UDLY cmd_error_code_i;
        payload1_o  <= #UDLY {16'd0, cmd_error_type_i};
        payload2_o  <= #UDLY cmd_error_seq_i;
        payload3_o  <= #UDLY device_state_r;
        payload4_o  <= #UDLY cmd_error_detail0_i;
        payload5_o  <= #UDLY cmd_error_detail1_i;
        payload6_o  <= #UDLY 32'd0;
        payload7_o  <= #UDLY 32'd0;
      end else if (cmd_fire_w && cmd_hello_w && !device_active_w) begin
        // HELLO_RSP payload：返回设备能力信息
        payload0_o  <= #UDLY LOCKSTEP_PROTOCOL_VERSION;
        payload1_o  <= #UDLY HELLO_PHYSICAL_CHANNELS;
        payload2_o  <= #UDLY HELLO_SAMPLE_WORD_BITS;
        payload3_o  <= #UDLY SUPPORTED_MODES;
        payload4_o  <= #UDLY MIN_SAMPLE_RATE_HZ;
        payload5_o  <= #UDLY MAX_VERIFIED_SAMPLE_RATE_HZ;
        payload6_o  <= #UDLY MAX_SAMPLE_COUNT;
        payload7_o  <= #UDLY LOCKSTEP_MAX_PAYLOAD_BYTES_INITIAL;
      end else if (cmd_fire_w && cmd_status_w) begin
        // STATUS_RSP payload：返回当前设备状态
        payload0_o  <= #UDLY device_state_r;
        payload1_o  <= #UDLY cmd_seq_i;
        payload2_o  <= #UDLY active_capture_id_r;
        payload3_o  <= #UDLY capture_samples_captured_i;
        payload4_o  <= #UDLY capture_samples_uploaded_i;
        payload5_o  <= #UDLY last_device_status_flags_r;
        payload6_o  <= #UDLY last_error_code_r;
        payload7_o  <= #UDLY debug_state_o;
        payload8_o  <= #UDLY capture_window_state_i;
        payload9_o  <= #UDLY capture_debug_flags_i;
        payload10_o <= #UDLY capture_pretrigger_samples_i;
        payload11_o <= #UDLY capture_posttrigger_samples_i;
        payload12_o <= #UDLY capture_frame_source_state_i;
        payload13_o <= #UDLY capture_tx_generator_state_i;
        payload14_o <= #UDLY capture_ft601_state_i;
        payload15_o <= #UDLY capture_tx_bytes_i;
      end else if (cmd_fire_w && cmd_event_config_w &&
                   (device_state_r == LOCKSTEP_STATE_CONFIGURED) && event_config_valid_w) begin
        payload0_o  <= #UDLY device_state_r;
        payload1_o  <= #UDLY cmd_seq_i;
        payload2_o  <= #UDLY active_capture_id_r;
        payload3_o  <= #UDLY capture_samples_captured_i;
        payload4_o  <= #UDLY capture_samples_uploaded_i;
        payload5_o  <= #UDLY last_device_status_flags_r;
        payload6_o  <= #UDLY LOCKSTEP_ERR_NONE;
        payload7_o  <= #UDLY debug_state_o;
        payload8_o  <= #UDLY capture_window_state_i;
        payload9_o  <= #UDLY {23'd0, cmd_payload0_i[8:0]};
        payload10_o <= #UDLY cmd_payload1_i;
        payload11_o <= #UDLY cmd_payload2_i;
        payload12_o <= #UDLY cmd_payload3_i;
        payload13_o <= #UDLY 32'h0000019f;
        payload14_o <= #UDLY 32'h00000060;
        payload15_o <= #UDLY 32'd64;
      end else if (cmd_fire_catchall_w) begin
        // 无法识别的命令 → ERROR_RSP payload
        if (cmd_arm_w && !capture_domain_ready_i &&
            (device_state_r == LOCKSTEP_STATE_CONFIGURED)) begin
          payload0_o <= #UDLY LOCKSTEP_ERR_INTERNAL;
          payload4_o <= #UDLY LOCKSTEP_DEVICE_STATUS_FLAG_FATAL_INTERNAL_ERROR;
        end else if (cmd_config_w && !config_supported_w) begin
          // 不支持的采集模式
          payload0_o <= #UDLY LOCKSTEP_ERR_UNSUPPORTED_MODE;
          payload4_o <= #UDLY SUPPORTED_MODES;
        end else if (cmd_config_w) begin
          payload0_o <= #UDLY LOCKSTEP_ERR_BAD_TRIGGER_CONFIG;
          payload4_o <= #UDLY cmd_payload_words_i;
        end else if (cmd_event_config_w) begin
          payload0_o <= #UDLY LOCKSTEP_ERR_BAD_TRIGGER_CONFIG;
          payload4_o <= #UDLY cmd_payload0_i;
        end else begin
          // 状态不匹配
          payload0_o <= #UDLY LOCKSTEP_ERR_BAD_STATE;
          payload4_o <= #UDLY device_state_r;
        end
        payload1_o <= #UDLY {16'd0, cmd_type_i};
        payload2_o <= #UDLY cmd_seq_i;
        payload3_o <= #UDLY device_state_r;
        payload5_o <= #UDLY 32'd0;
        payload6_o <= #UDLY 32'd0;
        payload7_o <= #UDLY 32'd0;
      end

      // 配置校验结果（独立于命令处理链）
      if ((cur_state == ST_WAIT_CFG_RESULT) && cfg_error_valid_i) begin
        // 配置校验失败 → ERROR_RSP payload
        payload0_o <= #UDLY cfg_error_code_i;
        payload1_o <= #UDLY {16'd0, LOCKSTEP_FRAME_CONFIG_CAPTURE};
        payload2_o <= #UDLY 32'd0;
        payload3_o <= #UDLY LOCKSTEP_STATE_CONFIGURED;
        payload4_o <= #UDLY cfg_error_detail0_i;
        payload5_o <= #UDLY cfg_error_detail1_i;
        payload6_o <= #UDLY 32'd0;
        payload7_o <= #UDLY 32'd0;
      end else if ((cur_state == ST_WAIT_CFG_RESULT) && !cfg_error_valid_i) begin
        // 配置校验成功 → STATUS_RSP payload
        payload0_o <= #UDLY LOCKSTEP_STATE_CONFIGURED;
        payload1_o <= #UDLY cmd_seq_i;
        payload2_o <= #UDLY active_capture_id_r;
        payload3_o <= #UDLY capture_samples_captured_i;
        payload4_o <= #UDLY capture_samples_uploaded_i;
        payload5_o <= #UDLY last_device_status_flags_r;
        payload6_o <= #UDLY LOCKSTEP_ERR_NONE;
        payload7_o <= #UDLY debug_state_o;
        payload8_o <= #UDLY capture_window_state_i;
        payload9_o <= #UDLY capture_debug_flags_i;
        payload10_o <= #UDLY capture_pretrigger_samples_i;
        payload11_o <= #UDLY capture_posttrigger_samples_i;
        payload12_o <= #UDLY capture_frame_source_state_i;
        payload13_o <= #UDLY capture_tx_generator_state_i;
        payload14_o <= #UDLY capture_ft601_state_i;
        payload15_o <= #UDLY capture_tx_bytes_i;
      end else if ((cur_state == ST_WAIT_ARM_ACCEPT) && arm_accepted_i) begin
        // 采样域接受 ARM → STATUS_RSP payload
        payload0_o <= #UDLY LOCKSTEP_STATE_CAPTURING_PRETRIGGER;
        payload1_o <= #UDLY cmd_seq_i;
        payload2_o <= #UDLY active_capture_id_r;
        payload3_o <= #UDLY capture_samples_captured_i;
        payload4_o <= #UDLY capture_samples_uploaded_i;
        payload5_o <= #UDLY capture_device_status_flags_i;
        payload6_o <= #UDLY LOCKSTEP_ERR_NONE;
        payload7_o <= #UDLY debug_state_o;
        payload8_o <= #UDLY capture_window_state_i;
        payload9_o <= #UDLY capture_debug_flags_i;
        payload10_o <= #UDLY capture_pretrigger_samples_i;
        payload11_o <= #UDLY capture_posttrigger_samples_i;
        payload12_o <= #UDLY capture_frame_source_state_i;
        payload13_o <= #UDLY capture_tx_generator_state_i;
        payload14_o <= #UDLY capture_ft601_state_i;
        payload15_o <= #UDLY capture_tx_bytes_i;
      end
      if (capture_domain_fault_w) begin
        payload0_o <= #UDLY LOCKSTEP_ERR_INTERNAL;
        payload1_o <= #UDLY {16'd0, LOCKSTEP_FRAME_ARM_CAPTURE};
        payload2_o <= #UDLY active_capture_id_r;
        payload3_o <= #UDLY device_state_r;
        payload4_o <= #UDLY LOCKSTEP_DEVICE_STATUS_FLAG_FATAL_INTERNAL_ERROR;
        payload5_o <= #UDLY capture_window_state_i;
        payload6_o <= #UDLY capture_debug_flags_i;
        payload7_o <= #UDLY 32'd0;
      end
    end
  end

endmodule
