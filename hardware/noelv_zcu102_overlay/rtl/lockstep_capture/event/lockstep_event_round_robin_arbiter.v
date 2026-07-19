/**********************************************************
* 文件名: lockstep_event_round_robin_arbiter.v
* 日期: 2026-07-19
* 版本: 1.0
* 更新记录: 新增九协议事件公平仲裁器。
* 描述: 对九路 FIFO 进行等权轮询，反压期间锁定授权和事件数据。
**********************************************************/

`timescale 1ns/1ps

module lockstep_event_round_robin_arbiter (
  clk,
  rst_n,
  source_valid_i,
  source_data_i,
  source_ready_o,
  event_valid_o,
  event_ready_i,
  event_data_o,
  event_source_o
);
  parameter UDLY = 1;
  parameter integer DATA_WIDTH = 512;

  input                    clk;
  input                    rst_n;
  input  [8:0]             source_valid_i;
  input  [9*DATA_WIDTH-1:0] source_data_i;
  output [8:0]             source_ready_o;
  output                   event_valid_o;
  input                    event_ready_i;
  output [DATA_WIDTH-1:0]  event_data_o;
  output [3:0]             event_source_o;

  reg [3:0] last_grant_r;
  reg       hold_valid_r;
  reg [3:0] hold_source_r;
  reg [DATA_WIDTH-1:0] hold_data_r;
  reg [3:0] select_source_w;
  reg       select_valid_w;
  reg [DATA_WIDTH-1:0] select_data_w;
  reg [8:0] source_ready_w;

  wire selected_fire_w;
  wire selected_stall_w;

  assign event_valid_o = hold_valid_r || select_valid_w;
  assign event_source_o = hold_valid_r ? hold_source_r : select_source_w;
  assign event_data_o = hold_valid_r ? hold_data_r : select_data_w;
  assign source_ready_o = source_ready_w;
  assign selected_fire_w = event_valid_o && event_ready_i;
  assign selected_stall_w = !hold_valid_r && select_valid_w && !event_ready_i;

  // 从上次授权的下一路开始扫描，所有源权重均为 1。
  always @(*) begin
    select_source_w = 4'd0;
    select_valid_w = 1'b0;

    case (last_grant_r)
      4'd0: begin
        if (source_valid_i[1]) begin select_source_w = 4'd1; select_valid_w = 1'b1; end
        else if (source_valid_i[2]) begin select_source_w = 4'd2; select_valid_w = 1'b1; end
        else if (source_valid_i[3]) begin select_source_w = 4'd3; select_valid_w = 1'b1; end
        else if (source_valid_i[4]) begin select_source_w = 4'd4; select_valid_w = 1'b1; end
        else if (source_valid_i[5]) begin select_source_w = 4'd5; select_valid_w = 1'b1; end
        else if (source_valid_i[6]) begin select_source_w = 4'd6; select_valid_w = 1'b1; end
        else if (source_valid_i[7]) begin select_source_w = 4'd7; select_valid_w = 1'b1; end
        else if (source_valid_i[8]) begin select_source_w = 4'd8; select_valid_w = 1'b1; end
        else if (source_valid_i[0]) begin select_source_w = 4'd0; select_valid_w = 1'b1; end
      end
      4'd1: begin
        if (source_valid_i[2]) begin select_source_w = 4'd2; select_valid_w = 1'b1; end
        else if (source_valid_i[3]) begin select_source_w = 4'd3; select_valid_w = 1'b1; end
        else if (source_valid_i[4]) begin select_source_w = 4'd4; select_valid_w = 1'b1; end
        else if (source_valid_i[5]) begin select_source_w = 4'd5; select_valid_w = 1'b1; end
        else if (source_valid_i[6]) begin select_source_w = 4'd6; select_valid_w = 1'b1; end
        else if (source_valid_i[7]) begin select_source_w = 4'd7; select_valid_w = 1'b1; end
        else if (source_valid_i[8]) begin select_source_w = 4'd8; select_valid_w = 1'b1; end
        else if (source_valid_i[0]) begin select_source_w = 4'd0; select_valid_w = 1'b1; end
        else if (source_valid_i[1]) begin select_source_w = 4'd1; select_valid_w = 1'b1; end
      end
      4'd2: begin
        if (source_valid_i[3]) begin select_source_w = 4'd3; select_valid_w = 1'b1; end
        else if (source_valid_i[4]) begin select_source_w = 4'd4; select_valid_w = 1'b1; end
        else if (source_valid_i[5]) begin select_source_w = 4'd5; select_valid_w = 1'b1; end
        else if (source_valid_i[6]) begin select_source_w = 4'd6; select_valid_w = 1'b1; end
        else if (source_valid_i[7]) begin select_source_w = 4'd7; select_valid_w = 1'b1; end
        else if (source_valid_i[8]) begin select_source_w = 4'd8; select_valid_w = 1'b1; end
        else if (source_valid_i[0]) begin select_source_w = 4'd0; select_valid_w = 1'b1; end
        else if (source_valid_i[1]) begin select_source_w = 4'd1; select_valid_w = 1'b1; end
        else if (source_valid_i[2]) begin select_source_w = 4'd2; select_valid_w = 1'b1; end
      end
      4'd3: begin
        if (source_valid_i[4]) begin select_source_w = 4'd4; select_valid_w = 1'b1; end
        else if (source_valid_i[5]) begin select_source_w = 4'd5; select_valid_w = 1'b1; end
        else if (source_valid_i[6]) begin select_source_w = 4'd6; select_valid_w = 1'b1; end
        else if (source_valid_i[7]) begin select_source_w = 4'd7; select_valid_w = 1'b1; end
        else if (source_valid_i[8]) begin select_source_w = 4'd8; select_valid_w = 1'b1; end
        else if (source_valid_i[0]) begin select_source_w = 4'd0; select_valid_w = 1'b1; end
        else if (source_valid_i[1]) begin select_source_w = 4'd1; select_valid_w = 1'b1; end
        else if (source_valid_i[2]) begin select_source_w = 4'd2; select_valid_w = 1'b1; end
        else if (source_valid_i[3]) begin select_source_w = 4'd3; select_valid_w = 1'b1; end
      end
      4'd4: begin
        if (source_valid_i[5]) begin select_source_w = 4'd5; select_valid_w = 1'b1; end
        else if (source_valid_i[6]) begin select_source_w = 4'd6; select_valid_w = 1'b1; end
        else if (source_valid_i[7]) begin select_source_w = 4'd7; select_valid_w = 1'b1; end
        else if (source_valid_i[8]) begin select_source_w = 4'd8; select_valid_w = 1'b1; end
        else if (source_valid_i[0]) begin select_source_w = 4'd0; select_valid_w = 1'b1; end
        else if (source_valid_i[1]) begin select_source_w = 4'd1; select_valid_w = 1'b1; end
        else if (source_valid_i[2]) begin select_source_w = 4'd2; select_valid_w = 1'b1; end
        else if (source_valid_i[3]) begin select_source_w = 4'd3; select_valid_w = 1'b1; end
        else if (source_valid_i[4]) begin select_source_w = 4'd4; select_valid_w = 1'b1; end
      end
      4'd5: begin
        if (source_valid_i[6]) begin select_source_w = 4'd6; select_valid_w = 1'b1; end
        else if (source_valid_i[7]) begin select_source_w = 4'd7; select_valid_w = 1'b1; end
        else if (source_valid_i[8]) begin select_source_w = 4'd8; select_valid_w = 1'b1; end
        else if (source_valid_i[0]) begin select_source_w = 4'd0; select_valid_w = 1'b1; end
        else if (source_valid_i[1]) begin select_source_w = 4'd1; select_valid_w = 1'b1; end
        else if (source_valid_i[2]) begin select_source_w = 4'd2; select_valid_w = 1'b1; end
        else if (source_valid_i[3]) begin select_source_w = 4'd3; select_valid_w = 1'b1; end
        else if (source_valid_i[4]) begin select_source_w = 4'd4; select_valid_w = 1'b1; end
        else if (source_valid_i[5]) begin select_source_w = 4'd5; select_valid_w = 1'b1; end
      end
      4'd6: begin
        if (source_valid_i[7]) begin select_source_w = 4'd7; select_valid_w = 1'b1; end
        else if (source_valid_i[8]) begin select_source_w = 4'd8; select_valid_w = 1'b1; end
        else if (source_valid_i[0]) begin select_source_w = 4'd0; select_valid_w = 1'b1; end
        else if (source_valid_i[1]) begin select_source_w = 4'd1; select_valid_w = 1'b1; end
        else if (source_valid_i[2]) begin select_source_w = 4'd2; select_valid_w = 1'b1; end
        else if (source_valid_i[3]) begin select_source_w = 4'd3; select_valid_w = 1'b1; end
        else if (source_valid_i[4]) begin select_source_w = 4'd4; select_valid_w = 1'b1; end
        else if (source_valid_i[5]) begin select_source_w = 4'd5; select_valid_w = 1'b1; end
        else if (source_valid_i[6]) begin select_source_w = 4'd6; select_valid_w = 1'b1; end
      end
      4'd7: begin
        if (source_valid_i[8]) begin select_source_w = 4'd8; select_valid_w = 1'b1; end
        else if (source_valid_i[0]) begin select_source_w = 4'd0; select_valid_w = 1'b1; end
        else if (source_valid_i[1]) begin select_source_w = 4'd1; select_valid_w = 1'b1; end
        else if (source_valid_i[2]) begin select_source_w = 4'd2; select_valid_w = 1'b1; end
        else if (source_valid_i[3]) begin select_source_w = 4'd3; select_valid_w = 1'b1; end
        else if (source_valid_i[4]) begin select_source_w = 4'd4; select_valid_w = 1'b1; end
        else if (source_valid_i[5]) begin select_source_w = 4'd5; select_valid_w = 1'b1; end
        else if (source_valid_i[6]) begin select_source_w = 4'd6; select_valid_w = 1'b1; end
        else if (source_valid_i[7]) begin select_source_w = 4'd7; select_valid_w = 1'b1; end
      end
      default: begin
        if (source_valid_i[0]) begin select_source_w = 4'd0; select_valid_w = 1'b1; end
        else if (source_valid_i[1]) begin select_source_w = 4'd1; select_valid_w = 1'b1; end
        else if (source_valid_i[2]) begin select_source_w = 4'd2; select_valid_w = 1'b1; end
        else if (source_valid_i[3]) begin select_source_w = 4'd3; select_valid_w = 1'b1; end
        else if (source_valid_i[4]) begin select_source_w = 4'd4; select_valid_w = 1'b1; end
        else if (source_valid_i[5]) begin select_source_w = 4'd5; select_valid_w = 1'b1; end
        else if (source_valid_i[6]) begin select_source_w = 4'd6; select_valid_w = 1'b1; end
        else if (source_valid_i[7]) begin select_source_w = 4'd7; select_valid_w = 1'b1; end
        else if (source_valid_i[8]) begin select_source_w = 4'd8; select_valid_w = 1'b1; end
      end
    endcase
  end

  // 根据选中编号取出记录，并仅向被消费的 FIFO 返回 ready。
  always @(*) begin
    case (select_source_w)
      4'd0: select_data_w = source_data_i[1*DATA_WIDTH-1:0*DATA_WIDTH];
      4'd1: select_data_w = source_data_i[2*DATA_WIDTH-1:1*DATA_WIDTH];
      4'd2: select_data_w = source_data_i[3*DATA_WIDTH-1:2*DATA_WIDTH];
      4'd3: select_data_w = source_data_i[4*DATA_WIDTH-1:3*DATA_WIDTH];
      4'd4: select_data_w = source_data_i[5*DATA_WIDTH-1:4*DATA_WIDTH];
      4'd5: select_data_w = source_data_i[6*DATA_WIDTH-1:5*DATA_WIDTH];
      4'd6: select_data_w = source_data_i[7*DATA_WIDTH-1:6*DATA_WIDTH];
      4'd7: select_data_w = source_data_i[8*DATA_WIDTH-1:7*DATA_WIDTH];
      default: select_data_w = source_data_i[9*DATA_WIDTH-1:8*DATA_WIDTH];
    endcase

    source_ready_w = 9'd0;
    if (!hold_valid_r && select_valid_w && event_ready_i) begin
      case (select_source_w)
        4'd0: source_ready_w[0] = 1'b1;
        4'd1: source_ready_w[1] = 1'b1;
        4'd2: source_ready_w[2] = 1'b1;
        4'd3: source_ready_w[3] = 1'b1;
        4'd4: source_ready_w[4] = 1'b1;
        4'd5: source_ready_w[5] = 1'b1;
        4'd6: source_ready_w[6] = 1'b1;
        4'd7: source_ready_w[7] = 1'b1;
        default: source_ready_w[8] = 1'b1;
      endcase
    end else if (hold_valid_r && event_ready_i) begin
      case (hold_source_r)
        4'd0: source_ready_w[0] = 1'b1;
        4'd1: source_ready_w[1] = 1'b1;
        4'd2: source_ready_w[2] = 1'b1;
        4'd3: source_ready_w[3] = 1'b1;
        4'd4: source_ready_w[4] = 1'b1;
        4'd5: source_ready_w[5] = 1'b1;
        4'd6: source_ready_w[6] = 1'b1;
        4'd7: source_ready_w[7] = 1'b1;
        default: source_ready_w[8] = 1'b1;
      endcase
    end
  end

  // 反压时锁定授权；握手后从该源的下一路继续轮询。
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      last_grant_r <= #UDLY 4'd8;
      hold_valid_r <= #UDLY 1'b0;
      hold_source_r <= #UDLY 4'd0;
      hold_data_r <= #UDLY {DATA_WIDTH{1'b0}};
    end else if (selected_stall_w) begin
      hold_valid_r <= #UDLY 1'b1;
      hold_source_r <= #UDLY select_source_w;
      hold_data_r <= #UDLY select_data_w;
    end else if (selected_fire_w) begin
      last_grant_r <= #UDLY event_source_o;
      hold_valid_r <= #UDLY 1'b0;
    end
  end

endmodule
