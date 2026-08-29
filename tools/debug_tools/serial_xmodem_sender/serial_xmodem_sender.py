#!/usr/bin/env python
"""
AIC IoT SDK
Copyright (C) 2018-2025 AICSemi Ltd.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Note: Use Python 2.7
"""
import sys
from os.path import isfile
import serial
from xmodem import XMODEM
from time import sleep
import logging
import logging.config

def config_serial(port, baudrate, timeout):
    print('config port:', port, 'baud:', baudrate, 'to:', timeout)
    comPort.port = port
    comPort.baudrate = baudrate
    comPort.timeout = timeout

def getc(size, timeout=1):
    return comPort.read(size) or None

def putc(data, timeout=1):
    comPort.write(data)
    # sleep(0.001)

def send_lf():
    print('send:', '\r')
    putc('\r')
    sleep(0.01) # 10ms at least
    while True:
        c = getc(10)
        if c == None:
            break
        print('getc:', c)

def send_file_xmodem(local_file, addr_hex):
    print('send:', 'x %s\r' % addr_hex)
    putc('x %s\r' % addr_hex)
    sleep(0.02)
    while True:
        c = getc(80)
        if c == None:
            break
        print('recv:', c)
    while True:
        c = comPort.read(1)
        if c == 'C' or c == '\x15':
            print('xmodem send...')
            break
        sleep(0.1)
    send_buf = open(local_file, 'rb')
    # use xmodem-1k mode
    m = XMODEM(getc, putc, mode='xmodem1k')
    # flush all the 'C' or '\x15' recvd
    while True:
        c = getc(80)
        if c == None:
            break
        print('recv:', c)
    # start sending
    ret = m.send(send_buf)
    print('xmodem ret:', ret)
    send_buf.close()
    while True:
        c = getc(128)
        if c == None:
            break
        print('recv:', c)

if __name__ == '__main__':
    if len(sys.argv) >= 4 and isfile(sys.argv[1]):
        bin_path = sys.argv[1]
        addr_hex = sys.argv[2]
        ser_port = sys.argv[3]
        baud_rate = '921600'
        if len(sys.argv) > 4:
            baud_rate = sys.argv[5]
        logging.config.fileConfig('logging.conf')
        root_logger = logging.getLogger('root')
        root_logger.debug('d: test root logger...')
        root_logger.info('i: test root logger...')
        xm_logger = logging.getLogger('xmodem')
        xm_logger.debug('d: test xmodem logger...')
        xm_logger.info('i: test xmodem logger...')
        comPort = serial.Serial(parity=serial.PARITY_NONE, bytesize=serial.EIGHTBITS, stopbits=serial.STOPBITS_ONE)
        config_serial(ser_port, int(baud_rate), 0.1)
        comPort.open()
        send_lf()
        sleep(1)
        send_file_xmodem(bin_path, addr_hex)
        comPort.flush()
        comPort.flushInput()
        comPort.close()
        print('Done.')
    else:
        print 'Usage: ' + sys.argv[0] + ' [bin_file] [hex_addr] [serial_port] <baud_rate>'

