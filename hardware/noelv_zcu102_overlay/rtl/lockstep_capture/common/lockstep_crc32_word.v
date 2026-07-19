/**********************************************************
* 文件名: lockstep_crc32_word.v
* 日期: 2026-06-03
* 版本: 0.2
* 更新记录:
*   0.1 初始版：单字组合逻辑 CRC 更新验证单元。
*   0.2 更新文件头为中文模板，转换端口列表，添加中文注释。
*
* 描述: 单字 CRC-32/ISO-HDLC 组合逻辑更新单元，用于 LOCKSTEP 帧校验。
*       LSB-first 按字节迭代，每字节按 bit 展开 8 轮移位异或。
*
*       LOCKSTEP 协议每帧的 CRC-32 校验字由这个模块逐字累加算出。
*       上位机收到帧后对 payload 做同样的 CRC 计算，
*       与硬件算的 CRC 比对——不匹配说明 USB/FT601 传输过程中数据损坏了。
*
*       此单元只做一拍的组合，给当前的CRC和32 bit字 算新的CRC。
*       被frame模块调用，lockstep_tx用它算，lockstep_rx用它验
**********************************************************/

`timescale 1ns/1ps

module lockstep_crc32_word (
  crc_i,
  data_i,
  valid_bytes_i,
  crc_o
);
  parameter UDLY = 1;

  input  [31:0] crc_i;          // 输入 CRC 初始值（上一轮结果或 0xFFFFFFFF）
  input  [31:0] data_i;         // 待校验的 32-bit 输入数据字
  input  [3:0]  valid_bytes_i;  // 有效字节掩码：bit[0]=byte0, bit[3]=byte3；1 表示该字节参与 CRC
  output [31:0] crc_o;          // 更新后的 CRC 值
  reg   [31:0] crc_o;

  // CRC-32/ISO-HDLC 反射多项式（LSB-first）：x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11 + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x + 1
  // 反射形式 0xEDB88320 用于逐位 LSB-first 处理
  localparam [31:0] CRC32_REFLECTED_POLY = 32'hedb88320;

  // 组合逻辑中间变量：CRC 累加器、当前处理字节、循环索引
  reg [31:0] crc_tmp_r;
  reg [7:0]  byte_tmp_r;
  integer    byte_idx_r;
  integer    bit_idx_r;

  // 纯组合逻辑 CRC 更新：逐字节逐位 LSB-first 处理
  always @(*) begin
    // 默认值：从输入 CRC 开始
    crc_tmp_r  = crc_i;
    byte_tmp_r = 8'd0;

    // 外层循环：按字节 0~3 依次处理，跳过 valid_bytes_i 为 0 的字节
    for (byte_idx_r = 0; byte_idx_r < 4; byte_idx_r = byte_idx_r + 1) begin
      if (valid_bytes_i[byte_idx_r]) begin
        // 根据字节序号从 32-bit 字中提取对应字节
        case (byte_idx_r)
          0: begin
            byte_tmp_r = data_i[7:0];     // 最低字节
          end

          1: begin
            byte_tmp_r = data_i[15:8];    // 次低字节
          end

          2: begin
            byte_tmp_r = data_i[23:16];   // 次高字节
          end

          default: begin
            byte_tmp_r = data_i[31:24];   // 最高字节
          end
        endcase

        // 将当前字节混入 CRC 累加器（异或到低 8 位）
        crc_tmp_r = crc_tmp_r ^ {24'd0, byte_tmp_r};

        // 内层循环：逐 bit 处理 8 轮 LSB-first 移位异或
        for (bit_idx_r = 0; bit_idx_r < 8; bit_idx_r = bit_idx_r + 1) begin
          if (crc_tmp_r[0]) begin
            // LSB 为 1：右移 1 位并异或反射多项式
            crc_tmp_r = (crc_tmp_r >> 1) ^ CRC32_REFLECTED_POLY;
          end else begin
            // LSB 为 0：仅右移 1 位
            crc_tmp_r = crc_tmp_r >> 1;
          end
        end
      end
    end

    // 输出最终 CRC 结果
    crc_o = crc_tmp_r;
  end

endmodule