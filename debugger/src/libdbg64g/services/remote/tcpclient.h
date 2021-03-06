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

#ifndef __DEBUGGER_TCPCLIENT_H__
#define __DEBUGGER_TCPCLIENT_H__

#include <iclass.h>
#include <iservice.h>
#include "tcpcmd_gen.h"
#include "coreservices/ithread.h"
#include "coreservices/irawlistener.h"

namespace debugger {

class TcpClient : public IService,
                  public IThread,
                  public IRawListener {
 public:
    explicit TcpClient(const char *name);
    virtual ~TcpClient();

    /** IService interface */
    virtual void postinitService();
    virtual void setExtArgument(void *args) {
        hsock_ = *reinterpret_cast<socket_def *>(args);
    }

    /** IRawListener interface */
    virtual int updateData(const char *buf, int buflen);

 protected:
    /** IThread interface */
    virtual void busyLoop();

 protected:
    int sendData(uint8_t *buf, int sz);
    void closeSocket();

 private:
    AttributeType isEnable_;
    AttributeType timeout_;
    AttributeType platformConfig_;
    AttributeType type_;
    AttributeType listenDefaultOutput_;

    socket_def hsock_;
    mutex_def mutexTx_;
    char rcvbuf[4096];
    char txbuf_[1<<20];
    int txcnt_;
    union reg8_type {
        char ibyte;
        uint8_t ubyte;
    } asyncbuf_[1 << 20];

    TcpCommandsGen *tcpcmd_;
};

DECLARE_CLASS(TcpClient)

}  // namespace debugger

#endif  // __DEBUGGER_TCPCLIENT_H__
