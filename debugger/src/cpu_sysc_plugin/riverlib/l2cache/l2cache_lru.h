/*
 *  Copyright 2019 Sergey Khabarov, sergeykhbr@gmail.com
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifndef __DEBUGGER_RIVERLIB_L2CACHE_L2CACHE_LRU_H__
#define __DEBUGGER_RIVERLIB_L2CACHE_L2CACHE_LRU_H__

#include <systemc.h>
#include "../river_cfg.h"
#include "../cache/tagmemnway.h"

namespace debugger {

SC_MODULE(L2CacheLru) {
    sc_in<bool> i_clk;
    sc_in<bool> i_nrst;
    sc_in<bool> i_req_valid;
    sc_in<sc_uint<L2_REQ_TYPE_BITS>> i_req_type;
    sc_in<sc_uint<3>> i_req_size;
    sc_in<sc_uint<3>> i_req_prot;
    sc_in<sc_uint<CFG_CPU_ADDR_BITS>> i_req_addr;
    sc_in<sc_biguint<L1CACHE_LINE_BITS>> i_req_wdata;
    sc_in<sc_uint<L1CACHE_BYTES_PER_LINE>> i_req_wstrb;
    sc_out<bool> o_req_ready;
    sc_out<bool> o_resp_valid;
    sc_out<sc_biguint<L1CACHE_LINE_BITS>> o_resp_rdata;
    sc_out<sc_uint<2>> o_resp_status;
    // Memory interface:
    sc_in<bool> i_req_mem_ready;
    sc_out<bool> o_req_mem_valid;
    sc_out<sc_uint<REQ_MEM_TYPE_BITS>> o_req_mem_type;
    sc_out<sc_uint<3>> o_req_mem_size;
    sc_out<sc_uint<3>> o_req_mem_prot;
    sc_out<sc_uint<CFG_CPU_ADDR_BITS>> o_req_mem_addr;
    sc_out<sc_uint<L2CACHE_BYTES_PER_LINE>> o_req_mem_strob;
    sc_out<sc_biguint<L2CACHE_LINE_BITS>> o_req_mem_data;
    sc_in<bool> i_mem_data_valid;
    sc_in<sc_biguint<L2CACHE_LINE_BITS>> i_mem_data;
    sc_in<bool> i_mem_data_ack;
    sc_in<bool> i_mem_load_fault;
    sc_in<bool> i_mem_store_fault;
    // Flush interface
    sc_in<sc_uint<CFG_CPU_ADDR_BITS>> i_flush_address;
    sc_in<bool> i_flush_valid;
    sc_out<bool> o_flush_end;

    void comb();
    void registers();

    SC_HAS_PROCESS(L2CacheLru);

    L2CacheLru(sc_module_name name_, bool async_reset);
    virtual ~L2CacheLru();

    void generateVCD(sc_trace_file *i_vcd, sc_trace_file *o_vcd);

 private:

    enum EState {
        State_Idle,
        State_CheckHit,
        State_TranslateAddress,
        State_WaitGrant,
        State_WaitResp,
        State_CheckResp,
        State_SetupReadAdr,
        State_WriteBus,
        State_FlushAddr,
        State_FlushCheck,
        State_Reset,
        State_ResetWrite,
    };

    sc_signal<bool> line_direct_access_i;
    sc_signal<bool> line_invalidate_i;
    sc_signal<bool> line_re_i;
    sc_signal<bool> line_we_i;
    sc_signal<sc_uint<CFG_CPU_ADDR_BITS>> line_addr_i;
    sc_signal<sc_biguint<L2CACHE_LINE_BITS>> line_wdata_i;
    sc_signal<sc_uint<L2CACHE_BYTES_PER_LINE>> line_wstrb_i;
    sc_signal<sc_uint<L2TAG_FL_TOTAL>> line_wflags_i;
    sc_signal<sc_uint<CFG_CPU_ADDR_BITS>> line_raddr_o;
    sc_signal<sc_biguint<L2CACHE_LINE_BITS>> line_rdata_o;
    sc_signal<sc_uint<L2TAG_FL_TOTAL>> line_rflags_o;
    sc_signal<bool> line_hit_o;
    // Snoop signals:
    sc_signal<sc_uint<CFG_CPU_ADDR_BITS>> line_snoop_addr_i;
    sc_signal<bool> line_snoop_ready_o;
    sc_signal<sc_uint<L2TAG_FL_TOTAL>> line_snoop_flags_o;

    struct RegistersType {
        sc_signal<sc_uint<L2_REQ_TYPE_BITS>> req_type;
        sc_signal<sc_uint<3>> req_size;
        sc_signal<sc_uint<3>> req_prot;
        sc_signal<sc_uint<CFG_CPU_ADDR_BITS>> req_addr;
        sc_signal<sc_biguint<L1CACHE_LINE_BITS>> req_wdata;
        sc_signal<sc_uint<L1CACHE_BYTES_PER_LINE>> req_wstrb;
        sc_signal<sc_uint<4>> state;
        sc_signal<bool> req_mem_valid;
        sc_signal<sc_uint<REQ_MEM_TYPE_BITS>> req_mem_type;
        sc_signal<sc_uint<CFG_CPU_ADDR_BITS>> mem_addr;
        sc_signal<sc_uint<2>> rb_resp;     // r_resp, b_resp
        sc_signal<bool> write_first;
        sc_signal<bool> write_flush;
        sc_signal<sc_uint<L2CACHE_BYTES_PER_LINE>> mem_wstrb;
        sc_signal<bool> req_flush;
        sc_signal<bool> req_flush_all;
        sc_signal<sc_uint<CFG_CPU_ADDR_BITS>> req_flush_addr;
        sc_signal<sc_uint<CFG_L2_LOG2_LINES_PER_WAY + CFG_L2_LOG2_NWAYS>> req_flush_cnt;
        sc_signal<sc_uint<CFG_L2_LOG2_LINES_PER_WAY + CFG_L2_LOG2_NWAYS>> flush_cnt;
        sc_signal<sc_biguint<L2CACHE_LINE_BITS>> cache_line_i;
        sc_signal<sc_biguint<L2CACHE_LINE_BITS>> cache_line_o;
    } v, r;

    void R_RESET(RegistersType &iv) {
        iv.req_type = 0;
        iv.req_size = 0;
        iv.req_prot = 0;
        iv.req_addr = 0;
        iv.req_wdata = 0;
        iv.req_wstrb = 0;
        iv.state = State_Reset;
        iv.req_mem_valid = 0;
        iv.req_mem_type = 0;
        iv.mem_addr = 0;
        iv.rb_resp = 0;
        iv.write_first = 0;
        iv.write_flush = 0;
        iv.mem_wstrb = 0;
        iv.req_flush = 0;           // init flush request
        iv.req_flush_all = 0;
        iv.req_flush_addr = 0;   // [0]=1 flush all
        iv.req_flush_cnt = 0;
        iv.flush_cnt = ~0ul;
        iv.cache_line_i = 0;
        iv.cache_line_o = 0;
    }

    TagMemNWay<CFG_CPU_ADDR_BITS,
               CFG_L2_LOG2_NWAYS,
               CFG_L2_LOG2_LINES_PER_WAY,
               CFG_L2_LOG2_BYTES_PER_LINE,
               L2TAG_FL_TOTAL,
               0> *mem;

    bool async_reset_;
};

}  // namespace debugger

#endif  // __DEBUGGER_RIVERLIB_L2CACHE_L2CACHE_LRU_H__
