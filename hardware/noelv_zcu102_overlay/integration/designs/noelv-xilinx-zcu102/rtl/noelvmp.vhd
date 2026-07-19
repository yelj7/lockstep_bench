-------------------------------------------------------------------------------
-- 文件名: noelvmp.vhd
-- 日期: 2026-07-15
-- 版本: 1.1
-- 更新记录: 接入 1024-bit LOCKSTEP 采样，并按实际配置标记协议真实来源。
-- 描述: ZCU102 NOEL-V 顶层及 FT601 采集链路集成。
-------------------------------------------------------------------------------

------------------------------------------------------------------------------
--  This file is a part of the GRLIB VHDL IP LIBRARY
--  Copyright (C) 2003 - 2008, Gaisler Research
--  Copyright (C) 2008 - 2014, Aeroflex Gaisler
--  Copyright (C) 2015 - 2023, Cobham Gaisler
--  Copyright (C) 2023 - 2025, Frontgrade Gaisler
--
--  This program is free software; you can redistribute it and/or modify
--  it under the terms of the GNU General Public License as published by
--  the Free Software Foundation; version 2.
--
--  This program is distributed in the hope that it will be useful,
--  but WITHOUT ANY WARRANTY; without even the implied warranty of
--  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
--  GNU General Public License for more details.
--
--  You should have received a copy of the GNU General Public License
--  along with this program; if not, write to the Free Software
--  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 

library ieee;
use ieee.std_logic_1164.all;

library grlib;
use grlib.amba.all;
use grlib.stdlib.all;
use grlib.devices.all;
use grlib.config.all;
use grlib.config_types.all;

library techmap;
use techmap.gencomp.all;

library unisim;
use unisim.vcomponents.all;

library gaisler;
use gaisler.leon3.all;
use gaisler.uart.all;
use gaisler.misc.all;
use gaisler.net.all;
use gaisler.jtag.all;
use gaisler.axi.all;
use gaisler.plic.all;
use gaisler.l2cache.all;
use gaisler.noelv.all;
use gaisler.can.all;

--pragma translate_off
use gaisler.sim.all;
--pragma translate_on

use work.config.all;
use work.config_local.all;
use work.rev.REVISION;
use work.cfgmap.all;

entity noelvmp is
  generic (
    fabtech                 : integer := CFG_FABTECH;
    memtech                 : integer := CFG_MEMTECH;
    padtech                 : integer := CFG_PADTECH;
    clktech                 : integer := CFG_CLKTECH;
    disas                   : integer := CFG_DISAS;     -- Enable disassembly to console
    SIMULATION              : integer := 0
    -- pragma translate_off 
    + CFG_MIG_7SERIES_MODEL
    ; ramfile               : string  := "ram.srec"
    ; romfile               : string  := "prom.srec"
    -- pragma translate_on
    );
  port (
    -- Clock and Reset
    reset       : in    std_ulogic;
    clk300p     : in    std_ulogic;  -- 300 MHz clock
    clk300n     : in    std_ulogic;  -- 300 MHz clock
    -- Switches
    switch      : in    std_logic_vector(6 downto 0);
    -- LEDs
    led         : out   std_logic_vector(7 downto 0);
    -- GPIOs
    gpio        : inout std_logic_vector(15 downto 0);
    -- I2C
    iic_scl     : inout std_logic;
    iic_sda     : inout std_logic;
    -- Ethernet
    eth_mdio    : inout std_logic;
    eth_mdc     : out   std_logic;
    eth_rxc     : in    std_logic;
    eth_rx_ctl  : in    std_logic;
    eth_rxd     : in    std_logic_vector(3 downto 0);
    eth_txc     : out   std_logic;
    eth_tx_ctl  : out   std_logic;
    eth_txd     : out   std_logic_vector(3 downto 0);
    -- UART
    dsurx       : in    std_ulogic; 
    dsutx       : out   std_ulogic;
    dsuctsn     : in    std_ulogic; 
    dsurtsn     : out   std_ulogic; 
    -- CAN
    can_rxi : in  std_logic;
    can_txo : out std_logic;
    -- SPI
    spi_miso : in  std_logic;
    spi_mosi : out std_logic;
    spi_sck  : out std_logic;
    spi_sel  : out std_logic_vector(0 downto 0);
    -- Push Buttons (Active High)
    button      : in    std_logic_vector(4 downto 0);
    -- DDR4 (MIG)
    ddr4_dq     : inout std_logic_vector(15 downto 0);
    ddr4_dqs_c  : inout std_logic_vector(1 downto 0); -- Data Strobe
    ddr4_dqs_t  : inout std_logic_vector(1 downto 0); -- Data Strobe
    ddr4_addr   : out   std_logic_vector(13 downto 0);-- Address
    ddr4_ras_n  : out   std_ulogic;
    ddr4_cas_n  : out   std_ulogic;
    ddr4_we_n   : out   std_ulogic;
    ddr4_ba     : out   std_logic_vector(1 downto 0); -- Device bank address per group
    ddr4_bg     : out   std_logic_vector(0 downto 0); -- Device bank group address
    ddr4_dm_n   : inout std_logic_vector(1 downto 0); -- Data Mask
    ddr4_ck_c   : out   std_logic_vector(0 downto 0); -- Clock Negative Edge
    ddr4_ck_t   : out   std_logic_vector(0 downto 0); -- Clock Positive Edge
    ddr4_cke    : out   std_logic_vector(0 downto 0); -- Clock Enable
    ddr4_act_n  : out   std_ulogic;                   -- Command Input
    --ddr4_alert_n: in    std_ulogic;                   -- Alert Output
    ddr4_odt    : out   std_logic_vector(0 downto 0); -- On-die Termination
    ddr4_par    : out   std_ulogic;                   -- Parity for cmd and addr
    --ddr4_ten    : out   std_ulogic;                   -- Connectivity Test Mode
    ddr4_cs_n   : out   std_logic_vector(0 downto 0); -- Chip Select
    ddr4_reset_n: out   std_ulogic;                   -- Asynchronous Reset
    -- FT601 / COM006
    ft_clk      : in    std_ulogic;
    ft_txe_n_i  : in    std_ulogic;
    ft_rxf_n_i  : in    std_ulogic;
    ft_data_io  : inout std_logic_vector(31 downto 0);
    ft_be_io    : inout std_logic_vector(3 downto 0);
    ft_oe_n_o   : out   std_ulogic;
    ft_rd_n_o   : out   std_ulogic;
    ft_wr_n_o   : out   std_ulogic;
    ft_siwu_n_o : out   std_ulogic;
    ft_wakeup_n_o : out std_ulogic
    );
