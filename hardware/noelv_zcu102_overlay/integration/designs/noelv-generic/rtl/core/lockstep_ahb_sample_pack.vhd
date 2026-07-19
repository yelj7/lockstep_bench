------------------------------------------------------------------------------
-- 文件名: lockstep_ahb_sample_pack.vhd
-- 日期: 2026-07-14
-- 版本: 0.1
-- 更新记录:
--   0.1 新增 NOEL-V 统一 AHB 512-bit 全采集 packer。
-- 描述: 旁路观察 GRLIB AHB 主总线截面，按固定 field map 输出 512-bit 样本。
------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library grlib;
use grlib.amba.all;

entity lockstep_ahb_sample_pack is
  port (
    clk                 : in  std_ulogic;
    rstn                : in  std_ulogic;
    ahbsi_i             : in  ahb_slv_in_type;
    ahbso_i             : in  ahb_slv_out_vector;
    ahbmi_i             : in  ahb_mst_in_type;
    ahbmo_i             : in  ahb_mst_out_vector;
    lockstep_mismatch_i : in  std_logic_vector(4 downto 0);
    sample_valid_o      : out std_ulogic;
    sample_abs_index_o  : out std_logic_vector(31 downto 0);
    sample_o            : out std_logic_vector(511 downto 0)
  );
end;

architecture rtl of lockstep_ahb_sample_pack is
  signal sample_counter : unsigned(31 downto 0);
  signal sample_valid   : std_ulogic;
  signal sample_data    : std_logic_vector(511 downto 0);
begin

  assert AHBDW = 128
    report "lockstep_ahb_sample_pack requires AHBDW=128 for fixed 512-bit field map"
    severity failure;

  assert NAHBMST = 16
    report "lockstep_ahb_sample_pack requires NAHBMST=16 for fixed 512-bit field map"
    severity failure;

  assert NAHBSLV = 16
    report "lockstep_ahb_sample_pack requires NAHBSLV=16 for fixed 512-bit field map"
    severity failure;

  assert NAHBIRQ = 32
    report "lockstep_ahb_sample_pack requires NAHBIRQ=32 for fixed 512-bit field map"
    severity failure;

  assert NAHBAMR = 4
    report "lockstep_ahb_sample_pack requires NAHBAMR=4 for fixed 512-bit field map"
    severity failure;

  sample_valid_o     <= sample_valid;
  sample_abs_index_o <= sample_data(31 downto 0);
  sample_o           <= sample_data;

  process(clk, rstn)
    variable packed_sample  : std_logic_vector(511 downto 0);
    variable selected_ready : std_ulogic;
    variable selected_resp  : std_logic_vector(1 downto 0);
    variable selected_seen  : std_ulogic;
    variable active_transfer : std_ulogic;
  begin
    if rstn = '0' then
      sample_counter <= (others => '0');
      sample_valid   <= '0';
      sample_data    <= (501 => '1', others => '0');
    elsif rising_edge(clk) then
      packed_sample  := (others => '0');
      selected_ready := '0';
      selected_resp  := (others => '0');
      selected_seen  := '0';

      for i in 0 to NAHBSLV - 1 loop
        if (ahbsi_i.hsel(i) = '1') and (selected_seen = '0') then
          selected_ready := ahbso_i(i).hready;
          selected_resp  := ahbso_i(i).hresp;
          selected_seen  := '1';
        end if;
      end loop;

      active_transfer := ahbsi_i.hready and ahbsi_i.htrans(1);

      packed_sample(31 downto 0)    := std_logic_vector(sample_counter);
      packed_sample(63 downto 32)   := ahbsi_i.haddr;
      packed_sample(191 downto 64)  := ahbsi_i.hwdata;
      packed_sample(319 downto 192) := ahbmi_i.hrdata;

      for i in 0 to NAHBSLV - 1 loop
        packed_sample(320 + i) := ahbsi_i.hsel(i);
      end loop;

      for i in 0 to NAHBMST - 1 loop
        packed_sample(336 + i) := ahbmi_i.hgrant(i);
      end loop;

      packed_sample(383 downto 352) := ahbmi_i.hirq;
      packed_sample(415 downto 384) := ahbsi_i.hirq;

      packed_sample(416)            := ahbsi_i.hwrite;
      packed_sample(418 downto 417) := ahbsi_i.htrans;
      packed_sample(421 downto 419) := ahbsi_i.hsize;
      packed_sample(424 downto 422) := ahbsi_i.hburst;
      packed_sample(428 downto 425) := ahbsi_i.hprot;
      packed_sample(429)            := ahbsi_i.hready;
      packed_sample(431 downto 430) := ahbmi_i.hresp;
      packed_sample(435 downto 432) := ahbsi_i.hmaster;
      packed_sample(436)            := ahbsi_i.hmastlock;

      for i in 0 to NAHBAMR - 1 loop
        packed_sample(437 + i) := ahbsi_i.hmbsel(i);
      end loop;

      packed_sample(441) := ahbsi_i.endian;
      packed_sample(442) := active_transfer;
      packed_sample(443) := active_transfer and not ahbsi_i.hwrite;
      packed_sample(444) := active_transfer and ahbsi_i.hwrite;
      if ahbmi_i.hresp /= HRESP_OKAY then
        packed_sample(445) := '1';
      else
        packed_sample(445) := '0';
      end if;

      for i in 0 to NAHBMST - 1 loop
        packed_sample(446 + i) := ahbmo_i(i).hbusreq;
        packed_sample(462 + i) := ahbmo_i(i).hlock;
      end loop;

      packed_sample(481)            := selected_ready;
      packed_sample(483 downto 482) := selected_resp;

      for i in 0 to NAHBSLV - 1 loop
        packed_sample(484 + i) := ahbso_i(i).hready;
      end loop;

      packed_sample(500) := '1';
      packed_sample(501) := not rstn;
      packed_sample(506 downto 502) := lockstep_mismatch_i;
      if lockstep_mismatch_i /= "00000" then
        packed_sample(507) := '1';
      else
        packed_sample(507) := '0';
      end if;

      sample_data    <= packed_sample;
      sample_valid   <= '1';
      sample_counter <= sample_counter + 1;
    end if;
  end process;

end rtl;
