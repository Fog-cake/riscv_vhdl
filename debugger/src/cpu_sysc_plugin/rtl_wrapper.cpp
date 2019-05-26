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

#include "api_core.h"
#include "rtl_wrapper.h"
#include "iservice.h"
#include <riscv-isa.h>
#include "coreservices/iserial.h"

//#define SIMULATE_WAIT_STATES

namespace debugger {

RtlWrapper::RtlWrapper(IFace *parent, sc_module_name name) : sc_module(name),
    o_clk("clk", 10, SC_NS) {
    iparent_ = parent;
    generate_ref_ = false;
    clockCycles_ = 1000000; // 1 MHz when default resolution = 1 ps

    SC_METHOD(registers);
    sensitive << o_clk.posedge_event();

    SC_METHOD(comb);
    sensitive << i_req_mem_valid;
    sensitive << i_req_mem_addr;
    sensitive << r.nrst;
    sensitive << r.resp_mem_data_valid;
    sensitive << r.resp_mem_data;
    sensitive << r.resp_mem_load_fault;
    sensitive << r.resp_mem_store_fault;
    sensitive << r.interrupt;
    sensitive << r.wait_state_cnt;

    SC_METHOD(clk_negedge_proc);
    sensitive << o_clk.negedge_event();

    w_nrst = 1;//0;
    v.nrst = 1;//0;
    v.interrupt = false;
    w_interrupt = 0;
    v.resp_mem_data = 0;
    v.resp_mem_data_valid = false;
    v.resp_mem_load_fault = false;
    v.resp_mem_store_fault = false;
    RISCV_event_create(&dport_.valid, "dport_valid");
    dport_.trans_idx_up = 0;
    dport_.trans_idx_down = 0;
}

RtlWrapper::~RtlWrapper() {
    RISCV_event_close(&dport_.valid);
}

void RtlWrapper::generateVCD(sc_trace_file *i_vcd, sc_trace_file *o_vcd) {
    if (i_vcd) {
    }
    if (o_vcd) {
        sc_trace(o_vcd, w_nrst, "wrapper0/w_nrst");
        sc_trace(o_vcd, r.nrst, "wrapper0/r_nrst");
    }
}

void RtlWrapper::clk_gen() {
    // todo: instead sc_clock
}

void RtlWrapper::comb() {

    o_nrst = r.nrst.read()[1].to_bool();
    o_resp_mem_data_valid = r.resp_mem_data_valid;
    o_resp_mem_data = r.resp_mem_data;
    o_resp_mem_load_fault = r.resp_mem_load_fault;
    o_resp_mem_store_fault = r.resp_mem_store_fault;
    o_interrupt = r.interrupt;
#ifdef SIMULATE_WAIT_STATES
    if (r.wait_state_cnt.read() == 1) {
        o_req_mem_ready = 1;
    } else {
        o_req_mem_ready = 0;
    }
#else
    o_req_mem_ready = i_req_mem_valid;
#endif

    o_dport_valid = r.dport_valid;
    o_dport_write = r.dport_write;
    o_dport_region = r.dport_region;
    o_dport_addr = r.dport_addr;
    o_dport_wdata = r.dport_wdata;

    if (!r.nrst.read()[1]) {
    }
}

void RtlWrapper::registers() {
    r = v;
}

void RtlWrapper::clk_negedge_proc() {
    /** Simulation events queue */
    IFace *cb;

    step_queue_.initProc();
    step_queue_.pushPreQueued();
    uint64_t step_cnt = i_time.read();
    while ((cb = step_queue_.getNext(step_cnt)) != 0) {
        static_cast<IClockListener *>(cb)->stepCallback(step_cnt);
    }

    /** */
    v.interrupt = w_interrupt;
    v.nrst = (r.nrst.read() << 1) | w_nrst;
    v.wait_state_cnt = r.wait_state_cnt.read() + 1;


    v.resp_mem_data = 0;
    v.resp_mem_data_valid = false;
    v.resp_mem_load_fault = false;
    v.resp_mem_store_fault = false;
    bool w_req_fire = 0;
#ifdef SIMULATE_WAIT_STATES
    if (r.wait_state_cnt.read() == 1)
#endif
    {
        w_req_fire = i_req_mem_valid.read();
    }
    if (w_req_fire) {
        Axi4TransactionType trans;
        trans.source_idx = 0;//CFG_NASTI_MASTER_CACHED;
        trans.addr = i_req_mem_addr.read();
        if (i_req_mem_write.read()) {
            uint8_t strob = i_req_mem_strob.read();
            uint64_t offset = mask2offset(strob);
            trans.addr += offset;
            trans.action = MemAction_Write;
            trans.xsize = mask2size(strob >> offset);
            trans.wstrb = (1 << trans.xsize) - 1;
            trans.wpayload.b64[0] = i_req_mem_data.read();
            ETransStatus r_resp = ibus_->b_transport(&trans);
            v.resp_mem_data = 0;
            if (r_resp == TRANS_ERROR) {
                v.resp_mem_load_fault = true;
            }
        } else {
            trans.action = MemAction_Read;
            trans.xsize = BUS_DATA_BYTES;
            ETransStatus b_resp = ibus_->b_transport(&trans);
            v.resp_mem_data = trans.rpayload.b64[0];
            if (b_resp == TRANS_ERROR) {
                v.resp_mem_store_fault = true;
            }
        }
        v.resp_mem_data_valid = true;
    }

    // Debug port handling:
    v.dport_valid = 0;
    if (RISCV_event_is_set(&dport_.valid)) {
        RISCV_event_clear(&dport_.valid);
        v.dport_valid = 1;
        v.dport_write = dport_.trans->write;
        v.dport_region = dport_.trans->region;
        v.dport_addr = dport_.trans->addr >> 3;
        v.dport_wdata = dport_.trans->wdata;
    }
    dport_.idx_missmatch = 0;
    if (i_dport_ready.read()) {
        dport_.trans->rdata = i_dport_rdata.read().to_uint64();
        dport_.trans_idx_down++;
        if (dport_.trans_idx_down != dport_.trans_idx_up) {
            dport_.idx_missmatch = 1;
            RISCV_error("error: sync. is lost: up=%d, down=%d",
                         dport_.trans_idx_up, dport_.trans_idx_down);
            dport_.trans_idx_down = dport_.trans_idx_up;
        }
        dport_.cb->nb_response_debug_port(dport_.trans);
    }
}

uint64_t RtlWrapper::mask2offset(uint8_t mask) {
    for (int i = 0; i < BUS_DATA_BYTES; i++) {
        if (mask & 0x1) {
            return static_cast<uint64_t>(i);
        }
        mask >>= 1;
    }
    return 0;
}

uint32_t RtlWrapper::mask2size(uint8_t mask) {
    uint32_t bytes = 0;
    for (int i = 0; i < BUS_DATA_BYTES; i++) {
        if (!(mask & 0x1)) {
            break;
        }
        bytes++;
        mask >>= 1;
    }
    return bytes;
}

void RtlWrapper::setClockHz(double hz) {
    sc_time dt = sc_get_time_resolution();
    clockCycles_ = static_cast<int>((1.0 / hz) / dt.to_seconds() + 0.5);
}
    
void RtlWrapper::registerStepCallback(IClockListener *cb, uint64_t t) {
    if (!w_nrst) {
        if (i_time.read() == t) {
            cb->stepCallback(t);
        }
    } else {
        step_queue_.put(t, cb);
    }
}

void RtlWrapper::raiseSignal(int idx) {
    switch (idx) {
    case SIGNAL_HardReset:
        w_nrst = 0;
        break;
    case INTERRUPT_MExternal:
        w_interrupt = true;
        break;
    default:;
    }
}

void RtlWrapper::lowerSignal(int idx) {
    switch (idx) {
    case SIGNAL_HardReset:
        w_nrst = 1;
        break;
    case INTERRUPT_MExternal:
        w_interrupt = false;
        break;
    default:;
    }
}

void RtlWrapper::nb_transport_debug_port(DebugPortTransactionType *trans,
                                         IDbgNbResponse *cb) {
    dport_.trans = trans;
    dport_.cb = cb;
    dport_.trans_idx_up++;
    RISCV_event_set(&dport_.valid);
}

}  // namespace debugger