end;

architecture rtl of noelvmp is
  constant OEPOL        : integer := padoen_polarity(padtech);
  constant BOARD_FREQ   : integer := 300000; -- input frequency in KHz
  constant CPU_FREQ     : integer := BOARD_FREQ * CFG_CLKMUL / CFG_CLKDIV; -- cpu frequency in KHz
  constant oeon         : std_logic := conv_std_logic_vector(OEPOL,1)(0);
  constant oeoff        : std_logic := not conv_std_logic_vector(OEPOL,1)(0);

  -------------------------------------
  -- Misc
  signal vcc            : std_ulogic;
  signal gnd            : std_ulogic;
  signal stati          : ahbstat_in_type;
  -- Clocks and Reset
  signal clkm           : std_ulogic
  -- pragma translate_off 
  := '0'
  -- pragma translate_on
  ;
  signal rstn           : std_ulogic;
  signal clk_300        : std_ulogic;
  signal cgi            : clkgen_in_type;
  signal cgo            : clkgen_out_type;
  signal eth_mmcm_fb    : std_ulogic;
  signal eth_mmcm_fb_buf : std_ulogic;
  signal eth_gtx_clk_raw : std_ulogic;
  signal eth_tx_clk_25_raw : std_ulogic;
  signal eth_gtx_clk    : std_ulogic;
  signal eth_tx_clk_25  : std_ulogic;
  signal eth_clk_mmcm_locked : std_ulogic;
  signal clklock        : std_ulogic;
  signal lock           : std_ulogic;
  signal lclk           : std_ulogic;
  signal rst            : std_ulogic;
  signal resetn         : std_ulogic;
  signal clkref         : std_ulogic;
  signal calib_done     : std_logic;
  signal migrstn        : std_ulogic;

  -- UART
  signal dsu_sel        : std_ulogic;
  signal uart_rx    : std_logic_vector(0 downto 0);
  signal uart_ctsn  : std_logic_vector(0 downto 0);
  signal uart_tx    : std_logic_vector(0 downto 0);
  signal uart_rtsn  : std_logic_vector(0 downto 0);

  signal duart_rx   : std_ulogic;
  signal duart_tx   : std_ulogic;
  -- CAN
  signal can_rxi_int      : std_logic;
  signal can_txo_int      : std_logic;
  -- SPI
  signal spi_miso_int     : std_logic;
  signal spi_mosi_int     : std_logic;
  signal spi_sck_int      : std_logic;
  signal spi_sel_int      : std_logic_vector(0 downto 0);
  -- I2C
  signal i2c_scl_i        : std_logic;
  signal i2c_scl_o        : std_logic;
  signal i2c_scl_oe       : std_logic;
  signal i2c_sda_i        : std_logic;
  signal i2c_sda_o        : std_logic;
  signal i2c_sda_oe       : std_logic;
  -- GPIO
  signal gpio_i         : std_logic_vector(CFG_GRGPIO_WIDTH-1 downto 0);
  signal gpio_o         : std_logic_vector(CFG_GRGPIO_WIDTH-1 downto 0);
  signal gpio_oe        : std_logic_vector(CFG_GRGPIO_WIDTH-1 downto 0);
  -- JTAG
  signal tck, tms, tdi, tdo : std_ulogic;
  -- RISC-V JTAG
  signal jtag_rv_tck    : std_ulogic := '0';
  signal jtag_rv_tms    : std_ulogic := '0';
  signal jtag_rv_tdi    : std_ulogic := '0';
  signal jtag_rv_tdo    : std_ulogic;
  -- Ethernet
  signal gmiii : eth_in_type;
  signal gmiio : eth_out_type;
  signal rgmiii : eth_in_type;
  signal rgmiio : eth_out_type;
  signal eth_apbi       : apb_slv_in_type;
  signal eth_apbo       : apb_slv_out_type := apb_none;
  signal debug_rgmii_phy_tx : std_logic_vector(31 downto 0);
  signal debug_rgmii_phy_rx : std_logic_vector(31 downto 0);
  signal eth_rst_cnt    : integer range 0 to CPU_FREQ := 0;
  signal eth_rstn_delayed : std_logic := '0';

  -- Memory
  signal mem_aximi      : axi_somi_type;
  signal mem_aximo      : axi_mosi_type;
  signal mem_ahbsi0     : ahb_slv_in_type;
  signal mem_ahbso0     : ahb_slv_out_type;
  signal mem_apbi0      : apb_slv_in_type;
  signal mem_apbo0      : apb_slv_out_type;
  signal rom_ahbsi1     : ahb_slv_in_type;
  signal rom_ahbso1     : ahb_slv_out_type;

  signal uart_rx_int    : std_ulogic; 
  signal uart_tx_int    : std_ulogic; 
  signal uart_ctsn_int  : std_ulogic;
  signal uart_rtsn_int  : std_ulogic;

  signal dmen           : std_logic;
  signal dmbreak        : std_logic;
  signal cpu0errn       : std_logic;
  signal lockstep_ahb_sample_valid     : std_ulogic;
  signal lockstep_ahb_sample_abs_index : std_logic_vector(31 downto 0);
  signal lockstep_ahb_sample           : std_logic_vector(511 downto 0);
  signal lockstep_protocol_probe       : std_logic_vector(511 downto 0);
  signal lockstep_system_sample        : std_logic_vector(1023 downto 0);
  signal lockstep_lockstep_mismatch    : std_logic_vector(4 downto 0);
  signal lockstep_ft601_debug_state    : std_logic_vector(31 downto 0);

  component mig_zcu102 
    generic(
      mem_bits  : integer := 30
    );
    port(
      calib_done          : out   std_logic;
      sys_clk_p           : in    std_logic;
      sys_clk_n           : in    std_logic;
      ddr4_addr           : out   std_logic_vector(13 downto 0);
      ddr4_we_n           : out   std_logic;
      ddr4_cas_n          : out   std_logic;
      ddr4_ras_n          : out   std_logic;
      ddr4_ba             : out   std_logic_vector(1 downto 0);
      ddr4_cke            : out   std_logic_vector(0 downto 0);
      ddr4_cs_n           : out   std_logic_vector(0 downto 0);
      ddr4_dm_n           : inout std_logic_vector(1 downto 0);
      ddr4_dq             : inout std_logic_vector(15 downto 0);
      ddr4_dqs_c          : inout std_logic_vector(1 downto 0);
      ddr4_dqs_t          : inout std_logic_vector(1 downto 0);
      ddr4_odt            : out   std_logic_vector(0 downto 0);
      ddr4_bg             : out   std_logic_vector(0 downto 0);
      ddr4_reset_n        : out   std_logic;
      ddr4_act_n          : out   std_logic;
      ddr4_ck_c           : out   std_logic_vector(0 downto 0);
      ddr4_ck_t           : out   std_logic_vector(0 downto 0);
      ddr4_ui_clk         : out   std_logic;
      ddr4_ui_clk_sync_rst: out   std_logic;
      rst_n_syn           : in    std_logic;
      rst_n_async         : in    std_logic;
      aximi               : out   axi_somi_type;
      aximo               : in    axi_mosi_type;
      -- Misc
      ddr4_ui_clkout1     : out   std_logic;
      clk_ref_i           : in    std_logic
      );
  end component;

  component lockstep_ft601_external_sample_top
    generic (
      PROBE_SAMPLE_BITS     : integer := 1024;
      MAX_PROBE_SAMPLE_BITS : integer := 1024;
      PROBE_LANE_BITS       : integer := 128;
      PROTOCOL_COUNT        : integer := 9;
      SAMPLE_ADDR_WIDTH     : integer := 12;
      LANE_INDEX_BITS       : integer := 4
    );
    port (
      ft_clk             : in    std_logic;
      rst_n              : in    std_logic;
      sample_clk         : in    std_logic;
      sample_rst_n       : in    std_logic;
      sample_valid_i     : in    std_logic;
      sample_i           : in    std_logic_vector(1023 downto 0);
      sample_abs_index_i : in    std_logic_vector(31 downto 0);
      ft_txe_n_i         : in    std_logic;
      ft_rxf_n_i         : in    std_logic;
      ft_data_io         : inout std_logic_vector(31 downto 0);
      ft_be_io           : inout std_logic_vector(3 downto 0);
      ft_oe_n_o          : out   std_logic;
      ft_rd_n_o          : out   std_logic;
      ft_wr_n_o          : out   std_logic;
      ft_siwu_n_o        : out   std_logic;
      ft_wakeup_n_o      : out   std_logic;
      debug_ft601_state_o: out   std_logic_vector(31 downto 0)
    );
  end component;

  component lockstep_protocol_probe_pack
    port (
      clk                : in    std_logic;
      rst_n              : in    std_logic;
      sample_valid_i     : in    std_logic;
      sample_abs_index_i : in    std_logic_vector(31 downto 0);
      uart_rx_i          : in    std_logic;
      uart_tx_i          : in    std_logic;
      uart_ctsn_i        : in    std_logic;
      uart_rtsn_i        : in    std_logic;
      spi_core_enabled_i : in    std_logic;
      spi_sclk_i         : in    std_logic;
      spi_mosi_i         : in    std_logic;
      spi_miso_i         : in    std_logic;
      spi_cs_n_i         : in    std_logic;
      jtag_tck_i         : in    std_logic;
      jtag_tms_i         : in    std_logic;
      jtag_tdi_i         : in    std_logic;
      jtag_tdo_i         : in    std_logic;
      rv_jtag_tck_i      : in    std_logic;
      rv_jtag_tms_i      : in    std_logic;
      rv_jtag_tdi_i      : in    std_logic;
      rv_jtag_tdo_i      : in    std_logic;
      can_core_enabled_i : in    std_logic;
      can_rx_i           : in    std_logic;
      can_tx_i           : in    std_logic;
      i2c_core_enabled_i : in    std_logic;
      i2c_scl_i          : in    std_logic;
      i2c_sda_i          : in    std_logic;
      eth_core_enabled_i : in    std_logic;
      eth_tx_en_i        : in    std_logic;
      eth_tx_er_i        : in    std_logic;
      eth_rx_dv_i        : in    std_logic;
      eth_rx_er_i        : in    std_logic;
      eth_txd_i          : in    std_logic_vector(7 downto 0);
      eth_rxd_i          : in    std_logic_vector(7 downto 0);
      probe_o            : out   std_logic_vector(511 downto 0)
    );
  end component;

  attribute mark_debug : string;
  attribute keep : string;
  attribute mark_debug of lockstep_lockstep_mismatch : signal is "true";
  attribute keep of lockstep_lockstep_mismatch : signal is "true";

