--!
--! Copyright 2019 Sergey Khabarov, sergeykhbr@gmail.com
--!
--! Licensed under the Apache License, Version 2.0 (the "License");
--! you may not use this file except in compliance with the License.
--! You may obtain a copy of the License at
--!
--!     http://www.apache.org/licenses/LICENSE-2.0
--!
--! Unless required by applicable law or agreed to in writing, software
--! distributed under the License is distributed on an "AS IS" BASIS,
--! WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
--! See the License for the specific language governing permissions and
--! limitations under the License.
--!

library ieee;
use ieee.std_logic_1164.all;
library commonlib;
use commonlib.types_common.all;
library ambalib;
use ambalib.types_amba4.all;

entity axictrl is
  generic (
    async_reset : boolean
  );
  port (
    i_clk    : in std_logic;
    i_nrst   : in std_logic;
    i_slvcfg : in  nasti_slave_cfg_vector;
    i_slvo   : in  nasti_slaves_out_vector;
    i_msto   : in  nasti_master_out_vector;
    o_slvi   : out nasti_slave_in_vector;
    o_msti   : out nasti_master_in_vector;
    o_bus_util_w : out std_logic_vector(CFG_NASTI_MASTER_TOTAL-1 downto 0);
    o_bus_util_r : out std_logic_vector(CFG_NASTI_MASTER_TOTAL-1 downto 0)
  );
end; 
 
architecture arch_axictrl of axictrl is

  constant MISS_ACCESS_SLAVE : nasti_slave_out_type := (
      '1', '1', '1', NASTI_RESP_DECERR,
      (others=>'0'), '0', '1', '1', NASTI_RESP_DECERR, (others=>'1'), 
      '1', (others=>'0'), '0');

  type nasti_master_out_vector_miss is array (0 to CFG_NASTI_MASTER_TOTAL) 
       of nasti_master_out_type;

  type nasti_master_in_vector_miss is array (0 to CFG_NASTI_MASTER_TOTAL) 
       of nasti_master_in_type;

  type nasti_slave_out_vector_miss is array (0 to CFG_NASTI_SLAVES_TOTAL) 
       of nasti_slave_out_type;

  type nasti_slave_in_vector_miss is array (0 to CFG_NASTI_SLAVES_TOTAL) 
       of nasti_slave_in_type;
       
  type reg_type is record
     r_midx : integer range 0 to CFG_NASTI_MASTER_TOTAL;
     r_sidx : integer range 0 to CFG_NASTI_SLAVES_TOTAL;
     w_midx : integer range 0 to CFG_NASTI_MASTER_TOTAL;
     w_sidx : integer range 0 to CFG_NASTI_SLAVES_TOTAL;
     b_midx : integer range 0 to CFG_NASTI_MASTER_TOTAL;
     b_sidx : integer range 0 to CFG_NASTI_SLAVES_TOTAL;
  end record;

  constant R_RESET : reg_type := (
      CFG_NASTI_MASTER_TOTAL, CFG_NASTI_SLAVES_TOTAL,
      CFG_NASTI_MASTER_TOTAL, CFG_NASTI_SLAVES_TOTAL,
      CFG_NASTI_MASTER_TOTAL, CFG_NASTI_SLAVES_TOTAL);

  signal rin, r : reg_type;

