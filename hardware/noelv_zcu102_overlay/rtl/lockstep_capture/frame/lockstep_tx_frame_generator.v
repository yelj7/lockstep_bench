/**********************************************************
* 文件名: lockstep_tx_frame_generator.v
* 日期: 2026-06-03
* 版本: 0.2
* 更新记录:
*   0.1 初始版：面向命令与传输仿真的通用帧生成器。
*   0.2 重构为多段 FSM：拆分 nxt_state 组合逻辑与 5 组数据通路寄存器，添加注释。
* 描述: 通用 LOCKSTEP 帧生成器——将上层组装好的 LOCKSTEP 帧（帧头+payload）
*       按 32-bit 字序列化输出。内部依次计算帧头 CRC、payload CRC，
*       然后逐字发送：8 字帧头 + N 字 payload + CRC 附加字。
**********************************************************/

`timescale 1ns/1ps

module lockstep_tx_frame_generator (
  clk,
  rst_n,
  frame_valid_i,
  frame_ready_o,
  frame_type_i,
  frame_capture_id_i,
  frame_flags_i,
  payload_word_count_i,
  payload0_i,
  payload1_i,
  payload2_i,
  payload3_i,
  payload4_i,
  payload5_i,
  payload6_i,
  payload7_i,
  payload8_i,
  payload9_i,
  payload10_i,
  payload11_i,
  payload12_i,
  payload13_i,
  payload14_i,
  payload15_i,
  tx_word_valid_o,
  tx_word_ready_i,
  tx_word_data_o,
  tx_frame_start_o,
  tx_frame_end_o,
  tx_frame_type_o,
  tx_frame_seq_o,
  debug_state_o
);
  parameter UDLY              = 1;
  parameter MAX_PAYLOAD_WORDS = 16;    // 最大 payload 字数

  input         clk;
  input         rst_n;
  input         frame_valid_i;
  output        frame_ready_o;
  input  [15:0] frame_type_i;
  input  [31:0] frame_capture_id_i;
  input  [31:0] frame_flags_i;
  input  [31:0] payload_word_count_i;
  input  [31:0] payload0_i;
  input  [31:0] payload1_i;
  input  [31:0] payload2_i;
  input  [31:0] payload3_i;
  input  [31:0] payload4_i;
  input  [31:0] payload5_i;
  input  [31:0] payload6_i;
  input  [31:0] payload7_i;
  input  [31:0] payload8_i;
  input  [31:0] payload9_i;
  input  [31:0] payload10_i;
  input  [31:0] payload11_i;
  input  [31:0] payload12_i;
  input  [31:0] payload13_i;
  input  [31:0] payload14_i;
  input  [31:0] payload15_i;
  output        tx_word_valid_o;
  input         tx_word_ready_i;
  output [31:0] tx_word_data_o;
  output        tx_frame_start_o;
  output        tx_frame_end_o;
  output [15:0] tx_frame_type_o;
  output [31:0] tx_frame_seq_o;
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
  localparam [2:0] ST_IDLE         = 3'd0; // 等待帧输入
  localparam [2:0] ST_CRC_HEADER   = 3'd1; // 计算帧头 CRC（8 字逐字送入）
  localparam [2:0] ST_CRC_PAYLOAD  = 3'd2; // 计算 payload CRC
  localparam [2:0] ST_SEND_HEADER  = 3'd3; // 逐字发送帧头
  localparam [2:0] ST_SEND_PAYLOAD = 3'd4; // 逐字发送 payload
  localparam [2:0] ST_DONE         = 3'd5; // 单帧发送完成，递增序列号

  //==================================================================
  // FSM 状态寄存器
  //==================================================================
  reg [2:0] cur_state;
  reg [2:0] nxt_state;

  //==================================================================
  // Block 3a: 帧头锁存寄存器 — 帧输入时锁存类型/ID/标志/payload字数/序号
  //==================================================================
  reg [15:0] frame_type_r;
  reg [31:0] frame_capture_id_r;
  reg [31:0] frame_flags_r;
  reg [31:0] frame_payload_words_r;
  reg [31:0] frame_seq_r;

  //==================================================================
  // Block 3b: CRC 流水线寄存器 — CRC 累加器与最终 XOROUT 结果
  //==================================================================
  reg [31:0] frame_crc_r;
  reg [31:0] frame_crc_final_r;

  //==================================================================
  // Block 3c: 字指针寄存器 — 帧头字序号、payload 字序号
  //==================================================================
  reg [3:0]  header_index_r;
  reg [31:0] payload_index_r;

  //==================================================================
  // Block 3d: Payload 锁存寄存器 — 帧输入时锁存全部 16 个 payload 字
  //==================================================================
  reg [31:0] payload0_r;
  reg [31:0] payload1_r;
  reg [31:0] payload2_r;
  reg [31:0] payload3_r;
  reg [31:0] payload4_r;
  reg [31:0] payload5_r;
  reg [31:0] payload6_r;
  reg [31:0] payload7_r;
  reg [31:0] payload8_r;
  reg [31:0] payload9_r;
  reg [31:0] payload10_r;
  reg [31:0] payload11_r;
  reg [31:0] payload12_r;
  reg [31:0] payload13_r;
  reg [31:0] payload14_r;
  reg [31:0] payload15_r;

  //==================================================================
  // Block 3e: 发送序列号计数器 — 每帧发送完成后递增
  //==================================================================
  reg [31:0] tx_seq_r;

  //==================================================================
  // 组合逻辑：帧头字 / payload 字 / 发送输出
  //==================================================================
  reg [31:0] header_word_w;
  reg [31:0] payload_word_w;
  reg [31:0] tx_word_data_o;
  reg        tx_word_valid_o;
  reg        tx_frame_start_o;
  reg        tx_frame_end_o;
  reg [15:0] tx_frame_type_o;
  reg [31:0] tx_frame_seq_o;
  reg [31:0] debug_state_o;

  wire [31:0] crc_data_w;
  wire [31:0] crc_next_w;
  wire        frame_start_w;

  // 就绪条件：IDLE 状态且 payload 字数在允许范围内
  assign frame_ready_o = (cur_state == ST_IDLE) && (payload_word_count_i <= MAX_PAYLOAD_WORDS);

  // 帧启动握手：上游有效且本地就绪
  assign frame_start_w = frame_valid_i && frame_ready_o;

  // CRC 输入数据选择：帧头阶段取 header_word_w，payload 阶段取 payload_word_w
  assign crc_data_w = (cur_state == ST_CRC_HEADER) ? header_word_w : payload_word_w;

  lockstep_crc32_word u_crc32_word (
    .crc_i         (frame_crc_r),
    .data_i        (crc_data_w),
    .valid_bytes_i (4'hf),
    .crc_o         (crc_next_w)
  );

  //------------------------------------------------------------------
  // 帧头字生成：根据 header_index_r 从锁存字段拼出 8 个帧头字
  //------------------------------------------------------------------
  always @(*) begin
    header_word_w = 32'd0;

    case (header_index_r)
      4'd0: begin
        header_word_w = LOCKSTEP_MAGIC;                                        // 字0：魔数
      end

      4'd1: begin
        if ((frame_type_r == LOCKSTEP_FRAME_CONFIG_EVENTS) ||
            (frame_type_r == LOCKSTEP_FRAME_START_EVENT_STREAM) ||
            (frame_type_r == LOCKSTEP_FRAME_EVENT_META) ||
            (frame_type_r == LOCKSTEP_FRAME_EVENT_DATA) ||
            (frame_type_r == LOCKSTEP_FRAME_EVENT_END)) begin
          header_word_w = {frame_type_r, LOCKSTEP_PROTOCOL_VERSION_V3[15:0]};
        end else begin
          header_word_w = {frame_type_r, LOCKSTEP_PROTOCOL_VERSION[15:0]};
        end
      end

      4'd2: begin
        header_word_w = 32'd32;                                           // 字2：帧头字节数（固定 32）
      end

      4'd3: begin
        header_word_w = frame_payload_words_r << 2;                       // 字3：payload 字节数
      end

      4'd4: begin
        header_word_w = frame_seq_r;                                      // 字4：帧序号
      end

      4'd5: begin
        header_word_w = frame_capture_id_r;                               // 字5：采集 ID
      end

      4'd6: begin
        header_word_w = frame_flags_r;                                    // 字6：标志位
      end

      default: begin
        // 字7：CRC 附加字（发送阶段附加，CRC 计算阶段填 0）
        if (cur_state == ST_SEND_HEADER) begin
          header_word_w = frame_crc_final_r;
        end else begin
          header_word_w = 32'd0;
        end
      end
    endcase
  end

  //------------------------------------------------------------------
  // Payload 字选择：根据 payload_index_r 低 4 位从锁存数组选字
  //------------------------------------------------------------------
  always @(*) begin
    payload_word_w = 32'd0;

    case (payload_index_r[3:0])
      4'd0:  payload_word_w = payload0_r;
      4'd1:  payload_word_w = payload1_r;
      4'd2:  payload_word_w = payload2_r;
      4'd3:  payload_word_w = payload3_r;
      4'd4:  payload_word_w = payload4_r;
      4'd5:  payload_word_w = payload5_r;
      4'd6:  payload_word_w = payload6_r;
      4'd7:  payload_word_w = payload7_r;
      4'd8:  payload_word_w = payload8_r;
      4'd9:  payload_word_w = payload9_r;
      4'd10: payload_word_w = payload10_r;
      4'd11: payload_word_w = payload11_r;
      4'd12: payload_word_w = payload12_r;
      4'd13: payload_word_w = payload13_r;
      4'd14: payload_word_w = payload14_r;
      default: payload_word_w = payload15_r;
    endcase
  end

  //------------------------------------------------------------------
  // 发送输出组合逻辑：valid / data / frame_start / frame_end 等
  //------------------------------------------------------------------
  always @(*) begin
    // 发送有效：发送帧头阶段始终有效，发送 payload 阶段仅在 payload 非空时有效
    tx_word_valid_o = (cur_state == ST_SEND_HEADER) ||
                      ((cur_state == ST_SEND_PAYLOAD) && (frame_payload_words_r != 32'd0));

    // 发送数据：帧头阶段取 header_word_w，否则取 payload_word_w
    tx_word_data_o = (cur_state == ST_SEND_HEADER) ? header_word_w : payload_word_w;

    // 帧起始：发送帧头且为第一个字（魔数字）时拉高
    tx_frame_start_o = (cur_state == ST_SEND_HEADER) && (header_index_r == 4'd0);

    // 帧结束：发送帧头最后一个字且无 payload，或发送 payload 最后一个字
    tx_frame_end_o = ((cur_state == ST_SEND_HEADER) && (header_index_r == 4'd7) && (frame_payload_words_r == 32'd0)) ||
                     ((cur_state == ST_SEND_PAYLOAD) && (payload_index_r == (frame_payload_words_r - 32'd1)));

    tx_frame_type_o = frame_type_r;
    tx_frame_seq_o  = frame_seq_r;
    debug_state_o   = {29'd0, cur_state};
  end

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
        // 收到有效帧输入，进入帧头 CRC 计算阶段
        if (frame_start_w) begin
          nxt_state = ST_CRC_HEADER;
        end
      end

      ST_CRC_HEADER: begin
        // 8 字帧头 CRC 计算完毕
        if (header_index_r == 4'd7) begin
          if (frame_payload_words_r == 32'd0) begin
            // 无 payload：直接进入帧头发送
            nxt_state = ST_SEND_HEADER;
          end else begin
            // 有 payload：继续计算 payload CRC
            nxt_state = ST_CRC_PAYLOAD;
          end
        end
      end

      ST_CRC_PAYLOAD: begin
        // 全部 payload 字 CRC 计算完毕，进入帧头发送
        if (payload_index_r == (frame_payload_words_r - 32'd1)) begin
          nxt_state = ST_SEND_HEADER;
        end
      end

      ST_SEND_HEADER: begin
        // 下游接收当前帧头字
        if (tx_word_ready_i) begin
          if (header_index_r == 4'd7) begin
            if (frame_payload_words_r == 32'd0) begin
              // 帧头发完且无 payload：帧结束
              nxt_state = ST_DONE;
            end else begin
              // 帧头发完，开始发送 payload
              nxt_state = ST_SEND_PAYLOAD;
            end
          end
        end
      end

      ST_SEND_PAYLOAD: begin
        // 下游接收当前 payload 字
        if (tx_word_ready_i) begin
          if (payload_index_r == (frame_payload_words_r - 32'd1)) begin
            // 最后一个 payload 字已发送：帧结束
            nxt_state = ST_DONE;
          end
        end
      end

      ST_DONE: begin
        // 单帧发送完成，回到 IDLE 等待下一帧
        nxt_state = ST_IDLE;
      end

      default: begin
        nxt_state = ST_IDLE;
      end
    endcase
  end

  //==================================================================
  // FSM Block 3a: 帧头锁存寄存器 — IDLE 收到帧输入时锁存
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      frame_type_r          <= #UDLY 16'd0;
      frame_capture_id_r    <= #UDLY 32'd0;
      frame_flags_r         <= #UDLY 32'd0;
      frame_payload_words_r <= #UDLY 32'd0;
      frame_seq_r           <= #UDLY 32'd0;
    end else if ((cur_state == ST_IDLE) && frame_start_w) begin
      // 锁存帧输入参数：类型、采集ID、标志、payload字数、序号
      frame_type_r          <= #UDLY frame_type_i;
      frame_capture_id_r    <= #UDLY frame_capture_id_i;
      frame_flags_r         <= #UDLY frame_flags_i;
      frame_payload_words_r <= #UDLY payload_word_count_i;
      frame_seq_r           <= #UDLY tx_seq_r;
    end
  end

  //==================================================================
  // FSM Block 3b: CRC 流水线寄存器 — 累加器与最终 XOROUT 结果
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      frame_crc_r       <= #UDLY LOCKSTEP_CRC32_INIT;
      frame_crc_final_r <= #UDLY 32'd0;
    end else begin
      // CRC 累加：在帧头 CRC 或 payload CRC 阶段逐字更新
      if ((cur_state == ST_CRC_HEADER) || (cur_state == ST_CRC_PAYLOAD)) begin
        frame_crc_r <= #UDLY crc_next_w;
      end else if ((cur_state == ST_IDLE) && frame_start_w) begin
        // 新帧开始时复位 CRC 累加器
        frame_crc_r <= #UDLY LOCKSTEP_CRC32_INIT;
      end

      // CRC 最终值锁存：帧头 CRC 完成且无 payload，或 payload CRC 完成
      if ((cur_state == ST_CRC_HEADER) && (header_index_r == 4'd7) && (frame_payload_words_r == 32'd0)) begin
        frame_crc_final_r <= #UDLY crc_next_w ^ LOCKSTEP_CRC32_XOROUT;
      end else if ((cur_state == ST_CRC_PAYLOAD) && (payload_index_r == (frame_payload_words_r - 32'd1))) begin
        frame_crc_final_r <= #UDLY crc_next_w ^ LOCKSTEP_CRC32_XOROUT;
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
      //--- header_index_r：帧头字序号 ---
      if ((cur_state == ST_IDLE) && frame_start_w) begin
        // 新帧开始：复位帧头序号
        header_index_r <= #UDLY 4'd0;
      end else if ((cur_state == ST_CRC_HEADER) && (header_index_r == 4'd7)) begin
        // 帧头 CRC 完成：复位序号供发送阶段使用
        header_index_r <= #UDLY 4'd0;
      end else if (cur_state == ST_CRC_HEADER) begin
        // 帧头 CRC 逐字递进
        header_index_r <= #UDLY header_index_r + 4'd1;
      end else if ((cur_state == ST_SEND_HEADER) && tx_word_ready_i) begin
        if (header_index_r == 4'd7) begin
          // 帧头发送完成：复位序号
          header_index_r <= #UDLY 4'd0;
        end else begin
          // 帧头逐字递进
          header_index_r <= #UDLY header_index_r + 4'd1;
        end
      end

      //--- payload_index_r：payload 字序号 ---
      if ((cur_state == ST_IDLE) && frame_start_w) begin
        // 新帧开始：复位 payload 序号
        payload_index_r <= #UDLY 32'd0;
      end else if ((cur_state == ST_CRC_HEADER) && (header_index_r == 4'd7) && (frame_payload_words_r != 32'd0)) begin
        // 帧头 CRC 完成，即将进入 payload CRC：复位序号
        payload_index_r <= #UDLY 32'd0;
      end else if ((cur_state == ST_CRC_PAYLOAD) && (payload_index_r == (frame_payload_words_r - 32'd1))) begin
        // payload CRC 完成：复位序号供发送阶段使用
        payload_index_r <= #UDLY 32'd0;
      end else if (cur_state == ST_CRC_PAYLOAD) begin
        // payload CRC 逐字递进
        payload_index_r <= #UDLY payload_index_r + 32'd1;
      end else if ((cur_state == ST_SEND_HEADER) && tx_word_ready_i && (header_index_r == 4'd7) && (frame_payload_words_r != 32'd0)) begin
        // 帧头发送完成，即将发送 payload：复位序号
        payload_index_r <= #UDLY 32'd0;
      end else if ((cur_state == ST_SEND_PAYLOAD) && tx_word_ready_i) begin
        if (payload_index_r == (frame_payload_words_r - 32'd1)) begin
          // payload 发送完成：复位序号
          payload_index_r <= #UDLY 32'd0;
        end else begin
          // payload 逐字递进
          payload_index_r <= #UDLY payload_index_r + 32'd1;
        end
      end
    end
  end

  //==================================================================
  // FSM Block 3d: Payload 锁存寄存器 — IDLE 收到帧输入时锁存全部 16 字
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      payload0_r  <= #UDLY 32'd0;
      payload1_r  <= #UDLY 32'd0;
      payload2_r  <= #UDLY 32'd0;
      payload3_r  <= #UDLY 32'd0;
      payload4_r  <= #UDLY 32'd0;
      payload5_r  <= #UDLY 32'd0;
      payload6_r  <= #UDLY 32'd0;
      payload7_r  <= #UDLY 32'd0;
      payload8_r  <= #UDLY 32'd0;
      payload9_r  <= #UDLY 32'd0;
      payload10_r <= #UDLY 32'd0;
      payload11_r <= #UDLY 32'd0;
      payload12_r <= #UDLY 32'd0;
      payload13_r <= #UDLY 32'd0;
      payload14_r <= #UDLY 32'd0;
      payload15_r <= #UDLY 32'd0;
    end else if ((cur_state == ST_IDLE) && frame_start_w) begin
      // 锁存全部 16 个 payload 输入字
      payload0_r  <= #UDLY payload0_i;
      payload1_r  <= #UDLY payload1_i;
      payload2_r  <= #UDLY payload2_i;
      payload3_r  <= #UDLY payload3_i;
      payload4_r  <= #UDLY payload4_i;
      payload5_r  <= #UDLY payload5_i;
      payload6_r  <= #UDLY payload6_i;
      payload7_r  <= #UDLY payload7_i;
      payload8_r  <= #UDLY payload8_i;
      payload9_r  <= #UDLY payload9_i;
      payload10_r <= #UDLY payload10_i;
      payload11_r <= #UDLY payload11_i;
      payload12_r <= #UDLY payload12_i;
      payload13_r <= #UDLY payload13_i;
      payload14_r <= #UDLY payload14_i;
      payload15_r <= #UDLY payload15_i;
    end
  end

  //==================================================================
  // FSM Block 3e: 发送序列号计数器 — 每帧发送完成递增
  //==================================================================
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      tx_seq_r <= #UDLY 32'd0;
    end else if (cur_state == ST_DONE) begin
      // 单帧发送完毕，序列号递增
      tx_seq_r <= #UDLY tx_seq_r + 32'd1;
    end
  end

endmodule