begin

  ----------------------------------------------------------------------
  ---  Reset and Clock generation  -------------------------------------
  ----------------------------------------------------------------------
  vcc         <= '1';
  gnd         <= '0';
  cgi.pllctrl <= "00";
  cgi.pllrst  <= resetn;

  -- Clocks
  clk_gen : if (CFG_MIG_7SERIES = 0) or 
               ((CFG_MIG_7SERIES = 1) and (SIMULATION /= 0)) generate
    clk_pad_ds : clkpad_ds generic map (
      tech      => padtech,
      level     => sstl12_dci,
      voltage   => x12v)
      port map (clk300p, clk300n, lclk);
    clkgen0 : clkgen        -- clock generator
      generic map (clktech, CFG_CLKMUL, CFG_CLKDIV, 0,
                   CFG_CLK_NOFB, 0, 0, 0, BOARD_FREQ)
      port map (lclk, lclk, clkm, open, open, open, open, cgi, cgo, open, open, open);
  end generate;

  reset_pad : inpad
    generic map (tech => padtech, level => cmos, voltage => x18v)
    port map (reset, rst);

  resetn <= not rst;

  lock <= calib_done when CFG_MIG_7SERIES = 1 else cgo.clklock;

  rst1 : rstgen         -- reset generator
    generic map (acthigh => 1)
    port map (rst, clkm, lock, migrstn, open);

  ----------------------------------------------------------------------
  ---  NOEL-V SUBSYSTEM ------------------------------------------------
  ----------------------------------------------------------------------

  core0 : entity work.noelvcore
  generic map (
    fabtech     => CFG_FABTECH,
    memtech     => CFG_MEMTECH,
    padtech     => CFG_PADTECH,
    clktech     => CFG_CLKTECH,
    cpu_freq    => CPU_FREQ,
    devid       => NOELV_XILINX_KCU105,
    disas       => disas)
  port map (
    -- Clock & reset
    clkm        => clkm, 
    resetn      => resetn,
    lock        => lock,
    rstno       => rstn,
    -- misc
    dmen        => '1',
    dmbreak     => dmbreak,
    dmreset     => open,
    cpu0errn    => open,
    -- GPIO
    gpio_i      => gpio_i,
    gpio_o      => gpio_o,
    gpio_oe     => gpio_oe,
    -- UART
    uart_rx     => uart_rx,
    uart_ctsn   => uart_ctsn,
    uart_tx     => uart_tx,
    uart_rtsn   => uart_rtsn,
    -- CAN
    can_rxi     => can_rxi_int,
    can_txo     => can_txo_int,
    -- SPI
    spi_miso    => spi_miso_int,
    spi_mosi    => spi_mosi_int,
    spi_sck     => spi_sck_int,
    spi_sel     => spi_sel_int,
    -- I2C
    i2c_scl_i   => i2c_scl_i,
    i2c_scl_o   => i2c_scl_o,
    i2c_scl_oe  => i2c_scl_oe,
    i2c_sda_i   => i2c_sda_i,
    i2c_sda_o   => i2c_sda_o,
    i2c_sda_oe  => i2c_sda_oe,
    -- Memory controller
    mem_aximi   => mem_aximi,
    mem_aximo   => mem_aximo,
    mem_ahbsi0  => mem_ahbsi0,
    mem_ahbso0  => mem_ahbso0,
    mem_apbi0   => mem_apbi0, 
    mem_apbo0   => mem_apbo0, 
    -- PROM controller
    rom_ahbsi1  => rom_ahbsi1,
    rom_ahbso1  => rom_ahbso1,
    -- Ethernet PHY
    ethi        => gmiii,
    etho        => gmiio,
    eth_apbi    => eth_apbi,
    eth_apbo    => eth_apbo,
    -- Debug UART
    duart_rx    => duart_rx,
    duart_tx    => duart_tx,
    -- Debug JTAG
    tck         => tck,
    tms         => tms,
    tdi         => tdi,
    tdo         => tdo,
    -- RISC-V JTAG
    jtag_rv_tck => jtag_rv_tck,
    jtag_rv_tms => jtag_rv_tms,
    jtag_rv_tdi => jtag_rv_tdi,
    jtag_rv_tdo => jtag_rv_tdo,
    -- LOCKSTEP AHB full capture
    lockstep_ahb_sample_valid     => lockstep_ahb_sample_valid,
    lockstep_ahb_sample_abs_index => lockstep_ahb_sample_abs_index,
    lockstep_ahb_sample           => lockstep_ahb_sample,
    lockstep_lockstep_mismatch    => lockstep_lockstep_mismatch
  );

  lockstep_protocol_probe0 : lockstep_protocol_probe_pack
    port map (
      clk                => clkm,
      rst_n              => rstn,
      sample_valid_i     => lockstep_ahb_sample_valid,
      sample_abs_index_i => lockstep_ahb_sample_abs_index,
      uart_rx_i          => uart_rx_int,
      uart_tx_i          => uart_tx_int,
      uart_ctsn_i        => uart_ctsn_int,
      uart_rtsn_i        => uart_rtsn_int,
      spi_core_enabled_i => '1',
      spi_sclk_i         => spi_sck_int,
      spi_mosi_i         => spi_mosi_int,
      spi_miso_i         => spi_miso_int,
      spi_cs_n_i         => spi_sel_int(0),
      jtag_tck_i         => tck,
      jtag_tms_i         => tms,
      jtag_tdi_i         => tdi,
      jtag_tdo_i         => tdo,
      rv_jtag_tck_i      => jtag_rv_tck,
      rv_jtag_tms_i      => jtag_rv_tms,
      rv_jtag_tdi_i      => jtag_rv_tdi,
      rv_jtag_tdo_i      => jtag_rv_tdo,
      can_core_enabled_i => '1',
      can_rx_i           => can_rxi_int,
      can_tx_i           => can_txo_int,
      i2c_core_enabled_i => '1',
      i2c_scl_i          => i2c_scl_i,
      i2c_sda_i          => i2c_sda_i,
      eth_core_enabled_i => '0',
      eth_tx_en_i        => rgmiio.tx_en,
      eth_tx_er_i        => rgmiio.tx_er,
      eth_rx_dv_i        => rgmiii.rx_dv,
      eth_rx_er_i        => rgmiii.rx_er,
      eth_txd_i          => rgmiio.txd,
      eth_rxd_i          => rgmiii.rxd,
      probe_o            => lockstep_protocol_probe
    );

  lockstep_system_sample <= lockstep_protocol_probe & lockstep_ahb_sample;

  lockstep_ft601_capture : lockstep_ft601_external_sample_top
    generic map (
      PROBE_SAMPLE_BITS     => 1024,
      MAX_PROBE_SAMPLE_BITS => 1024,
      PROBE_LANE_BITS       => 128,
      PROTOCOL_COUNT        => 9,
      SAMPLE_ADDR_WIDTH     => 12,
      LANE_INDEX_BITS       => 4
    )
    port map (
      ft_clk             => ft_clk,
      rst_n              => resetn,
      sample_clk         => clkm,
      sample_rst_n       => rstn,
      sample_valid_i     => lockstep_ahb_sample_valid,
      sample_i           => lockstep_system_sample,
      sample_abs_index_i => lockstep_ahb_sample_abs_index,
      ft_txe_n_i         => ft_txe_n_i,
      ft_rxf_n_i         => ft_rxf_n_i,
      ft_data_io         => ft_data_io,
      ft_be_io           => ft_be_io,
      ft_oe_n_o          => ft_oe_n_o,
      ft_rd_n_o          => ft_rd_n_o,
      ft_wr_n_o          => ft_wr_n_o,
      ft_siwu_n_o        => ft_siwu_n_o,
      ft_wakeup_n_o      => ft_wakeup_n_o,
      debug_ft601_state_o=> lockstep_ft601_debug_state
    );

  --errorn_pad : odpad
  --  generic map (tech => padtech, oepol => OEPOL)
  --  port map (errorn, cpu0errn);

  --dsuen_pad : inpad
  --  generic map (tech => padtech, level => cmos, voltage => x12v)
  --  port map (switch(2), dmen);
  dmen <= '1';

  -- Button 2,3,4 are still to be assigned
  dmbreak_pad : inpad
    generic map (tech => padtech, level => cmos, voltage => x18v)
    port map (button(4), dmbreak);

  --ndreset_pad : outpad
  --  generic map (tech => padtech, level => cmos, voltage => x18v)
  --  port map (led(4), dsuo.ndmreset);

  --dmactive_pad : outpad
  --  generic map (tech => padtech, level => cmos, voltage => x18v)
  --  port map (led(5), dsuo.dmactive);

  -----------------------------------------------------------------------------
  -- Debug UART / UART --------------------------------------------------------
  -----------------------------------------------------------------------------
  sw4_pad : inpad
    generic map (tech => padtech, level => cmos, voltage => x12v)
    port map (switch(3), dsu_sel);

  uart_tx_int     <= duart_tx       when dsu_sel = '1' else uart_tx(0);
  uart_rtsn_int   <= '1'            when dsu_sel = '1' else uart_rtsn(0);  
  uart_rx(0)      <= uart_rx_int    when dsu_sel = '0' else '1';
  uart_ctsn(0)    <= uart_ctsn_int  when dsu_sel = '0' else '1';
  duart_rx        <= uart_rx_int    when dsu_sel = '1' else '1';

  dsurx_pad : inpad
    generic map (level => cmos, voltage => x18v, tech => padtech)
    port map (dsurx, uart_rx_int);
  dsutx_pad : outpad
    generic map (level => cmos, voltage => x18v, tech => padtech)
    port map (dsutx, uart_tx_int);
  dsuctsn_pad : inpad
    generic map (level => cmos, voltage => x18v, tech => padtech)
    port map (dsuctsn, uart_ctsn_int);
  dsurtsn_pad : outpad
    generic map (level => cmos, voltage => x18v, tech => padtech)
    port map (dsurtsn, uart_rtsn_int);

  dsusel_pad : outpad
    generic map (tech => padtech, level => cmos, voltage => x18v)
    port map (led(4), dsu_sel);

  -----------------------------------------------------------------------------
  -- CAN --------------------------------------------------------
  -----------------------------------------------------------------------------

  can_rx_pad : inpad
    generic map (
      tech    => padtech,
      level   => cmos,
      voltage => x18v
    )
    port map (
      can_rxi,
      can_rxi_int
    );

  can_tx_pad : outpad
    generic map (
      tech     => padtech,
      level    => cmos,
      voltage  => x18v,
      strength => 8
    )
    port map (
      can_txo,
      can_txo_int
    );

  -----------------------------------------------------------------------------
  -- SPI --------------------------------------------------------
  -----------------------------------------------------------------------------

  -- SPICTRL pads are mapped to ZCU102 J3 Bank 50 LVCMOS33 pins in hardware.
  spi_miso_pad : inpad
    generic map (
      tech    => padtech,
      level   => cmos,
      voltage => x33v
    )
    port map (
      spi_miso,
      spi_miso_int
    );

  spi_mosi_pad : outpad
    generic map (
      tech     => padtech,
      level    => cmos,
      voltage  => x33v,
      strength => 8
    )
    port map (
      spi_mosi,
      spi_mosi_int
    );

  spi_sck_pad : outpad
    generic map (
      tech     => padtech,
      level    => cmos,
      voltage  => x33v,
      strength => 8
    )
    port map (
      spi_sck,
      spi_sck_int
    );

  spi_sel_pad : outpad
    generic map (
      tech     => padtech,
      level    => cmos,
      voltage  => x33v,
      strength => 8
    )
    port map (
      spi_sel(0),
      spi_sel_int(0)
    );

  -----------------------------------------------------------------------------
  -- I2C --------------------------------------------------------
  -----------------------------------------------------------------------------

  i2c_scl_pad : iopad
    generic map (
      tech     => padtech,
      level    => cmos,
      voltage  => x18v,
      strength => 8
    )
    port map (
      iic_scl,
      i2c_scl_o,
      i2c_scl_oe,
      i2c_scl_i
    );

  i2c_sda_pad : iopad
    generic map (
      tech     => padtech,
      level    => cmos,
      voltage  => x18v,
      strength => 8
    )
    port map (
      iic_sda,
      i2c_sda_o,
      i2c_sda_oe,
      i2c_sda_i
    );

  -----------------------------------------------------------------------------
  -- DDR4 Memory Controller (MIG) ---------------------------------------------
  -----------------------------------------------------------------------------
  -- No APB interface on memory controller  
  mem_apbo0    <= apb_none;

  mig_gen : if (CFG_MIG_7SERIES = 1) and (SIMULATION = 0) generate
    ddr4c: mig_zcu102 generic map (
      mem_bits  => 30
      )
      port map (
        calib_done      => calib_done,
        sys_clk_p       => clk300p,
        sys_clk_n       => clk300n,
        ddr4_addr       => ddr4_addr,
        ddr4_we_n       => ddr4_we_n,
        ddr4_cas_n      => ddr4_cas_n,
        ddr4_ras_n      => ddr4_ras_n,
        ddr4_ba         => ddr4_ba,
        ddr4_cke        => ddr4_cke,
        ddr4_cs_n       => ddr4_cs_n,
        ddr4_dm_n       => ddr4_dm_n,
        ddr4_dq         => ddr4_dq,
        ddr4_dqs_c      => ddr4_dqs_c,
        ddr4_dqs_t      => ddr4_dqs_t,
        ddr4_odt        => ddr4_odt,
        ddr4_bg         => ddr4_bg,
        ddr4_reset_n    => ddr4_reset_n,
        ddr4_act_n      => ddr4_act_n,
        ddr4_ck_c       => ddr4_ck_c,
        ddr4_ck_t       => ddr4_ck_t,
        ddr4_ui_clk     => open,
        ddr4_ui_clk_sync_rst => open,
        rst_n_syn       => migrstn,
        rst_n_async     => resetn,
        aximi           => mem_aximi,
        aximo           => mem_aximo,
        -- Misc
        ddr4_ui_clkout1 => clkm,
        clk_ref_i       => clkref
        );
  end generate mig_gen;

  no_mig_gen : if (CFG_MIG_7SERIES = 0) generate  
    -- Tie-Off DDR4 Signals
    ddr4_addr       <= (others => '0');
    ddr4_we_n       <= '0';
    ddr4_cas_n      <= '0';
    ddr4_ras_n      <= '0';
    ddr4_ba         <= (others => '0');
    ddr4_cke        <= (others => '0');
    ddr4_cs_n       <= (others => '0');
    ddr4_dm_n       <= (others => 'Z');
    ddr4_dq         <= (others => 'Z');
    ddr4_dqs_c      <= (others => 'Z');
    ddr4_dqs_t      <= (others => 'Z');
    ddr4_odt        <= (others => '0');
    ddr4_bg         <= (others => '0');
    ddr4_reset_n    <= '1';
    ddr4_act_n      <= '1';

    ddr4_ck_outpad : outpad_ds
      generic map (tech => padtech, level => sstl12_dci, voltage => x12v)
      port map (ddr4_ck_t(0), ddr4_ck_c(0), gnd, gnd);

    calib_done <= '1';

  end generate no_mig_gen;

  led6_pad : outpad generic map (tech => padtech, level => cmos, voltage => x18v)
    port map (led(6), calib_done);
  led7_pad : outpad generic map (tech => padtech, level => cmos, voltage => x18v)
    port map (led(7), lock);

  -- For designs that have PAR connected from the FPGA to a component, SODIMM, or UDIMM,
  -- the PAR output of the FPGA should be driven low using an SSTL12 driver to ensure it
  -- is held low at the memory.

  --ddr4_ten      <= gnd;
  ddr4_par      <= gnd;
  clkref        <= gnd;
  
  -- Simulation module
  no_mig_mem_gen : if (CFG_MIG_7SERIES = 0) and (SIMULATION = 0) generate
    axi_mem_gen : if (CFG_L2_AXI = 1) generate
      mem_ahbso0 <= ahbs_none;
    end generate axi_mem_gen;

    ahb_mem_gen : if (CFG_L2_AXI = 0) generate
      ahbram1 : ahbram 
        generic map (
          hindex      => 0,
          haddr       => L2C_HADDR,
          hmask       => L2C_HMASK,
          tech        => CFG_MEMTECH,
          kbytes      => 1024)
        port map (
          rstn,
          clkm,
          mem_ahbsi0,
          mem_ahbso0);
    end generate ahb_mem_gen;
  end generate no_mig_mem_gen;

  -- Simulation module
  -- pragma translate_off
  sim_mem_gen : if (SIMULATION /= 0) generate
    calib_done  <= '1';

    axi_mem_gen : if (CFG_L2_AXI = 1) generate
      mig_axiram : aximem
        generic map (
          fname   => ramfile,
          axibits => AXIDW,
          rstmode => 0)
        port map (
          clk   => clkm,
          rst   => rstn,
          axisi => mem_aximo,
          axiso => mem_aximi);

      mem_ahbso0 <= ahbs_none;
    end generate axi_mem_gen;

    ahb_mem_gen : if (CFG_L2_AXI = 0) generate
      mig_ahbram : ahbram_sim
        generic map (
          hindex   => 0,
          haddr    => L2C_HADDR,
          hmask    => L2C_HMASK,
          tech     => 0,
          kbytes   => 1024,
          pipe     => 0,
          maccsz   => AHBDW,
          fname    => ramfile)
        port map(
          rst     => rstn,
          clk     => clkm,
          ahbsi   => mem_ahbsi0,
          ahbso   => mem_ahbso0);
    end generate ahb_mem_gen;
  end generate sim_mem_gen;
  -- pragma translate_on

  -----------------------------------------------------------------------
  --  PROM
  -----------------------------------------------------------------------

  prom_gen : if (SIMULATION = 0) generate
    rom32 : if CFG_AHBDW = 32 generate
      brom : entity work.ahbrom
        generic map (
          hindex  => 1,
          haddr   => ROM_HADDR,
          hmask   => ROM_HMASK,
          pipe    => 0)
        port map (
          rst     => rstn,
          clk     => clkm,
          ahbsi   => rom_ahbsi1,
          ahbso   => rom_ahbso1);
    end generate;
    rom64 : if CFG_AHBDW = 64 generate
      brom : entity work.ahbrom64
        generic map (
          hindex  => 1,
          haddr   => ROM_HADDR,
          hmask   => ROM_HMASK,
          pipe    => 0)
        port map (
          rst     => rstn,
          clk     => clkm,
          ahbsi   => rom_ahbsi1,
          ahbso   => rom_ahbso1);
    end generate;
    rom128 : if CFG_AHBDW = 128 generate
      brom : entity work.ahbrom128
        generic map (
          hindex  => 1,
          haddr   => ROM_HADDR,
          hmask   => ROM_HMASK,
          pipe    => 0)
        port map (
          rst     => rstn,
          clk     => clkm,
          ahbsi   => rom_ahbsi1,
          ahbso   => rom_ahbso1);
    end generate;
  end generate prom_gen;

  -- pragma translate_off
  sim_prom_gen : if (SIMULATION /= 0) generate
    mig_ahbram : ahbram_sim
      generic map (
        hindex   => 1,
        haddr    => ROM_HADDR,
        hmask    => ROM_HMASK,
        tech     => 0,
        kbytes   => 1024,
        pipe     => 0,
        maccsz   => AHBDW,
        fname    => romfile)
      port map(
        rst     => rstn,
        clk     => clkm,
        ahbsi   => rom_ahbsi1,
        ahbso   => rom_ahbso1);
  end generate sim_prom_gen;
  -- pragma translate_on

