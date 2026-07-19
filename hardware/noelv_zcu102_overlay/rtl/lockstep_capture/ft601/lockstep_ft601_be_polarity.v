/**********************************************************
* 文件名: lockstep_ft601_be_polarity.v
* 日期: 2026-07-16
* 版本: 1.0
* 更新记录: 从原型前缀更名并清理乱码源码。
* 描述: 统一内部高有效 byte-enable 与板级 FT601 焊盘极性。
**********************************************************/

`timescale 1ns/1ps

module lockstep_ft601_be_polarity (
  input  [3:0] be_valid_i,
  output [3:0] be_pad_o,
  input  [3:0] be_pad_i,
  output [3:0] be_valid_o
);
  parameter FT_BE_PAD_ACTIVE_LOW = 0;

  assign be_pad_o = (FT_BE_PAD_ACTIVE_LOW != 0) ? ~be_valid_i : be_valid_i;
  assign be_valid_o = (FT_BE_PAD_ACTIVE_LOW != 0) ? ~be_pad_i : be_pad_i;
endmodule

