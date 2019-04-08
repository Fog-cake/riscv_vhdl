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

#include "edcl_types.h"
#include "edcl.h"

namespace debugger {

EdclService::EdclService(const char *name) : IService(name) {
    registerInterface(static_cast<ITap *>(this));
    registerAttribute("Transport", &transport_);
    registerAttribute("seq_cnt", &seq_cnt_);
    seq_cnt_.make_uint64(0);
    itransport_ = 0;

    dbgRdTRansactionCnt_ = 0;
}

void EdclService::postinitService() {
    IService *iserv = 
        static_cast<IService *>(RISCV_get_service(transport_.to_string()));
    if (!iserv) {
        RISCV_error("Transport service '%'s not found", 
                    transport_.to_string());
    }
    itransport_ = static_cast<ILink *>(iserv->getInterface(IFACE_LINK));
    if (!itransport_) {
        RISCV_error("UDP interface '%s' not found", 
                     transport_.to_string());
    }
}

int EdclService::read(uint64_t addr, int bytes, uint8_t *obuf) {
    int txoff, rxoff;
    UdpEdclCommonType req = {0};
    UdpEdclCommonType rsp;

    if (!itransport_) {
        RISCV_error("UDP transport not defined, addr=%x", addr);
        return TAP_ERROR;
    }

    int rd_bytes = 0;
    while (rd_bytes < bytes && rd_bytes != TAP_ERROR) {
        req.control.request.seqidx = 
                    static_cast<uint32_t>(seq_cnt_.to_uint64());
        req.control.request.write = 0;
        req.address = static_cast<uint32_t>(addr + rd_bytes);
        if ((bytes - rd_bytes) > EDCL_PAYLOAD_MAX_BYTES) {
            req.control.request.len = 
                    static_cast<uint32_t>(EDCL_PAYLOAD_MAX_BYTES);
        } else {
            req.control.request.len = static_cast<uint32_t>(bytes - rd_bytes);
        }

        txoff = write16(tx_buf_, 0, req.offset);
        txoff = write32(tx_buf_, txoff, req.control.word);
        txoff = write32(tx_buf_, txoff, req.address);

        txoff = itransport_->sendData(tx_buf_, txoff);
        if (txoff == -1) {
            RISCV_error("Data sending error", NULL);
            rd_bytes = TAP_ERROR;
            break;
        }

        dbgRdTRansactionCnt_++;
        rxoff = itransport_->readData(rx_buf_, sizeof(rx_buf_));
        if (rxoff == -1) {
            RISCV_error("Data receiving error", NULL);
            rd_bytes = TAP_ERROR;
            break;
        } 
        if (rxoff == 0) {
            RISCV_error("No response. Break read transaction[%d] at %08x",
                        dbgRdTRansactionCnt_, req.address);
            rd_bytes = TAP_ERROR;
            break;
        }

        rsp.control.word = read32(&rx_buf_[2]);

        const char *NAK[2] = {"ACK", "NAK"};
        RISCV_debug("EDCL read: %s[%d], len = %d", 
                    NAK[rsp.control.response.nak],
                    rsp.control.response.seqidx,
                    rsp.control.response.len);

        // Retry with new sequence counter.
        if (rsp.control.response.nak) {
            RISCV_info("Sequence counter detected %d. Re-sending transaction.",
                         rsp.control.response.seqidx);
            seq_cnt_.make_uint64(rsp.control.response.seqidx);
            continue;
        }

        // Try to receive next packet:
        if (rsp.control.response.seqidx != seq_cnt_.to_uint32()) {
            RISCV_error("Wrong ID received %d != %d. Try again.",
                        seq_cnt_.to_uint32(), rsp.control.response.seqidx);

            rxoff = itransport_->readData(rx_buf_, sizeof(rx_buf_));
            if (rxoff <= 0) {
                rd_bytes = TAP_ERROR;
                break;
            }
            rsp.control.word = read32(&rx_buf_[2]);
            if (rsp.control.response.seqidx != seq_cnt_.to_uint32()) {
                rd_bytes = TAP_ERROR;
                break;
            }
        }

        memcpy(&obuf[rd_bytes], &rx_buf_[10], rsp.control.response.len);
        rd_bytes += rsp.control.response.len;
        seq_cnt_.make_uint64((seq_cnt_.to_uint64() + 1) & 0x3FFF);
    }
    return rd_bytes;
}

int EdclService::write(uint64_t addr, int bytes, uint8_t *ibuf) {
    int off;
    UdpEdclCommonType req = {0};
    UdpEdclCommonType rsp;

    if (!itransport_) {
        RISCV_error("UDP transport not defined, addr=%x", addr);
        return TAP_ERROR;
    }

    int wr_bytes = 0;
    while (wr_bytes < bytes && wr_bytes != -1) {
        req.control.request.seqidx = 
                    static_cast<uint32_t>(seq_cnt_.to_uint64());
        req.control.request.write = 1;
        req.address = static_cast<uint32_t>(addr + wr_bytes);
        if ((bytes - wr_bytes) > EDCL_PAYLOAD_MAX_BYTES) {
            req.control.request.len = 
                    static_cast<uint32_t>(EDCL_PAYLOAD_MAX_BYTES);
        } else {
            req.control.request.len = static_cast<uint32_t>(bytes - wr_bytes);
        }

        off = write16(tx_buf_, 0, req.offset);
        off = write32(tx_buf_, off, req.control.word);
        off = write32(tx_buf_, off, req.address);
        memcpy(&tx_buf_[off], &ibuf[wr_bytes], req.control.request.len);


        off = itransport_->sendData(tx_buf_, off + req.control.request.len);
        if (off == -1) {
            RISCV_error("Data sending error", NULL);
            wr_bytes = -1;
            break;
        }

        off = itransport_->readData(rx_buf_, sizeof(rx_buf_));
        if (off == -1) {
            RISCV_error("Data receiving error", NULL);
            wr_bytes = -1;
            break;
        } 
        if (off == 0) {
            RISCV_error("No response. Break write transaction.", NULL);
            wr_bytes = -1;
            break;
        }

        rsp.control.word = read32(&rx_buf_[2]);

        // Warning:
        //   response length = 0;
        const char *NAK[2] = {"ACK", "NAK"};
        RISCV_debug("EDCL write: %s[%d], len = %d", 
                    NAK[rsp.control.response.nak],
                    rsp.control.response.seqidx,
                    req.control.request.len);

        // Retry with new sequence counter.
        if (rsp.control.response.nak) {
            RISCV_info("Sequence counter detected %d. Re-sending transaction.",
                         rsp.control.response.seqidx);
            seq_cnt_.make_uint64(rsp.control.response.seqidx);
            continue;
        }

        wr_bytes += req.control.request.len;
        seq_cnt_.make_uint64(seq_cnt_.to_uint64() + 1);
    }
    return wr_bytes;
}

int EdclService::write16(uint8_t *buf, int off, uint16_t v) {
    buf[off++] = (uint8_t)((v >> 8) & 0xFF);
    buf[off++] = (uint8_t)(v & 0xFF);
    return off;
}

int EdclService::write32(uint8_t *buf, int off, uint32_t v) {
    buf[off++] = (uint8_t)((v >> 24) & 0xFF);
    buf[off++] = (uint8_t)((v >> 16) & 0xFF);
    buf[off++] = (uint8_t)((v >> 8) & 0xFF);
    buf[off++] = (uint8_t)(v & 0xFF);
    return off;
}

uint32_t EdclService::read32(uint8_t *buf) {
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | (buf[3] << 0);
}

}  // namespace debugger