-----------------------------------------------------------------------
-- GPIO                                                                
-----------------------------------------------------------------------
  gpio0 : if CFG_GRGPIO_ENABLE /= 0 generate

    gpled_pads : for i in 0 to 3 generate
      gpled_pad : outpad
        generic map (tech => padtech, level => cmos, voltage => x18v)
        port map (led(i), gpio_o(i+16));
    end generate gpled_pads;

    gpsw_pads : for i in 0 to 2 generate
      gpsw_pad : inpad
        generic map (tech => padtech, level => cmos, voltage => x12v)
        port map (switch(i), gpio_i(i));
    end generate gpsw_pads;
    gpio_i(3) <= dsu_sel;

    gpb_pads : for i in 0 to 3 generate
      gpb_pad : inpad
        generic map (tech => padtech, level => cmos, voltage => x12v)
        port map (button(i), gpio_i(i+4));
    end generate gpb_pads;

    pio_pads : for i in 0 to 7 generate
      gpio_pad : iopad generic map (tech => padtech, level => cmos, voltage => x33v, strength => 8)
        port map (gpio(i), gpio_o(i+8), gpio_oe(i+8), gpio_i(i+8));
    end generate;

  end generate;

-----------------------------------------------------------------------
-- RISC-V JTAG
-----------------------------------------------------------------------
  --     PMOD1-J53
  -------------------
  -- TDO  1 |  7  TDI
  -- NC   2 |  8  TMS
  -- TCK  3 |  9  NC
  -- NC   4 | 10  NC
  -- GND  5 | 11  GND
  -- VCC  6 | 12  VCC
  -------------------

  rvjtag : if CFG_LOCAL_AHB_JTAG_RV = 1 generate
    --tdo_pad : iopad generic map (tech => padtech)
    --  port map (gpio(8), jtag_rv_tdo, oeon, open);
    tdo_pad : outpad generic map (tech => padtech, level => cmos, voltage => x12v, strength => 8)
        port map (gpio(8), jtag_rv_tdo);
    
    --ntrst_pad : iopad generic map (tech => padtech)
    --  port map (gpio(9), gnd, oeoff, open);
    
    --tck_pad : iopad generic map (tech => padtech)
    --  port map (gpio(10), gnd, oeoff, jtag_rv_tck);
    tck_pad : clkpad generic map (tech => padtech, level => cmos, voltage => x12v, arch => 2)
      port map (gpio(10), jtag_rv_tck);
    
    --nc3_pad : iopad generic map (tech => padtech)
    --  port map (gpio(11), gnd, oeoff, open);
    
    tdi_pad : iopad generic map (tech => padtech, level => cmos, voltage => x12v, strength => 8)
      port map (gpio(12), gnd, oeoff, jtag_rv_tdi);
    tms_pad : iopad generic map (tech => padtech, level => cmos, voltage => x12v, strength => 8)
      port map (gpio(13), gnd, oeoff, jtag_rv_tms);
    
      --nrst_pad : iopad generic map (tech => padtech)
    --  port map (gpio(14), gnd, oeoff, open);
    --nc7_pad : iopad generic map (tech => padtech)
    --  port map (gpio(15), gnd, oeoff, open);

  end generate;

