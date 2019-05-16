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
#include "uart.h"

namespace debugger {

static const uint32_t UART_STATUS_TX_FULL     = 0x00000001;
static const uint32_t UART_STATUS_TX_EMPTY    = 0x00000002;
static const uint32_t UART_STATUS_RX_FULL     = 0x00000010;
static const uint32_t UART_STATUS_RX_EMPTY    = 0x00000020;
static const uint32_t UART_STATUS_ERR_PARITY  = 0x00000100;
static const uint32_t UART_STATUS_ERR_STOPBIT = 0x00000200;
static const uint32_t UART_CONTROL_RX_IRQ_ENA = 0x00002000;
static const uint32_t UART_CONTROL_TX_IRQ_ENA = 0x00004000;
static const uint32_t UART_CONTROL_PARITY_ENA = 0x00008000;


UART::UART(const char *name)  : IService(name) {
    registerInterface(static_cast<IMemoryOperation *>(this));
    registerInterface(static_cast<ISerial *>(this));
    registerAttribute("IrqControl", &irqctrl_);
    registerAttribute("AutoTestEna", &autoTestEna_);
    registerAttribute("TestCases", &testCases_);

    listeners_.make_list(0);
    RISCV_mutex_init(&mutexListeners_);

    memset(&regs_, 0, sizeof(regs_));
    regs_.status = UART_STATUS_TX_EMPTY | UART_STATUS_RX_EMPTY;

    p_rx_wr_ = rxfifo_;
    p_rx_rd_ = rxfifo_;
    rx_total_ = 0;
    pautotest_ = NULL;
}

UART::~UART() {
    RISCV_mutex_destroy(&mutexListeners_);
    if (pautotest_) {
        delete pautotest_;
    }
}

void UART::postinitService() {
    iwire_ = static_cast<IWire *>(
        RISCV_get_service_port_iface(irqctrl_[0u].to_string(),
                                     irqctrl_[1].to_string(),
                                     IFACE_WIRE));
    if (!iwire_) {
        RISCV_error("Can't find IWire interface %s", irqctrl_[0u].to_string());
    }
    if (autoTestEna_.to_bool()) {
        pautotest_ = new AutoTest(static_cast<ISerial *>(this), &testCases_);
    }
}

int UART::writeData(const char *buf, int sz) {
    if (sz > (RX_FIFO_SIZE - rx_total_)) {
        sz = (RX_FIFO_SIZE - rx_total_);
    }
    for (int i = 0; i < sz; i++) {
        rx_total_++;
        *p_rx_wr_ = buf[i];
        if ((++p_rx_wr_) >= (rxfifo_ + RX_FIFO_SIZE)) {
            p_rx_wr_ = rxfifo_;
        }
    }

    if (regs_.status & UART_CONTROL_RX_IRQ_ENA) {
        iwire_->raiseLine();
    }
    return sz;
}

void UART::registerRawListener(IFace *listener) {
    AttributeType lstn(listener);
    RISCV_mutex_lock(&mutexListeners_);
    listeners_.add_to_list(&lstn);
    RISCV_mutex_unlock(&mutexListeners_);
}

void UART::unregisterRawListener(IFace *listener) {
    for (unsigned i = 0; i < listeners_.size(); i++) {
        IFace *iface = listeners_[i].to_iface();
        if (iface == listener) {
            RISCV_mutex_lock(&mutexListeners_);
            listeners_.remove_from_list(i);
            RISCV_mutex_unlock(&mutexListeners_);
            break;
        }
    }
}

void UART::getListOfPorts(AttributeType *list) {
    list->make_list(0);
}

int UART::openPort(const char *port, AttributeType settings) {
    return 0;
}

void UART::closePort() {
}


ETransStatus UART::b_transport(Axi4TransactionType *trans) {
    uint64_t mask = (length_.to_uint64() - 1);
    uint64_t off = ((trans->addr - getBaseAddress()) & mask) / 4;
    char wrdata;
    trans->response = MemResp_Valid;
    if (trans->action == MemAction_Write) {
        for (uint64_t i = 0; i < trans->xsize/4; i++) {
            if ((trans->wstrb & (0xf << 4*i)) == 0) {
                continue;
            }
            switch (off + i) {
            case 0:
                regs_.status = trans->wpayload.b32[i];
                RISCV_info("Set status = %08x", regs_.status);
                break;
            case 1:
                regs_.scaler = trans->wpayload.b32[i];
                RISCV_info("Set scaler = %d", regs_.scaler);
                break;
            case 4:
                wrdata = static_cast<char>(trans->wpayload.b32[i]);
                RISCV_info("Set data = %s", &regs_.data);
                RISCV_mutex_lock(&mutexListeners_);
                for (unsigned n = 0; n < listeners_.size(); n++) {
                    IRawListener *lstn = static_cast<IRawListener *>(
                                        listeners_[n].to_iface());

                    lstn->updateData(&wrdata, 1);
                }
                RISCV_mutex_unlock(&mutexListeners_);
                if (regs_.status & UART_CONTROL_TX_IRQ_ENA) {
                    iwire_->raiseLine();
                }
                break;
            default:;
            }
        }
    } else {
        for (uint64_t i = 0; i < trans->xsize/4; i++) {
            switch (off + i) {
            case 0:
                if (0) {
                    regs_.status &= ~UART_STATUS_TX_EMPTY;
                } else {
                    regs_.status |= UART_STATUS_TX_EMPTY;
                }
                if (rx_total_ == 0) {
                    regs_.status |= UART_STATUS_RX_EMPTY;
                } else {
                    regs_.status &= ~UART_STATUS_RX_EMPTY;
                }
                trans->rpayload.b32[i] = regs_.status;
                RISCV_info("Get status = %08x", regs_.status);
                break;
            case 1:
                trans->rpayload.b32[i] = regs_.scaler;
                RISCV_info("Get scaler = %d", regs_.scaler);
                break;
            case 4:
                if (rx_total_ == 0) {
                    trans->rpayload.b32[i] = 0;
                } else {
                    trans->rpayload.b32[i] = *p_rx_rd_;
                    rx_total_--;
                    if ((++p_rx_rd_) >= (rxfifo_ + RX_FIFO_SIZE)) {
                        p_rx_rd_ = rxfifo_;
                    }
                }
                RISCV_debug("Get data = %02x", (trans->rpayload.b32[i] & 0xFF));
                break;
            default:
                trans->rpayload.b32[i] = ~0;
            }
        }
    }
    return TRANS_OK;
}

/**
 * CPU validation purpose. Reaction on keyboard interrupt:
 */

UART::AutoTest::AutoTest(ISerial *parent, AttributeType *tests) {
    parent_ = parent;
    tests_ = *tests;
    testcnt_ = 0;
    IClock *iclk = static_cast<IClock *>(
            RISCV_get_service_iface("core0", IFACE_CLOCK));
    if (iclk && tests_.is_list() && tests_.size()) {
        const AttributeType &t0 = tests_[testcnt_];
        if (t0.size() == 2) {
            iclk->registerStepCallback(static_cast<IClockListener *>(this),
                                       t0[0u].to_uint64());
        }
    }
}

void UART::AutoTest::stepCallback(uint64_t t) {
    const AttributeType &tn = tests_[testcnt_];
    char msg[64];
    int msglen = 0;
    const char *pbuf = tn[1].to_string();
    for (unsigned i = 0; i < tn[1].size(); i++) {
        if (pbuf[i] == '\\' && pbuf[i+1] == 'r') {
            ++i;
            msg[msglen++] = '\r';
            msg[msglen] = '\0';
            continue;
        } else if (pbuf[i] == '\\' && pbuf[i+1] == 'n') {
            ++i;
            msg[msglen++] = '\n';
            msg[msglen] = '\0';
            continue;
        }
        msg[msglen++] = pbuf[i];
        msg[msglen] = '\0';
    }
    parent_->writeData(msg, msglen);

    if (++testcnt_ < tests_.size()) {
        const AttributeType &t0 = tests_[testcnt_];
        IClock *iclk = static_cast<IClock *>(
                RISCV_get_service_iface("core0", IFACE_CLOCK));
        iclk->registerStepCallback(static_cast<IClockListener *>(this),
                                   t0[0u].to_uint64());
    }
}

}  // namespace debugger