begin

  comblogic : process(i_nrst, i_slvcfg, i_msto, i_slvo, r)
    variable v : reg_type;
    variable ar_midx : integer range 0 to CFG_NASTI_MASTER_TOTAL;
    variable aw_midx : integer range 0 to CFG_NASTI_MASTER_TOTAL;
    variable w_midx_mux : integer range 0 to CFG_NASTI_MASTER_TOTAL;
    variable ar_sidx : integer range 0 to CFG_NASTI_SLAVES_TOTAL; -- +1 miss access
    variable aw_sidx : integer range 0 to CFG_NASTI_SLAVES_TOTAL; -- +1 miss access
    variable w_sidx_mux : integer range 0 to CFG_NASTI_SLAVES_TOTAL;
    variable vmsto : nasti_master_out_vector_miss;
    variable vmsti : nasti_master_in_vector_miss;
    variable vslvi : nasti_slave_in_vector_miss;
    variable vslvo : nasti_slave_out_vector_miss;
    variable aw_fire : std_logic;
    variable ar_fire : std_logic;
    variable w_fire : std_logic;
    variable w_fire_lite : std_logic;
    variable w_busy : std_logic;
    variable w_busy_lite : std_logic;
    variable r_fire : std_logic;
    variable r_busy : std_logic;
    variable b_fire : std_logic;
    variable b_busy : std_logic;
    -- Bus statistic signals
    variable wb_bus_util_w : std_logic_vector(CFG_NASTI_MASTER_TOTAL downto 0);
    variable wb_bus_util_r : std_logic_vector(CFG_NASTI_MASTER_TOTAL downto 0);

  begin

    v := r;

    for m in 0 to CFG_NASTI_MASTER_TOTAL-1 loop
       vmsto(m) := i_msto(m);
       vmsti(m) := nasti_master_in_none;
    end loop;
    vmsto(CFG_NASTI_MASTER_TOTAL) := nasti_master_out_none;
    vmsti(CFG_NASTI_MASTER_TOTAL) := nasti_master_in_none;

    for s in 0 to CFG_NASTI_SLAVES_TOTAL-1 loop
       vslvo(s) := i_slvo(s);
       vslvi(s) := nasti_slave_in_none;
    end loop;
    vslvo(CFG_NASTI_SLAVES_TOTAL) := MISS_ACCESS_SLAVE;
    vslvi(CFG_NASTI_SLAVES_TOTAL) := nasti_slave_in_none;

    ar_midx := CFG_NASTI_MASTER_TOTAL;
    aw_midx := CFG_NASTI_MASTER_TOTAL;
    ar_sidx := CFG_NASTI_SLAVES_TOTAL;
    aw_sidx := CFG_NASTI_SLAVES_TOTAL;

    -- select master bus:
    for m in 0 to CFG_NASTI_MASTER_TOTAL-1 loop
        if i_msto(m).ar_valid = '1' then
            ar_midx := m;
        end if;
        if i_msto(m).aw_valid = '1' then
            aw_midx := m;
        end if;
    end loop;

    -- select slave interface
    for s in 0 to CFG_NASTI_SLAVES_TOTAL-1 loop
        if i_slvcfg(s).xmask /= X"00000" and
           (vmsto(ar_midx).ar_bits.addr(CFG_NASTI_ADDR_BITS-1 downto 12) 
            and i_slvcfg(s).xmask) = i_slvcfg(s).xaddr then
            ar_sidx := s;
        end if;
        if i_slvcfg(s).xmask /= X"00000" and
           (vmsto(aw_midx).aw_bits.addr(CFG_NASTI_ADDR_BITS-1 downto 12)
            and i_slvcfg(s).xmask) = i_slvcfg(s).xaddr then
            aw_sidx := s;
        end if;
    end loop;

    -- Read Channel:
    ar_fire := vmsto(ar_midx).ar_valid and vslvo(ar_sidx).ar_ready;
    r_fire := vmsto(r.r_midx).r_ready and vslvo(r.r_sidx).r_valid and vslvo(r.r_sidx).r_last;
    -- Write channel:
    aw_fire := vmsto(aw_midx).aw_valid and vslvo(aw_sidx).aw_ready;
    w_fire_lite := vmsto(aw_midx).w_valid and vmsto(aw_midx).w_last and vslvo(aw_sidx).w_ready;
    w_fire := vmsto(r.w_midx).w_valid and vmsto(r.w_midx).w_last and vslvo(r.w_sidx).w_ready;
    -- Write confirm channel
    b_fire := vmsto(r.b_midx).b_ready and vslvo(r.b_sidx).b_valid;

    r_busy := '0';
    if r.r_sidx /= CFG_NASTI_SLAVES_TOTAL and r_fire = '0' then
        r_busy := '1';
    end if;

    w_busy := '0';
    if (r.w_sidx /= CFG_NASTI_SLAVES_TOTAL and w_fire = '0') 
       or (r.b_sidx /= CFG_NASTI_SLAVES_TOTAL and b_fire = '0') then
        w_busy := '1';
    end if;

    w_busy_lite := '0';
    if (r.w_sidx /= CFG_NASTI_SLAVES_TOTAL)
        or (r.b_sidx /= CFG_NASTI_SLAVES_TOTAL and b_fire = '0') then
        w_busy_lite := '1';
    end if;

    b_busy := '0';
    if (r.b_sidx /= CFG_NASTI_SLAVES_TOTAL and b_fire = '0')  then
        b_busy := '1';
    end if;

    if ar_fire = '1' and r_busy = '0' then
        v.r_sidx := ar_sidx;
        v.r_midx := ar_midx;
    elsif r_fire = '1' then
        v.r_sidx := CFG_NASTI_SLAVES_TOTAL;
        v.r_midx := CFG_NASTI_MASTER_TOTAL;
    end if;

    if aw_fire = '1' and w_fire_lite = '1' and w_busy_lite = '0' then
        v.w_sidx := CFG_NASTI_SLAVES_TOTAL;
        v.w_midx := CFG_NASTI_MASTER_TOTAL;
    elsif aw_fire = '1' and w_busy = '0' then
        v.w_sidx := aw_sidx;
        v.w_midx := aw_midx;
    elsif w_fire = '1' and b_busy = '0' then
        v.w_sidx := CFG_NASTI_SLAVES_TOTAL;
        v.w_midx := CFG_NASTI_MASTER_TOTAL;
    end if;

    w_sidx_mux := r.w_sidx;
    w_midx_mux := r.w_midx;
    if aw_fire = '1' and w_fire_lite = '1' and w_busy_lite = '0' then
        v.b_sidx := aw_sidx;
        v.b_midx := aw_midx;
        w_sidx_mux := aw_sidx;
        w_midx_mux := aw_midx;
    elsif w_fire = '1' and b_busy = '0' then
        v.b_sidx := r.w_sidx;
        v.b_midx := r.w_midx;
    elsif b_fire = '1' then
        v.b_sidx := CFG_NASTI_SLAVES_TOTAL;
        v.b_midx := CFG_NASTI_MASTER_TOTAL;
    end if;


    vmsti(ar_midx).ar_ready := vslvo(ar_sidx).ar_ready and not r_busy;
    vslvi(ar_sidx).ar_valid := vmsto(ar_midx).ar_valid and not r_busy;
    vslvi(ar_sidx).ar_bits  := vmsto(ar_midx).ar_bits;
    vslvi(ar_sidx).ar_id    := vmsto(ar_midx).ar_id;
    vslvi(ar_sidx).ar_user  := vmsto(ar_midx).ar_user;

    vmsti(r.r_midx).r_valid := vslvo(r.r_sidx).r_valid;
    vmsti(r.r_midx).r_resp  := vslvo(r.r_sidx).r_resp;
    vmsti(r.r_midx).r_data  := vslvo(r.r_sidx).r_data;
    vmsti(r.r_midx).r_last  := vslvo(r.r_sidx).r_last;
    vmsti(r.r_midx).r_id    := vslvo(r.r_sidx).r_id;
    vmsti(r.r_midx).r_user  := vslvo(r.r_sidx).r_user;
    vslvi(r.r_sidx).r_ready := vmsto(r.r_midx).r_ready;

    vmsti(aw_midx).aw_ready := vslvo(aw_sidx).aw_ready and not w_busy;
    vslvi(aw_sidx).aw_valid := vmsto(aw_midx).aw_valid and not w_busy;
    vslvi(aw_sidx).aw_bits  := vmsto(aw_midx).aw_bits;
    vslvi(aw_sidx).aw_id    := vmsto(aw_midx).aw_id;
    vslvi(aw_sidx).aw_user  := vmsto(aw_midx).aw_user;

    vmsti(w_midx_mux).w_ready := vslvo(w_sidx_mux).w_ready and not b_busy;
    vslvi(w_sidx_mux).w_valid := vmsto(w_midx_mux).w_valid and not b_busy;
    vslvi(w_sidx_mux).w_data := vmsto(w_midx_mux).w_data;
    vslvi(w_sidx_mux).w_last := vmsto(w_midx_mux).w_last;
    vslvi(w_sidx_mux).w_strb := vmsto(w_midx_mux).w_strb;
    vslvi(w_sidx_mux).w_user := vmsto(w_midx_mux).w_user;

    vmsti(r.b_midx).b_valid := vslvo(r.b_sidx).b_valid;
    vmsti(r.b_midx).b_resp := vslvo(r.b_sidx).b_resp;
    vmsti(r.b_midx).b_id := vslvo(r.b_sidx).b_id;
    vmsti(r.b_midx).b_user := vslvo(r.b_sidx).b_user;
    vslvi(r.b_sidx).b_ready := vmsto(r.b_midx).b_ready;

    -- Statistic
    wb_bus_util_w := (others => '0');
    wb_bus_util_w(r.w_midx) :=  '1';
    wb_bus_util_r := (others => '0');
    wb_bus_util_r(r.r_midx) := '1';


    if not async_reset and i_nrst = '0' then
        v := R_RESET;
    end if;
 
    rin <= v;

    for m in 0 to CFG_NASTI_MASTER_TOTAL-1 loop
       o_msti(m) <= vmsti(m);
    end loop;
    for s in 0 to CFG_NASTI_SLAVES_TOTAL-1 loop
       o_slvi(s) <= vslvi(s);
    end loop;
    o_bus_util_w <= wb_bus_util_w(CFG_NASTI_MASTER_TOTAL-1 downto 0);
    o_bus_util_r <= wb_bus_util_r(CFG_NASTI_MASTER_TOTAL-1 downto 0);
  end process;

  regs : process(i_clk, i_nrst)
  begin 
     if async_reset and i_nrst = '0' then
         r <= R_RESET;
     elsif rising_edge(i_clk) then 
         r <= rin;
     end if; 
  end process;
end;
