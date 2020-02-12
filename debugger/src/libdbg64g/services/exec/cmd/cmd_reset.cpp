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

#include "cmd_reset.h"
#include "debug/dsumap.h"

namespace debugger {

CmdReset::CmdReset(ITap *tap) : ICommand ("reset", tap) {

    briefDescr_.make_string("Reset, Un-reset or Reboot target");
    detailedDescr_.make_string(
        "Description:\n"
        "    Reset, Un-reset or Reboot target.\n"
        "Warning:\n"
        "    When reset is HIGH CPU debug port is resetting also and cannot\n"
        "    response on debugger requests.\n"
        "Example:\n"
        "    reset\n"
        "    reset 1\n"
        "    reset 0\n");
}

int CmdReset::isValid(AttributeType *args) {
    if (!cmdName_.is_equal((*args)[0u].to_string())) {
        return CMD_INVALID;
    }
    if (args->size() <= 2) {
        return CMD_VALID;
    }
    return CMD_WRONG_ARGS;
}

void CmdReset::exec(AttributeType *args, AttributeType *res) {
    res->attr_free();
    res->make_nil();

    Reg64Type rst;
    uint64_t sw_rst_addr = DSUREGBASE(ulocal.v.soft_reset);

    if (args->size() == 2) {
        rst.val = (*args)[1].to_uint64();
        tap_->write(sw_rst_addr, 8, rst.buf);
    } else {
        // Reboot
        rst.val = 1;
        tap_->write(sw_rst_addr, 8, rst.buf);
        RISCV_sleep_ms(10);
        rst.val = 0;
        tap_->write(sw_rst_addr, 8, rst.buf);
    }
}

}  // namespace debugger
