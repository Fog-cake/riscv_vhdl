"""
 @copyright  Copyright 2017 GNSS Sensor Ltd. All right reserved.
 @author     Sergey Khabarov - sergeykhbr@gmail.com
 @brief      Transport TCP layer of the Python API.
"""

import threading
import socket
import time
from safe import safe_print

TCP_IP = '127.0.0.1'
TCP_PORT = 8689
BUFFER_SIZE = 1024

TCP_DEBUG = 1

class TcpClient(threading.Thread):
    def __init__(self, name, eventDone):
        threading.Thread.__init__(self)
        self.name = name
        self.skt = None
        self.eventDone = eventDone
        self.messageid = 0
        self.enabled = True
        self.eventTx = threading.Event()
        self.response = ""
        self.console_listeners = []

    def run(self):
        safe_print("Connecting to {0}:{1}\n".format(TCP_IP, TCP_PORT))
        self.skt = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.skt.connect((TCP_IP, TCP_PORT))
        self.eventDone.set()

        buffer = ""
        while self.enabled:
             rxstr = self.skt.recv(BUFFER_SIZE)
             if rxstr == '':
                 self.enabled = False
                 continue

             buffer += rxstr
             if '\0' in buffer:
                 strlist = buffer.split('\0')
                 if buffer.endswith('\0'):
                     buffer = ""
                 else:
                     buffer = strlist[len(strlist) - 1]
                     strlist = strlist[:-1]

                 for s in strlist:
                     if len(s) == 0:
                         continue
                     if TCP_DEBUG == 1:
                         safe_print("i<= {0}".format(s))
                     json = s.translate(None,'\n\r\0')
                     json = eval(json)
                     if type(json) is list:
                          self.response = json
                          self.eventTx.set()
                     else:
                          raise ValueError(
                            'Unexpected DpiServer response: {0}'.format(json))
             else:
                 pass

        # Ending of the thread:
        safe_print("Thread {0} stopped".format(self.name))

    def stop(self):
        self.enabled = False
        self.skt.shutdown(socket.SHUT_WR)

    def send(self, data):
        tx = bytearray(str(data))
        tx.append(0)
        if TCP_DEBUG == 1:
            safe_print("o=> {0}".format(tx))

        self.eventTx.clear()
        self.response = ""
        self.skt.send(tx)
        self.eventTx.wait()
        return self.response

        