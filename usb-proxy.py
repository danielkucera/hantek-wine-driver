#!/usr/bin/python3

import socket
import struct
import time
import usb.core
import usb.util

HOST = "0.0.0.0"
PORT = 8484

VENDOR_REQUEST=0x40
DEVICE_TO_HOST=0x80

def debug(*args, **kwargs):
    return
    print(args, kwargs)

class Device:
    def __init__(self):
        dev = usb.core.find(idVendor=0x04b5, idProduct=0x6cde )

        if dev is None:
            raise ValueError('Device not found')

        dev.reset()

        # set the active configuration. With no arguments, the first
        # configuration will be the active one
        dev.set_configuration()

        # get an endpoint instance
        cfg = dev.get_active_configuration()

        intf = cfg[(0,0)]

        dev.set_interface_altsetting(interface = 0, alternate_setting = 0)

        self.ep2 = usb.util.find_descriptor(
            intf,
            custom_match = \
            lambda e: \
                e.bEndpointAddress == 2)

        self.ep6 = usb.util.find_descriptor(
            intf,
            custom_match = \
            lambda e: \
                e.bEndpointAddress == 0x86)

        assert self.ep2 is not None
        assert self.ep6 is not None

        self.dev = dev

    # USB communication
    def ctrl(self, rtype, req, data, error=None, wValue=0, wIndex=0):
        debug("ctrl", rtype, req, data, wValue, wIndex)
        try:
            ret = self.dev.ctrl_transfer(rtype, req, wValue, wIndex, data)
        except usb.core.USBError as e:
            print("got", e.errno, e)
            if e.errno == error:
                return
            else:
                raise e
        return ret

    def bwrite(self, data, without_rst=False):
        if not without_rst:
            self.rst()
        self.ep2.write(data)

    def bread(self, length):
        timeout = 3000
        return self.dev.read(self.ep6.bEndpointAddress, length, timeout)


def process_cmd(io_num, indata, outdata):
    debug("--- process ---")
    status = 0
    in_data = b""
    out_data = b""
    debug(hex(io_num))
    if io_num == 0x222059:
        devToHost = (indata[0] == 1)
        bmRequestType = VENDOR_REQUEST + DEVICE_TO_HOST * devToHost;
        bRequest = indata[4];
        wValue = indata[6] + indata[7] * 0x100;
        wIndex = indata[8] + indata[9] * 0x100;
        error = None
        if bRequest == 234:
            error = 32
        try:
            if devToHost:
                out_data = device.ctrl(bmRequestType, bRequest, len(outdata), error=error, wValue=wValue, wIndex=wIndex)
            else:
                device.ctrl(bmRequestType, bRequest, outdata, error=error, wValue=wValue, wIndex=wIndex)
        except Exception as e:
            print(e)
            status = 1;
    elif io_num == 0x222051:
        debug("bwrite", outdata)
        device.bwrite(outdata, without_rst=True)
    elif io_num == 0x22204e:
        to_read = len(outdata)
        debug("bread", to_read)
        try:
            out_data = device.bread(to_read)
        except:
            status = 1

    if not out_data:
        out_data = b""
    ret = (status, in_data, out_data)
    debug(ret)
    return ret

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((HOST, PORT))
    s.listen()

    while True:

        conn, addr = s.accept()
        with conn:
            print(f"Connected by {addr}")
            try:
                device = Device()
                while True:
                    data = conn.recv(8)
                    (io_num, in_len, out_len) = cmd_head = struct.unpack("<IHH", data)
                    debug(cmd_head)
                    in_data = conn.recv(in_len)
                    out_data = conn.recv(out_len)
        
                    (status, in_data, out_data) = process_cmd(io_num, in_data, out_data)
                    res_head = struct.pack("<BHH", status, len(in_data), len(out_data))
                    debug("sending", res_head)
                    conn.send(res_head)
                    if len(in_data) > 0:
                        conn.send(in_data)
                    if len(out_data) > 0:
                        conn.send(out_data)
            except struct.error as e:
                print(e)
    
