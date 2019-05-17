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

#include <api_core.h>
#include "dsu.h"
#include "dsumap.h"

namespace debugger {

DSU::DSU(const char *name)  : RegMemBankGeneric(name),
    softreset_(static_cast<IService *>(this), "soft_reset",
                DSUREG(ulocal.v.soft_reset)),
    missaccesscnt_(static_cast<IService *>(this), "miss_access_cnt",
                DSUREG(ulocal.v.miss_access_cnt)),
    missaccess_(static_cast<IService *>(this), "miss_access_addr",
                DSUREG(ulocal.v.miss_access_addr)) {
    registerInterface(static_cast<IMemoryOperation *>(this));
    registerInterface(static_cast<IDbgNbResponse *>(this));
    registerInterface(static_cast<IDsuGeneric *>(this));
    registerAttribute("CPU", &cpu_);
    registerAttribute("Bus", &bus_);
    registerAttribute("IrqControl", &irqctrl_);

    memset(&info_, 0, sizeof(info_));
}

DSU::~DSU() {
}

void DSU::postinitService() {
    RegMemBankGeneric::postinitService();

    icpu_ = static_cast<ICpuGeneric *>(
        RISCV_get_service_iface(cpu_.to_string(), IFACE_CPU_GENERIC));
    if (!icpu_) {
        RISCV_error("Can't find ICpuGeneric interface %s", cpu_.to_string());
    }
    icpurst_ = static_cast<IResetListener *>(
        RISCV_get_service_iface(cpu_.to_string(), IFACE_RESET_LISTENER));
    if (!icpurst_) {
        RISCV_error("Can't find IResetListener interface %s",
                    cpu_.to_string());
    }
    ibus_ = static_cast<IMemoryOperation *>(
        RISCV_get_service_iface(bus_.to_string(), IFACE_MEMORY_OPERATION));
    if (!ibus_) {
        RISCV_error("Can't find IBus interface %s", bus_.to_string());
    }
    iirq_ = static_cast<IWire *>(
        RISCV_get_service_port_iface(irqctrl_[0u].to_string(),
                                     irqctrl_[1].to_string(),
                                     IFACE_WIRE));
    if (!iirq_) {
        RISCV_error("Can't find IWire interface %s", irqctrl_[0u].to_string());
    }
}

/*ETransStatus DSU::b_transport(Axi4TransactionType *trans) {
    uint64_t mask = (length_.to_uint64() - 1);
    uint64_t off64 = (trans->addr - getBaseAddress()) & mask;
    if (!icpu_) {
        trans->response = MemResp_Error;
        return TRANS_ERROR;
    }
    uint64_t region = (off64 >> 15) & 0x3;

    if (region < 3) {
        RISCV_error("b_transport() to debug port NOT SUPPORTED", NULL);
        trans->response = MemResp_Error;
        return TRANS_ERROR;
    }

    if (trans->action == MemAction_Read) {
        readLocal(off64 & 0x7fff, trans);
    } else {
        writeLocal(off64 & 0x7fff, trans);
    }

    trans->response = MemResp_Valid;
    // @todo Memory mapped registers not related to debug port
    return TRANS_OK;
}*/

ETransStatus DSU::nb_transport(Axi4TransactionType *trans,
                               IAxi4NbResponse *cb) {
    uint64_t mask = (length_.to_uint64() - 1);
    uint64_t off64 = (trans->addr - getBaseAddress()) & mask;
    if (!icpu_) {
        trans->response = MemResp_Error;
        cb->nb_response(trans);
        return TRANS_ERROR;
    }

    nb_trans_.p_axi_trans = trans;
    nb_trans_.iaxi_cb = cb;

    nb_trans_.dbg_trans.write = 0;
    nb_trans_.dbg_trans.bytes = trans->xsize;
    if (trans->action == MemAction_Write) {
        nb_trans_.dbg_trans.write = 1;
        nb_trans_.dbg_trans.wdata = trans->wpayload.b64[0];
    }

    ETransStatus ret = TRANS_OK;
    nb_trans_.dbg_trans.addr = off64 & 0x7FFF;
    nb_trans_.dbg_trans.region = (off64 >> 15) & 0x3;
    if (nb_trans_.dbg_trans.region == 3) {
        ret = b_transport(trans);
        cb->nb_response(trans);
    } else {
        icpu_->nb_transport_debug_port(&nb_trans_.dbg_trans, this);
    }
    return ret;
}

void DSU::nb_response_debug_port(DebugPortTransactionType *trans) {
    nb_trans_.p_axi_trans->response = MemResp_Valid;
    nb_trans_.p_axi_trans->rpayload.b64[0] = trans->rdata;
    nb_trans_.iaxi_cb->nb_response(nb_trans_.p_axi_trans);
}

/*
void DSU::readLocal(uint64_t off, Axi4TransactionType *trans) {
    switch (off >> 3) {
    case 0:
        trans->rpayload.b64[0] = soft_reset_;
        break;
    case 2:
        trans->rpayload.b64[0] = missaccess_.getValue().val;
        break;
    case 8:
        trans->rpayload.b64[0] = info_[0].w_cnt;
        break;
    case 9:
        trans->rpayload.b64[0] = info_[0].r_cnt;
        break;
    case 12:
        trans->rpayload.b64[0] = info_[2].w_cnt;
        break;
    case 13:
        trans->rpayload.b64[0] = info_[2].r_cnt;
        break;
    default:
        trans->rpayload.b64[0] = 0;
    }
    if (trans->xsize == 4 && (off & 0x4) == 0x4) {
        trans->rpayload.b64[0] >>= 32;
    }
}

void DSU::writeLocal(uint64_t off, Axi4TransactionType *trans) {
    if (trans->xsize == 4) {
        if ((off & 0x4) == 0) {
            wdata64_ = trans->wpayload.b32[0];
            return;
        } else {
            wdata64_ |= static_cast<uint64_t>(trans->wpayload.b32[0]) << 32;
        }
    } else {
        wdata64_ = trans->wpayload.b64[0];
    }
    switch (off >> 3) {
    case 0:     // soft reset
        if (wdata64_ & 0x1) {
            icpurst_->reset(true);
        } else {
            icpurst_->reset(false);
        }
        soft_reset_ = wdata64_;
        break;
    default:;
    }
}
*/
void DSU::incrementRdAccess(int mst_id) {
    info_[mst_id].r_cnt++;
}

void DSU::incrementWrAccess(int mst_id) {
    info_[mst_id].w_cnt++;
}

void DSU::raiseMissaccess(uint64_t adr) {
    if (!iirq_) {
        return;
    }
    iirq_->raiseLine();
}

void DSU::softReset(bool val) {
    if (!icpurst_) {
        return;
    }
    icpurst_->reset(val);
}

uint64_t SOFT_RESET_TYPE::aboutToWrite(uint64_t new_val) {
    new_val &= 0x1;
    DSU *p = static_cast<DSU *>(parent_);
    p->softReset(new_val ? true: false);
    return new_val;
}

ETransStatus MISS_ACCESS_TYPE::b_transport(Axi4TransactionType *trans) {
    DSU *p = static_cast<DSU *>(parent_);
    value_.val = trans->addr;
    if (trans->action == MemAction_Read) {
        trans->rpayload.b64[0] = value_.val;
    }
    p->raiseMissaccess(trans->addr);
    return TRANS_OK;
}

}  // namespace debugger

