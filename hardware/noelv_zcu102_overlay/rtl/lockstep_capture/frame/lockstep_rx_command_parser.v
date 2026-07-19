/**********************************************************
* 文件名: lockstep_rx_command_parser.v
* 日期: 2026-06-03
* 版本: 0.2
* 更新记录:
*   0.1 初始版：面向字对齐 LOCKSTEP 控制帧的命令解析器。
*   0.2 重构为多段 FSM：拆分 nxt_state 组合逻辑与 6 组数据通路寄存器，添加注释。
* 描述: LOCKSTEP PC-to-PL 命令帧解析器——从 FT601 32-bit 字流中解析上行命令帧。
*       顺序接收 8 字帧头 + 可选 N 字 payload，逐字计算 CRC-32 校验，
*       帧头完成后检查魔数/版本/长度/类型，payload 完成后检查 CRC，
*       输出解析成功的命令或错误报告。
**********************************************************/

`timescale 1ns/1ps

module lockstep_rx_command_parser (
  clk,
  rst_n,
  rx_word_valid_i,
  rx_word_ready_o,
  rx_word_data_i,
  rx_be_valid_i,
  cmd_valid_o,
  cmd_ready_i,
  cmd_type_o,
  cmd_seq_o,
  cmd_capture_id_o,
  cmd_payload_words_o,
  cmd_payload0_o,
  cmd_payload1_o,
  cmd_payload2_o,
  cmd_payload3_o,
  cmd_payload4_o,
  cmd_payload5_o,
  cmd_payload6_o,
  cmd_payload7_o,
  cmd_payload8_o,
  cmd_payload9_o,
  cmd_payload10_o,
  cmd_payload11_o,
  cmd_payload12_o,
  error_valid_o,
  error_ready_i,
  error_code_o,
  error_type_o,
  error_seq_o,
  error_detail0_o,
  error_detail1_o,
  debug_state_o
);
  parameter UDLY = 1;

  input         clk;
  input         rst_n;
  input         rx_word_valid_i;
  output        rx_word_ready_o;
  input  [31:0] rx_word_data_i;
  input  [3:0]  rx_be_valid_i;
  output        cmd_valid_o;
  input         cmd_ready_i;
  output [15:0] cmd_type_o;
  output [31:0] cmd_seq_o;
  output [31:0] cmd_capture_id_o;
  output [31:0] cmd_payload_words_o;
  output [31:0] cmd_payload0_o;
  output [31:0] cmd_payload1_o;
  output [31:0] cmd_payload2_o;
  output [31:0] cmd_payload3_o;
  output [31:0] cmd_payload4_o;
  output [31:0] cmd_payload5_o;
  output [31:0] cmd_payload6_o;
  output [31:0] cmd_payload7_o;
  output [31:0] cmd_payload8_o;
  output [31:0] cmd_payload9_o;
  output [31:0] cmd_payload10_o;
  output [31:0] cmd_payload11_o;
  output [31:0] cmd_payload12_o;
  output        error_valid_o;
  input         error_ready_i;
  output [31:0] error_code_o;
  output [15:0] error_type_o;
  output [31:0] error_seq_o;
  output [31:0] error_detail0_o;
  output [31:0] error_detail1_o;
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
  // FSM 状态编码：二进制编码
  //==================================================================
  localparam [2:0] ST_HEADER     = 3'd0; // 接收帧头（8 字）
  localparam [2:0] ST_PAYLOAD    = 3'd1; // 接收 payload
  localparam [2:0] ST_CMD_HOLD   = 3'd2; // 命令输出保持，等待下游就绪
  localparam [2:0] ST_ERROR_HOLD = 3'd3; // 错误输出保持，等待下游就绪

  //==================================================================
  // FSM 状态寄存器
  //==================================================================
  reg [2:0] cur_state;
  reg [2:0] nxt_state;

  //==================================================================
  // Block 3a: 帧头字段寄存器 — 逐字段捕获帧头 8 字内容
  //==================================================================
  reg [31:0] magic_r;
  reg [15:0] version_r;
  reg [15:0] type_r;
  reg [31:0] header_len_r;
  reg [31:0] payload_len_r;
  reg [31:0] payload_words_r;
  reg [31:0] seq_r;
  reg [31:0] capture_id_r;
  reg [31:0] flags_r;
  reg [31:0] crc_received_r;

  //==================================================================
  // Block 3b: CRC 寄存器 — CRC-32 累加器
  //==================================================================
  reg [31:0] crc_r;

  //==================================================================
  // Block 3c: 字指针寄存器 — 帧头字序号、payload 字序号
  //==================================================================
  reg [3:0]  header_index_r;
  reg [31:0] payload_index_r;

  //==================================================================
  // Block 3d: 命令输出寄存器 — 解析成功后的 cmd 握手信号与元数据
  //==================================================================
  reg        cmd_valid_o;
  reg [15:0] cmd_type_o;
  reg [31:0] cmd_seq_o;
  reg [31:0] cmd_capture_id_o;
  reg [31:0] cmd_payload_words_o;

  //==================================================================
  // Block 3e: Payload 输出寄存器 — 解析得到的 payload 数据字
  //==================================================================
  reg [31:0] cmd_payload0_o;
  reg [31:0] cmd_payload1_o;
  reg [31:0] cmd_payload2_o;
  reg [31:0] cmd_payload3_o;
  reg [31:0] cmd_payload4_o;
  reg [31:0] cmd_payload5_o;
  reg [31:0] cmd_payload6_o;
  reg [31:0] cmd_payload7_o;
  reg [31:0] cmd_payload8_o;
  reg [31:0] cmd_payload9_o;
  reg [31:0] cmd_payload10_o;
  reg [31:0] cmd_payload11_o;
  reg [31:0] cmd_payload12_o;

  //==================================================================
  // Block 3f: 错误输出寄存器 — 错误握手信号与错误详情
  //==================================================================
  reg        error_valid_o;
  reg [31:0] error_code_o;
  reg [15:0] error_type_o;
  reg [31:0] error_seq_o;
  reg [31:0] error_detail0_o;
  reg [31:0] error_detail1_o;

  //==================================================================
  // 组合逻辑：CRC 数据通路、头校验、调试
  //==================================================================
  reg [31:0] expected_payload_len_w;
  reg [31:0] header_error_code_w;
  reg [31:0] header_error_detail0_w;
  reg [31:0] header_error_detail1_w;
  reg [31:0] debug_state_o;

  wire        rx_word_fire_w;
  wire [31:0] crc_word_w;
  wire [31:0] crc_next_w;
  wire [31:0] crc_final_w;
  wire [15:0] current_type_w;
  wire [31:0] current_seq_w;
  wire [31:0] current_payload_len_w;
  wire [31:0] current_crc_received_w;

  // 就绪：仅在接收帧头或 payload 阶段接受新字
  assign rx_word_ready_o = (cur_state == ST_HEADER) || (cur_state == ST_PAYLOAD);

  // 字握手完成
  assign rx_word_fire_w = rx_word_valid_i && rx_word_ready_o;

  // CRC 输入数据：帧头第 8 字（CRC 字）用 0 替代（发送方 CRC 计算时该位置为 0）
  assign crc_word_w = ((cur_state == ST_HEADER) && (header_index_r == 4'd7)) ? 32'd0 : rx_word_data_i;

  // CRC 最终值：异或标准 XOROUT 掩码
  assign crc_final_w = crc_next_w ^ LOCKSTEP_CRC32_XOROUT;

  // 当前帧类型：header_index=1 时取新值，否则保持已锁存值
  assign current_type_w = ((cur_state == ST_HEADER) && (header_index_r == 4'd1)) ? rx_word_data_i[31:16] : type_r;

  // 当前帧序号：header_index=4 时取新值，否则保持已锁存值
  assign current_seq_w = ((cur_state == ST_HEADER) && (header_index_r == 4'd4)) ? rx_word_data_i : seq_r;

  // 当前 payload 字节长度：header_index=3 时取新值，否则保持已锁存值
  assign current_payload_len_w = ((cur_state == ST_HEADER) && (header_index_r == 4'd3)) ? rx_word_data_i : payload_len_r;

  // 当前接收 CRC：header_index=7 时取新值，否则保持已锁存值
  assign current_crc_received_w = ((cur_state == ST_HEADER) && (header_index_r == 4'd7)) ? rx_word_data_i : crc_received_r;

  lockstep_crc32_word u_crc32_word (
    .crc_i         (crc_r),
    .data_i        (crc_word_w),
    .valid_bytes_i (4'hf),
    .crc_o         (crc_next_w)
  );

  //------------------------------------------------------------------
  // 期望 payload 字节数查找表：根据帧类型返回该类型标准 payload 长度
  //------------------------------------------------------------------
  always @(*) begin
    expected_payload_len_w = 32'hffffffff;

    case (current_type_w)
      LOCKSTEP_FRAME_HELLO_REQ: begin
        expected_payload_len_w = LOCKSTEP_PAYLOAD_BYTES_HELLO_REQ;
      end

      LOCKSTEP_FRAME_CONFIG_CAPTURE: begin
        expected_payload_len_w = LOCKSTEP_PAYLOAD_BYTES_CONFIG_CAPTURE;
      end

      LOCKSTEP_FRAME_ARM_CAPTURE: begin
        expected_payload_len_w = LOCKSTEP_PAYLOAD_BYTES_ARM_CAPTURE;
      end

      LOCKSTEP_FRAME_STOP_CAPTURE: begin
        expected_payload_len_w = LOCKSTEP_PAYLOAD_BYTES_STOP_CAPTURE;
      end

      LOCKSTEP_FRAME_GET_STATUS: begin
        expected_payload_len_w = LOCKSTEP_PAYLOAD_BYTES_GET_STATUS;
      end

      LOCKSTEP_FRAME_CONFIG_EVENTS: begin
        expected_payload_len_w = LOCKSTEP_PAYLOAD_BYTES_CONFIG_EVENTS;
      end

      LOCKSTEP_FRAME_START_EVENT_STREAM: begin
        expected_payload_len_w = LOCKSTEP_PAYLOAD_BYTES_START_EVENT_STREAM;
      end

      default: begin
        // 未识别的帧类型：保持最大值标记，后续头校验会报 BAD_TYPE
        expected_payload_len_w = 32'hffffffff;
      end
    endcase
  end

  //------------------------------------------------------------------
  // 帧头错误检查：魔数→版本→帧头长度→类型→payload 长度
  //------------------------------------------------------------------
  always @(*) begin
    header_error_code_w    = LOCKSTEP_ERR_NONE;
    header_error_detail0_w = 32'd0;
    header_error_detail1_w = 32'd0;

    if (magic_r != LOCKSTEP_MAGIC) begin
      // 魔数不匹配
      header_error_code_w    = LOCKSTEP_ERR_BAD_MAGIC;
      header_error_detail0_w = magic_r;
      header_error_detail1_w = LOCKSTEP_MAGIC;
    end else if ((((current_type_w == LOCKSTEP_FRAME_CONFIG_EVENTS) ||
                   (current_type_w == LOCKSTEP_FRAME_START_EVENT_STREAM)) &&
                  (version_r != LOCKSTEP_PROTOCOL_VERSION_V3[15:0])) ||
                 ((current_type_w != LOCKSTEP_FRAME_CONFIG_EVENTS) &&
                  (current_type_w != LOCKSTEP_FRAME_START_EVENT_STREAM) &&
                  (version_r != LOCKSTEP_PROTOCOL_VERSION[15:0]))) begin
      // 协议版本不匹配
      header_error_code_w    = LOCKSTEP_ERR_BAD_VERSION;
      header_error_detail0_w = {16'd0, version_r};
      header_error_detail1_w = LOCKSTEP_PROTOCOL_VERSION;
    end else if (header_len_r != LOCKSTEP_FRAME_HEADER_BYTES) begin
      // 帧头长度不匹配
      header_error_code_w    = LOCKSTEP_ERR_BAD_LENGTH;
      header_error_detail0_w = header_len_r;
      header_error_detail1_w = LOCKSTEP_FRAME_HEADER_BYTES;
    end else if (expected_payload_len_w == 32'hffffffff) begin
      // 帧类型未识别
      header_error_code_w    = LOCKSTEP_ERR_BAD_TYPE;
      header_error_detail0_w = {16'd0, current_type_w};
      header_error_detail1_w = 32'd0;
    end else if ((current_payload_len_w[1:0] != 2'b00) || (current_payload_len_w != expected_payload_len_w)) begin
      // payload 长度不匹配或未字对齐
      header_error_code_w    = LOCKSTEP_ERR_BAD_LENGTH;
      header_error_detail0_w = current_payload_len_w;
      header_error_detail1_w = expected_payload_len_w;
    end
  end

  //------------------------------------------------------------------
  // 调试状态输出
  //------------------------------------------------------------------
  always @(*) begin
    debug_state_o = {29'd0, cur_state};
  end

  //==================================================================
  // FSM Block 1: 现态寄存器
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      cur_state <= #UDLY ST_HEADER;
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
      ST_HEADER: begin
        // 收到字且字节使能有效
        if (rx_word_fire_w && (rx_be_valid_i == 4'hf)) begin
          if (header_index_r == 4'd7) begin
            // 帧头最后一字接收完毕，检查帧头错误
            if (header_error_code_w != LOCKSTEP_ERR_NONE) begin
              nxt_state = ST_ERROR_HOLD;
            end else if (current_payload_len_w == 32'd0) begin
              // 无 payload：检查 CRC 后输出命令或错误
              if (crc_final_w == current_crc_received_w) begin
                nxt_state = ST_CMD_HOLD;
              end else begin
                nxt_state = ST_ERROR_HOLD;
              end
            end else begin
              // 有 payload：进入 payload 接收阶段
              nxt_state = ST_PAYLOAD;
            end
          end
          // header_index_r < 7：保持在 ST_HEADER，继续接收
        end else if (rx_word_fire_w) begin
          // 字节使能无效：进入错误保持
          nxt_state = ST_ERROR_HOLD;
        end
      end

      ST_PAYLOAD: begin
        // 收到字且字节使能有效
        if (rx_word_fire_w && (rx_be_valid_i == 4'hf)) begin
          if (payload_index_r == (payload_words_r - 32'd1)) begin
            // 最后一个 payload 字：检查 CRC
            if (crc_final_w == crc_received_r) begin
              nxt_state = ST_CMD_HOLD;
            end else begin
              nxt_state = ST_ERROR_HOLD;
            end
          end
          // 非最后字：保持在 ST_PAYLOAD
        end else if (rx_word_fire_w) begin
          // 字节使能无效
          nxt_state = ST_ERROR_HOLD;
        end
      end

      ST_CMD_HOLD: begin
        // 下游接收命令后返回帧头接收状态
        if (cmd_ready_i) begin
          nxt_state = ST_HEADER;
        end
      end

      ST_ERROR_HOLD: begin
        // 下游接收错误后返回帧头接收状态
        if (error_ready_i) begin
          nxt_state = ST_HEADER;
        end
      end

      default: begin
        nxt_state = ST_HEADER;
      end
    endcase
  end

  //==================================================================
  // FSM Block 3a: 帧头字段寄存器 — ST_HEADER 阶段逐字捕获帧头字段
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      magic_r         <= #UDLY 32'd0;
      version_r       <= #UDLY 16'd0;
      type_r          <= #UDLY 16'd0;
      header_len_r    <= #UDLY 32'd0;
      payload_len_r   <= #UDLY 32'd0;
      payload_words_r <= #UDLY 32'd0;
      seq_r           <= #UDLY 32'd0;
      capture_id_r    <= #UDLY 32'd0;
      flags_r         <= #UDLY 32'd0;
      crc_received_r  <= #UDLY 32'd0;
    end else if (rx_word_fire_w && (rx_be_valid_i == 4'hf) && (cur_state == ST_HEADER)) begin
      // 根据帧头字序号捕获对应字段
      case (header_index_r)
        4'd0: begin
          magic_r <= #UDLY rx_word_data_i;                                // 字0：魔数
        end

        4'd1: begin
          version_r <= #UDLY rx_word_data_i[15:0];                        // 字1 低 16 位：协议版本
          type_r    <= #UDLY rx_word_data_i[31:16];                       // 字1 高 16 位：帧类型
        end

        4'd2: begin
          header_len_r <= #UDLY rx_word_data_i;                           // 字2：帧头字节数
        end

        4'd3: begin
          payload_len_r   <= #UDLY rx_word_data_i;                        // 字3：payload 字节数
          payload_words_r <= #UDLY rx_word_data_i >> 2;                   // 字3>>2：payload 字数
        end

        4'd4: begin
          seq_r <= #UDLY rx_word_data_i;                                  // 字4：帧序号
        end

        4'd5: begin
          capture_id_r <= #UDLY rx_word_data_i;                           // 字5：采集 ID
        end

        4'd6: begin
          flags_r <= #UDLY rx_word_data_i;                                // 字6：标志位
        end

        default: begin
          crc_received_r <= #UDLY rx_word_data_i;                         // 字7：接收 CRC
        end
      endcase
    end
  end

  //==================================================================
  // FSM Block 3b: CRC 寄存器 — 帧头与 payload 阶段逐字累加
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      crc_r <= #UDLY LOCKSTEP_CRC32_INIT;
    end else begin
      // 返回帧头状态时复位 CRC
      if ((cur_state == ST_CMD_HOLD) || (cur_state == ST_ERROR_HOLD)) begin
        crc_r <= #UDLY LOCKSTEP_CRC32_INIT;
      end else if (rx_word_fire_w && (rx_be_valid_i == 4'hf) && ((cur_state == ST_HEADER) || (cur_state == ST_PAYLOAD))) begin
        // 有效字接收：更新 CRC 累加器
        crc_r <= #UDLY crc_next_w;
      end
    end
  end

  //==================================================================
  // FSM Block 3c: 字指针寄存器 — 帧头字序号、payload 字序号
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      header_index_r  <= #UDLY 4'd0;
      payload_index_r <= #UDLY 32'd0;
    end else begin
      //--- 返回帧头状态时复位两个指针 ---
      if ((cur_state == ST_CMD_HOLD) || (cur_state == ST_ERROR_HOLD)) begin
        header_index_r  <= #UDLY 4'd0;
        payload_index_r <= #UDLY 32'd0;
      end else if (rx_word_fire_w && (rx_be_valid_i == 4'hf)) begin
        if (cur_state == ST_HEADER) begin
          if (header_index_r == 4'd7) begin
            // 帧头最后一字：指针保持（等待状态切换），payload 指针复位
            payload_index_r <= #UDLY 32'd0;
          end else begin
            // 帧头逐字递进
            header_index_r <= #UDLY header_index_r + 4'd1;
          end
        end else begin
          // ST_PAYLOAD：payload 字逐字递进，最后字保持指针不变
          if (payload_index_r != (payload_words_r - 32'd1)) begin
            payload_index_r <= #UDLY payload_index_r + 32'd1;
          end
        end
      end
    end
  end

  //==================================================================
  // FSM Block 3d: 命令输出寄存器 — 解析成功后输出 cmd 握手与元数据
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      cmd_valid_o         <= #UDLY 1'b0;
      cmd_type_o          <= #UDLY 16'd0;
      cmd_seq_o           <= #UDLY 32'd0;
      cmd_capture_id_o    <= #UDLY 32'd0;
      cmd_payload_words_o <= #UDLY 32'd0;
    end else begin
      // 下游就绪：清除 valid，准备下一帧
      if ((cur_state == ST_CMD_HOLD) && cmd_ready_i) begin
        cmd_valid_o <= #UDLY 1'b0;
      // 无 payload 帧头 CRC 校验通过 → 输出命令
      end else if (rx_word_fire_w && (rx_be_valid_i == 4'hf) && (cur_state == ST_HEADER) && (header_index_r == 4'd7) && (header_error_code_w == LOCKSTEP_ERR_NONE) && (current_payload_len_w == 32'd0) && (crc_final_w == current_crc_received_w)) begin
        cmd_valid_o         <= #UDLY 1'b1;
        cmd_type_o          <= #UDLY current_type_w;
        cmd_seq_o           <= #UDLY current_seq_w;
        cmd_capture_id_o    <= #UDLY capture_id_r;
        cmd_payload_words_o <= #UDLY 32'd0;
      // payload CRC 校验通过 → 输出命令
      end else if (rx_word_fire_w && (rx_be_valid_i == 4'hf) && (cur_state == ST_PAYLOAD) && (payload_index_r == (payload_words_r - 32'd1)) && (crc_final_w == crc_received_r)) begin
        cmd_valid_o         <= #UDLY 1'b1;
        cmd_type_o          <= #UDLY type_r;
        cmd_seq_o           <= #UDLY seq_r;
        cmd_capture_id_o    <= #UDLY capture_id_r;
        cmd_payload_words_o <= #UDLY payload_words_r;
      end
    end
  end

  //==================================================================
  // FSM Block 3e: Payload 输出寄存器 — ST_PAYLOAD 阶段逐字捕获
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      cmd_payload0_o  <= #UDLY 32'd0;
      cmd_payload1_o  <= #UDLY 32'd0;
      cmd_payload2_o  <= #UDLY 32'd0;
      cmd_payload3_o  <= #UDLY 32'd0;
      cmd_payload4_o  <= #UDLY 32'd0;
      cmd_payload5_o  <= #UDLY 32'd0;
      cmd_payload6_o  <= #UDLY 32'd0;
      cmd_payload7_o  <= #UDLY 32'd0;
      cmd_payload8_o  <= #UDLY 32'd0;
      cmd_payload9_o  <= #UDLY 32'd0;
      cmd_payload10_o <= #UDLY 32'd0;
      cmd_payload11_o <= #UDLY 32'd0;
      cmd_payload12_o <= #UDLY 32'd0;
    end else if (rx_word_fire_w && (rx_be_valid_i == 4'hf) && (cur_state == ST_PAYLOAD)) begin
      // 按 payload 字序号写入对应寄存器
      case (payload_index_r[3:0])
        4'd0:  cmd_payload0_o  <= #UDLY rx_word_data_i;
        4'd1:  cmd_payload1_o  <= #UDLY rx_word_data_i;
        4'd2:  cmd_payload2_o  <= #UDLY rx_word_data_i;
        4'd3:  cmd_payload3_o  <= #UDLY rx_word_data_i;
        4'd4:  cmd_payload4_o  <= #UDLY rx_word_data_i;
        4'd5:  cmd_payload5_o  <= #UDLY rx_word_data_i;
        4'd6:  cmd_payload6_o  <= #UDLY rx_word_data_i;
        4'd7:  cmd_payload7_o  <= #UDLY rx_word_data_i;
        4'd8:  cmd_payload8_o  <= #UDLY rx_word_data_i;
        4'd9:  cmd_payload9_o  <= #UDLY rx_word_data_i;
        4'd10: cmd_payload10_o <= #UDLY rx_word_data_i;
        4'd11: cmd_payload11_o <= #UDLY rx_word_data_i;
        default: cmd_payload12_o <= #UDLY rx_word_data_i;
      endcase
    end
  end

  //==================================================================
  // FSM Block 3f: 错误输出寄存器 — 各错误条件下输出 error 握手与详情
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      error_valid_o    <= #UDLY 1'b0;
      error_code_o     <= #UDLY LOCKSTEP_ERR_NONE;
      error_type_o     <= #UDLY 16'd0;
      error_seq_o      <= #UDLY 32'd0;
      error_detail0_o  <= #UDLY 32'd0;
      error_detail1_o  <= #UDLY 32'd0;
    end else begin
      // 下游就绪：清除 valid
      if ((cur_state == ST_ERROR_HOLD) && error_ready_i) begin
        error_valid_o <= #UDLY 1'b0;
      // 字节使能无效错误
      end else if (rx_word_fire_w && (rx_be_valid_i != 4'hf)) begin
        error_valid_o   <= #UDLY 1'b1;
        error_code_o    <= #UDLY LOCKSTEP_ERR_BAD_BYTE_ENABLE;
        error_type_o    <= #UDLY current_type_w;
        error_seq_o     <= #UDLY current_seq_w;
        error_detail0_o <= #UDLY {28'd0, rx_be_valid_i};
        error_detail1_o <= #UDLY 32'd0;
      // 帧头最后一字：头校验错误 或 CRC 错误（无 payload 时）
      end else if (rx_word_fire_w && (rx_be_valid_i == 4'hf) && (cur_state == ST_HEADER) && (header_index_r == 4'd7)) begin
        if (header_error_code_w != LOCKSTEP_ERR_NONE) begin
          // 帧头字段校验失败
          error_valid_o   <= #UDLY 1'b1;
          error_code_o    <= #UDLY header_error_code_w;
          error_type_o    <= #UDLY current_type_w;
          error_seq_o     <= #UDLY current_seq_w;
          error_detail0_o <= #UDLY header_error_detail0_w;
          error_detail1_o <= #UDLY header_error_detail1_w;
        end else if ((current_payload_len_w == 32'd0) && (crc_final_w != current_crc_received_w)) begin
          // 无 payload 帧 CRC 校验失败
          error_valid_o   <= #UDLY 1'b1;
          error_code_o    <= #UDLY LOCKSTEP_ERR_BAD_CRC;
          error_type_o    <= #UDLY current_type_w;
          error_seq_o     <= #UDLY current_seq_w;
          error_detail0_o <= #UDLY current_crc_received_w;
          error_detail1_o <= #UDLY crc_final_w;
        end
      // payload 最后一字：CRC 校验失败
      end else if (rx_word_fire_w && (rx_be_valid_i == 4'hf) && (cur_state == ST_PAYLOAD) && (payload_index_r == (payload_words_r - 32'd1)) && (crc_final_w != crc_received_r)) begin
        error_valid_o   <= #UDLY 1'b1;
        error_code_o    <= #UDLY LOCKSTEP_ERR_BAD_CRC;
        error_type_o    <= #UDLY type_r;
        error_seq_o     <= #UDLY seq_r;
        error_detail0_o <= #UDLY crc_received_r;
        error_detail1_o <= #UDLY crc_final_w;
      end
    end
  end

endmodule