-----------------------------------------------------------------------
-- ETHERNET PHY
-----------------------------------------------------------------------

  eth_enabled : if CFG_GRETH = 1 generate
  eth_mmcm : MMCME3_ADV
    generic map (
      BANDWIDTH            => "OPTIMIZED",
      CLKFBOUT_MULT_F      => 10.000,
      CLKFBOUT_PHASE       => 0.000,
      CLKFBOUT_USE_FINE_PS => "FALSE",
      CLKIN1_PERIOD        => 10.000,
      CLKIN2_PERIOD        => 0.000,
      CLKOUT0_DIVIDE_F     => 8.000,
      CLKOUT0_DUTY_CYCLE   => 0.500,
      CLKOUT0_PHASE        => 0.000,
      CLKOUT0_USE_FINE_PS  => "FALSE",
      CLKOUT1_DIVIDE       => 40,
      CLKOUT1_DUTY_CYCLE   => 0.500,
      CLKOUT1_PHASE        => 0.000,
      CLKOUT1_USE_FINE_PS  => "FALSE",
      CLKOUT2_DIVIDE       => 8,
      CLKOUT2_DUTY_CYCLE   => 0.500,
      CLKOUT2_PHASE        => 0.000,
      CLKOUT2_USE_FINE_PS  => "FALSE",
      CLKOUT3_DIVIDE       => 8,
      CLKOUT3_DUTY_CYCLE   => 0.500,
      CLKOUT3_PHASE        => 0.000,
      CLKOUT3_USE_FINE_PS  => "FALSE",
      CLKOUT4_CASCADE      => "FALSE",
      CLKOUT4_DIVIDE       => 8,
      CLKOUT4_DUTY_CYCLE   => 0.500,
      CLKOUT4_PHASE        => 0.000,
      CLKOUT4_USE_FINE_PS  => "FALSE",
      CLKOUT5_DIVIDE       => 8,
      CLKOUT5_DUTY_CYCLE   => 0.500,
      CLKOUT5_PHASE        => 0.000,
      CLKOUT5_USE_FINE_PS  => "FALSE",
      CLKOUT6_DIVIDE       => 8,
      CLKOUT6_DUTY_CYCLE   => 0.500,
      CLKOUT6_PHASE        => 0.000,
      CLKOUT6_USE_FINE_PS  => "FALSE",
      COMPENSATION         => "AUTO",
      DIVCLK_DIVIDE        => 1,
      REF_JITTER1          => 0.010,
      REF_JITTER2          => 0.010,
      SS_EN                => "FALSE",
      SS_MODE              => "CENTER_HIGH",
      SS_MOD_PERIOD        => 10000,
      STARTUP_WAIT         => "FALSE")
    port map (
      CDDCDONE     => open,
      CLKFBOUT     => eth_mmcm_fb,
      CLKFBOUTB    => open,
      CLKFBSTOPPED => open,
      CLKINSTOPPED => open,
      CLKOUT0      => eth_gtx_clk_raw,
      CLKOUT0B     => open,
      CLKOUT1      => eth_tx_clk_25_raw,
      CLKOUT1B     => open,
      CLKOUT2      => open,
      CLKOUT2B     => open,
      CLKOUT3      => open,
      CLKOUT3B     => open,
      CLKOUT4      => open,
      CLKOUT5      => open,
      CLKOUT6      => open,
      DO           => open,
      DRDY         => open,
      LOCKED       => eth_clk_mmcm_locked,
      PSDONE       => open,
      CDDCREQ      => '0',
      CLKFBIN      => eth_mmcm_fb_buf,
      CLKIN1       => clkm,
      CLKIN2       => '0',
      CLKINSEL     => '1',
      DADDR        => (others => '0'),
      DCLK         => '0',
      DEN          => '0',
      DI           => (others => '0'),
      DWE          => '0',
      PSCLK        => '0',
      PSEN         => '0',
      PSINCDEC     => '0',
      PWRDWN       => '0',
      RST          => rst);

  eth_mmcm_fb_bufg : BUFG
    port map (I => eth_mmcm_fb, O => eth_mmcm_fb_buf);

  eth_gtx_bufg : BUFG
    port map (I => eth_gtx_clk_raw, O => eth_gtx_clk);

  eth_tx25_bufg : BUFG
    port map (I => eth_tx_clk_25_raw, O => eth_tx_clk_25);

  eth_reset_delay : process(clkm, rstn, eth_clk_mmcm_locked)
  begin
    if rstn = '0' or eth_clk_mmcm_locked = '0' then
      eth_rst_cnt      <= 0;
      eth_rstn_delayed <= '0';
    elsif rising_edge(clkm) then
      if eth_rst_cnt < CPU_FREQ then
        eth_rst_cnt      <= eth_rst_cnt + 1;
        eth_rstn_delayed <= '0';
      else
        eth_rstn_delayed <= '1';
      end if;
    end if;
  end process;

  rgmiii.gtx_clk    <= eth_gtx_clk;
  rgmiii.tx_clk_25  <= eth_tx_clk_25;
  rgmiii.tx_clk_50  <= eth_tx_clk_25;
  rgmiii.tx_clk     <= '0';
  rgmiii.tx_clk_90  <= '0';
  rgmiii.tx_clk_100 <= '0';
  rgmiii.rmii_clk   <= '0';
  rgmiii.tx_dv      <= '0';
  rgmiii.rx_er      <= '0';
  rgmiii.rx_col     <= '0';
  rgmiii.rx_crs     <= '0';
  rgmiii.rx_en      <= '1';
  rgmiii.mdint      <= '1';
  rgmiii.phyrstaddr <= conv_std_logic_vector(CFG_ETH_PHY_ADDR, 5);
  rgmiii.edcladdr   <= (others => '0');
  rgmiii.edclsepahb <= '0';
  rgmiii.edcldisable <= '0';
  rgmiii.rxd(7 downto 4) <= (others => '0');

  eth_rgmii0 : rgmii_series7
    generic map (
      pindex   => GRETH_PHY_PINDEX,
      paddr    => GRETH_PHY_PADDR,
      pmask    => GRETH_PHY_PMASK,
      tech     => fabtech,
      gmii     => CFG_GRETH1G,
      abits    => 8,
      pirq     => GRETH_PHY_PIRQ,
      base10_x => 0)
    port map (
      rstn                => rstn,
      gmiii               => gmiii,
      gmiio               => gmiio,
      rgmiii              => rgmiii,
      rgmiio              => rgmiio,
      apb_clk             => clkm,
      apb_rstn            => rstn,
      apbi                => eth_apbi,
      apbo                => eth_apbo,
      debug_rgmii_phy_tx  => debug_rgmii_phy_tx,
      debug_rgmii_phy_rx  => debug_rgmii_phy_rx);

  eth_mdio_pad : iopad
    generic map (
      tech     => padtech,
      level    => cmos,
      voltage  => x18v,
      strength => 8)
    port map (
      eth_mdio,
      rgmiio.mdio_o,
      rgmiio.mdio_oe,
      rgmiii.mdio_i);

  eth_mdc_pad : outpad
    generic map (
      tech     => padtech,
      level    => cmos,
      voltage  => x18v,
      strength => 8)
    port map (
      eth_mdc,
      rgmiio.mdc);

  -- COM006 FT601 uses the former ETH reset FMC pin on ZCU102. Keep the
  -- GRETH reset delay internal for this capture build and do not drive W6.

  eth_rxc_pad : clkpad
    generic map (
      tech    => padtech,
      level   => cmos,
      voltage => x18v,
      arch    => 2)
    port map (
      eth_rxc,
      rgmiii.rx_clk);

  eth_rx_ctl_pad : inpad
    generic map (
      tech    => padtech,
      level   => cmos,
      voltage => x18v)
    port map (
      eth_rx_ctl,
      rgmiii.rx_dv);

  eth_rxd_pads : for i in 0 to 3 generate
    eth_rxd_pad : inpad
      generic map (
        tech    => padtech,
        level   => cmos,
        voltage => x18v)
      port map (
        eth_rxd(i),
        rgmiii.rxd(i));
  end generate eth_rxd_pads;

  eth_txc_pad : outpad
    generic map (
      tech     => padtech,
      level    => cmos,
      voltage  => x18v,
      strength => 8)
    port map (
      eth_txc,
      rgmiio.tx_clk);

  eth_tx_ctl_pad : outpad
    generic map (
      tech     => padtech,
      level    => cmos,
      voltage  => x18v,
      strength => 8)
    port map (
      eth_tx_ctl,
      rgmiio.tx_en);

  eth_txd_pads : for i in 0 to 3 generate
    eth_txd_pad : outpad
      generic map (
        tech     => padtech,
        level    => cmos,
        voltage  => x18v,
        strength => 8)
      port map (
        eth_txd(i),
        rgmiio.txd(i));
  end generate eth_txd_pads;
  end generate eth_enabled;

  eth_disabled : if CFG_GRETH = 0 generate
    eth_mdio   <= 'Z';
    eth_mdc    <= '0';
    eth_txc    <= '0';
    eth_tx_ctl <= '0';
    eth_txd    <= (others => '0');
  end generate eth_disabled;

-----------------------------------------------------------------------
---  Boot message  ----------------------------------------------------
-----------------------------------------------------------------------

-- pragma translate_off
  x : report_design
    generic map(
      msg1    => "NOELV/GRLIB KCU105 Demonstration design",
      fabtech => tech_table(fabtech), memtech => tech_table(memtech),
      mdel    => 1
      );
-- pragma translate_on

end rtl;
