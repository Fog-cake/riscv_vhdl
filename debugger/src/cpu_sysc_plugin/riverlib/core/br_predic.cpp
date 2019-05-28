/*
 *  Copyright 2018 Sergey Khabarov, sergeykhbr@gmail.com
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

#include "br_predic.h"

namespace debugger {

BranchPredictor::BranchPredictor(sc_module_name name_) : sc_module(name_) {
    SC_METHOD(comb);
    sensitive << i_nrst;
    sensitive << i_req_mem_fire;
    sensitive << i_resp_mem_valid;
    sensitive << i_resp_mem_addr;
    sensitive << i_resp_mem_data;
    sensitive << i_f_predic_miss;
    sensitive << i_e_npc;
    sensitive << i_ra;
    sensitive << r.npc;
    sensitive << r.resp_mem_data;
    sensitive << r.resp_mem_addr;

    SC_METHOD(registers);
    sensitive << i_clk.pos();
};

void BranchPredictor::generateVCD(sc_trace_file *i_vcd, sc_trace_file *o_vcd) {
    if (o_vcd) {
        sc_trace(o_vcd, i_req_mem_fire, "/top/proc0/bp0/i_req_mem_fire");
        sc_trace(o_vcd, i_resp_mem_valid, "/top/proc0/bp0/i_resp_mem_valid");
        sc_trace(o_vcd, i_resp_mem_addr, "/top/proc0/bp0/i_resp_mem_addr");
        sc_trace(o_vcd, i_resp_mem_data, "/top/proc0/bp0/i_resp_mem_data");
        sc_trace(o_vcd, i_f_predic_miss, "/top/proc0/bp0/i_f_predic_miss");
        sc_trace(o_vcd, i_e_npc, "/top/proc0/bp0/i_e_npc");
        sc_trace(o_vcd, i_ra, "/top/proc0/bp0/i_ra");

        sc_trace(o_vcd, o_npc_predict, "/top/proc0/bp0/o_npc_predict");
        sc_trace(o_vcd, r.npc, "/top/proc0/bp0/r_npc");
        sc_trace(o_vcd, r.resp_mem_data, "/top/proc0/bp0/r_resp_mem_data");
        sc_trace(o_vcd, r.resp_mem_addr, "/top/proc0/bp0/r_resp_mem_addr");
        sc_trace(o_vcd, wb_npc, "/top/proc0/bp0/wb_npc");
    }
}

void BranchPredictor::comb() {
    v = r;
    sc_uint<BUS_ADDR_WIDTH> wb_jal_off;
    sc_uint<32> wb_tmp;
    bool v_predict;

    if (i_resp_mem_valid.read()) {
        v.resp_mem_addr = i_resp_mem_addr.read();
        v.resp_mem_data = i_resp_mem_data.read();
    }
    wb_tmp = r.resp_mem_data.read();
    wb_npc = r.npc.read();

    if (wb_tmp[31]) {
        wb_jal_off(BUS_ADDR_WIDTH-1, 20) = ~0;
    } else {
        wb_jal_off(BUS_ADDR_WIDTH-1, 20) = 0;
    }
    wb_jal_off(19, 12) = wb_tmp(19, 12);
    wb_jal_off[11] = wb_tmp[20];
    wb_jal_off(10, 1) = wb_tmp(30, 21);
    wb_jal_off[0] = 0;

    v_predict = 0;
    if (i_f_predic_miss.read()) {
        wb_npc = i_e_npc.read();
    } else if (wb_tmp == 0x00008067) {
        // ret32 pseudo-instruction:
        wb_npc = i_ra.read()(BUS_ADDR_WIDTH-1, 0);
        v_predict = 1;
    //} else if (wb_tmp(6, 0) == 0x6f) {
        // jal instruction: Dhry score 35136 -> 36992
        //wb_npc = r.resp_mem_addr.read() + wb_jal_off;
    } else {
        wb_npc = r.npc.read() + 2;
        v_predict = 1;
    }
    
    if (i_req_mem_fire.read()) {
        v.npc = wb_npc;
    }

    if (!i_nrst.read()) {
        v.npc = RESET_VECTOR - 2;
        v.resp_mem_addr = RESET_VECTOR;
        v.resp_mem_data = 0;
    }

    o_npc_predict = wb_npc;
    o_predict = v_predict;
}

void BranchPredictor::registers() {
    r = v;
}

}  // namespace debugger

