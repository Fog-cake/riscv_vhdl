-----------------------------------------------------------------------------
--! @file
--! @copyright Copyright 2016 GNSS Sensor Ltd. All right reserved.
--! @author    Sergey Khabarov - sergeykhbr@gmail.com
--! @brief     Branch predictor.
--! @details   This module gives about 5% of performance improvement (CPI)
------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
library commonlib;
use commonlib.types_common.all;
--! RIVER CPU specific library.
library riverlib;
--! RIVER CPU configuration constants.
use riverlib.river_cfg.all;


entity BranchPredictor is
  port (
    i_clk : in std_logic;                              -- CPU clock
    i_nrst : in std_logic;                             -- Reset. Active LOW.
    i_req_mem_fire : in std_logic;                     -- Memory request was accepted
    i_resp_mem_valid : in std_logic;                   -- Memory response from ICache is valid
    i_resp_mem_addr : in std_logic_vector(BUS_ADDR_WIDTH-1 downto 0);-- Memory response address
    i_resp_mem_data : in std_logic_vector(31 downto 0);-- Memory response value
    i_f_predic_miss : in std_logic;                    -- Fetch modul detects deviation between predicted and valid pc.
    i_e_npc : in std_logic_vector(31 downto 0);        -- Valid instruction value awaited by 'Executor'
    i_ra : in std_logic_vector(RISCV_ARCH-1 downto 0); -- Return address register value
    o_npc_predict : out std_logic_vector(31 downto 0); -- Predicted next instruction address
    o_predict : out std_logic                          -- mark requested address as predicted
  );
end; 
 
architecture arch_BranchPredictor of BranchPredictor is

  type RegistersType is record
      npc : std_logic_vector(BUS_ADDR_WIDTH-1 downto 0);
      resp_mem_addr : std_logic_vector(BUS_ADDR_WIDTH-1 downto 0);
      resp_mem_data : std_logic_vector(31 downto 0);
  end record;

  signal r, rin : RegistersType;

begin

  comb : process(i_nrst, i_req_mem_fire, i_resp_mem_valid, i_resp_mem_addr,
                 i_resp_mem_data, i_f_predic_miss, i_e_npc, i_ra, r)
    variable v : RegistersType;
    variable wb_tmp : std_logic_vector(31 downto 0);
    variable wb_npc : std_logic_vector(BUS_ADDR_WIDTH-1 downto 0);
    variable wb_jal_off : std_logic_vector(BUS_ADDR_WIDTH-1 downto 0);
    variable v_predict : std_logic;
  begin

    v := r;

    if i_resp_mem_valid = '1' then
        v.resp_mem_addr := i_resp_mem_addr;
        v.resp_mem_data := i_resp_mem_data;
    end if;

    wb_tmp := r.resp_mem_data;
    wb_npc := r.npc;

    wb_jal_off(BUS_ADDR_WIDTH-1 downto 20) := (others => wb_tmp(31));
    wb_jal_off(19 downto 12) := wb_tmp(19 downto 12);
    wb_jal_off(11) := wb_tmp(20);
    wb_jal_off(10 downto 1) := wb_tmp(30 downto 21);
    wb_jal_off(0) := '0';

    v_predict := '0';
    if i_f_predic_miss = '1' then
        wb_npc := i_e_npc;
    elsif wb_tmp = X"00008067" then
        -- ret pseudo-instruction: Dhry score 34816 -> 35136
        wb_npc := i_ra(BUS_ADDR_WIDTH-1 downto 0);
        v_predict := '1';
    --!elsif wb_tmp(6 downto 0) = "1101111" then
    --!    -- jal instruction: Dhry score 35136 -> 36992
    --!    wb_npc := i_resp_mem_addr + wb_jal_off;
    else
        wb_npc := r.npc + 2;
        v_predict := '1';
    end if;


    if i_req_mem_fire = '1' then
        v.npc := wb_npc;
    end if;

    if i_nrst = '0' then
        v.npc := RESET_VECTOR - 2;
        v.resp_mem_addr := RESET_VECTOR;
        v.resp_mem_data := (others => '0');
    end if;

    o_npc_predict <= wb_npc;
    o_predict <= v_predict;
    
    rin <= v;
  end process;

  -- registers:
  regs : process(i_clk)
  begin 
     if rising_edge(i_clk) then 
        r <= rin;
     end if; 
  end process;

end;
