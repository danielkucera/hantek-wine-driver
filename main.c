/*
 * hantek.sys
 *
 * Copyright 2022 Daniel Kucera
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "ddk/wdm.h"
#include "wine/debug.h"
#include "winioctl.h"
#include <stdint.h>

#include <libusb-1.0/libusb.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#include <errno.h>
#include <stdlib.h>

#define VENDOR_REQUEST 0x40
#define DEVICE_TO_HOST 0x80

#define DEFAULT_PORT 8484

int sock = 0, client_fd;

WINE_DEFAULT_DEBUG_CHANNEL(hantek);

libusb_device_handle *usbdev;

struct __attribute__((__packed__)) hantekCommand {
    uint32_t io_code;
    uint16_t input_len;
    uint16_t output_len;
};

struct __attribute__((__packed__)) hantekResponse {
    uint8_t ret_val;
    uint16_t input_len;
    uint16_t output_len;
};

static NTSTATUS WINAPI hantek_ioctl_usb( DEVICE_OBJECT *device, IRP *irp )
{
    IO_STACK_LOCATION *irpsp = IoGetCurrentIrpStackLocation( irp );

    int r;
    int requestTimeout = 5000;

    TRACE( "ioctl %x insize %u outsize %u\n",
           irpsp->Parameters.DeviceIoControl.IoControlCode,
           irpsp->Parameters.DeviceIoControl.InputBufferLength,
           irpsp->Parameters.DeviceIoControl.OutputBufferLength );
    TRACE("outdata %s\n", wine_dbgstr_an(irp->UserBuffer, irpsp->Parameters.DeviceIoControl.OutputBufferLength));
    TRACE("indata %s\n", wine_dbgstr_an(irp->AssociatedIrp.SystemBuffer, irpsp->Parameters.DeviceIoControl.InputBufferLength));

    uint8_t* indata = irp->AssociatedIrp.SystemBuffer;

    int transferSize;
    switch(irpsp->Parameters.DeviceIoControl.IoControlCode) {
	case 0x222059:
    		TRACE( "control transfer\n" );
		// libusb_control_transfer(libusb_device_handle *dev_handle, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,	unsigned char *data, uint16_t wLength, unsigned int timeout)
		uint8_t bmRequestType = VENDOR_REQUEST + DEVICE_TO_HOST * (indata[0] == 1);
		uint8_t bRequest = indata[4];
		uint16_t wValue = indata[6] + indata[7] * 0x100;
		uint16_t wIndex = indata[8] + indata[9] * 0x100;
		if (indata[0]) {
			r = libusb_control_transfer(usbdev, bmRequestType, bRequest, wValue, wIndex, (unsigned char*)irp->UserBuffer, (uint16_t)irpsp->Parameters.DeviceIoControl.OutputBufferLength, requestTimeout);
    			TRACE("control read reqlen %d readlen %d %s\n", irpsp->Parameters.DeviceIoControl.OutputBufferLength, r, wine_dbgstr_an(irp->UserBuffer, r));
			if (r < 0) {
				irp->IoStatus.Information = 0;
			} else {
				irp->IoStatus.Information = r;
			}

			irp->IoStatus.Status = STATUS_SUCCESS;
		} else {
			r = libusb_control_transfer(usbdev, bmRequestType, bRequest, wValue, wIndex, (unsigned char*)irp->UserBuffer, (uint16_t)irpsp->Parameters.DeviceIoControl.OutputBufferLength, requestTimeout);
			irp->IoStatus.Information = 0;
			irp->IoStatus.Status = STATUS_SUCCESS;
		}
		break;
	case 0x222051:
    		TRACE( "write bulk\n" );
		r = libusb_bulk_transfer(usbdev, 0x02, (unsigned char*)irp->UserBuffer, irpsp->Parameters.DeviceIoControl.OutputBufferLength, &transferSize, requestTimeout);
    		TRACE("write bulk reqlen %d writelen %d\n", irpsp->Parameters.DeviceIoControl.OutputBufferLength, transferSize);
		irp->IoStatus.Information = 0;
		irp->IoStatus.Status = STATUS_SUCCESS;
		break;
	case 0x22204e:
    		TRACE( "read bulk\n" );
		r = libusb_bulk_transfer(usbdev, 0x86, (unsigned char*)irp->UserBuffer, irpsp->Parameters.DeviceIoControl.OutputBufferLength, &transferSize, requestTimeout);
    		TRACE("read bulk reqlen %d readlen %d %s\n", irpsp->Parameters.DeviceIoControl.OutputBufferLength, transferSize, wine_dbgstr_an(irp->UserBuffer, r));
		irp->IoStatus.Information = transferSize;
		irp->IoStatus.Status = STATUS_SUCCESS;
		break;
	default:
    		TRACE( "------ unhandled ioctl --------\n" );
    }

    TRACE( "------ IOCTL COMPLETE --------\n" );
    IoCompleteRequest( irp, IO_NO_INCREMENT );
    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI hantek_ioctl_tcp( DEVICE_OBJECT *device, IRP *irp )
{
    IO_STACK_LOCATION *irpsp = IoGetCurrentIrpStackLocation( irp );

    struct hantekCommand cmd;
    struct hantekResponse res;
    int valread;

    TRACE( "ioctl %x insize %u outsize %u\n",
           irpsp->Parameters.DeviceIoControl.IoControlCode,
           irpsp->Parameters.DeviceIoControl.InputBufferLength,
           irpsp->Parameters.DeviceIoControl.OutputBufferLength );
    TRACE("outdata %s\n", wine_dbgstr_an(irp->UserBuffer, irpsp->Parameters.DeviceIoControl.OutputBufferLength));
    TRACE("indata %s\n", wine_dbgstr_an(irp->AssociatedIrp.SystemBuffer, irpsp->Parameters.DeviceIoControl.InputBufferLength));

    cmd.io_code = irpsp->Parameters.DeviceIoControl.IoControlCode;
    cmd.input_len = irpsp->Parameters.DeviceIoControl.InputBufferLength;
    cmd.output_len = irpsp->Parameters.DeviceIoControl.OutputBufferLength;

    send(sock, &cmd, sizeof(cmd), 0);
    send(sock, irp->AssociatedIrp.SystemBuffer, cmd.input_len, 0);
    send(sock, irp->UserBuffer, cmd.output_len, 0);

    valread = recv(sock, &res, sizeof(res), MSG_WAITALL);
    TRACE("res len: %d ret: %d in_len: %d, out_len:%d \n", valread, res.ret_val, res.input_len, res.output_len);
    if(valread != sizeof(res)){
	    TRACE("short header, len %d\n", valread);
	    return STATUS_PIPE_NOT_AVAILABLE;
    }

    if (res.input_len) {
        valread = recv(sock, irp->AssociatedIrp.SystemBuffer, res.input_len, MSG_WAITALL);
        if(valread != res.input_len){
    	    TRACE("short in_data\n");
    	    return STATUS_PIPE_NOT_AVAILABLE;
        }
    }

    if (res.output_len) {
        valread = recv(sock, (unsigned char*)irp->UserBuffer, res.output_len, MSG_WAITALL);
        if(valread != res.output_len){
    	    TRACE("short out_data len: %d %s\n", valread, strerror(errno));
    	    return STATUS_PIPE_NOT_AVAILABLE;
        }
    }

    irp->IoStatus.Information = res.output_len + res.input_len;
    irp->IoStatus.Status = res.ret_val;

    TRACE( "------ IOCTL COMPLETE --------\n" );
    IoCompleteRequest( irp, IO_NO_INCREMENT );
    return STATUS_SUCCESS;
}

int hantek_connect(DRIVER_OBJECT *driver) {
    struct sockaddr_in serv_addr;
    int hantek_port = DEFAULT_PORT;

    const char* hantek_port_var = getenv("HANTEK_PORT");
    const char* hantek_host_var = getenv("HANTEK_HOST");

    if(hantek_port_var) {
        hantek_port = atoi(hantek_port_var);
    }

    if(!hantek_host_var) {
        const struct libusb_version* version;
        int r;
        version = libusb_get_version();
        TRACE("Using libusb v%d.%d.%d.%d\n\n", version->major, version->minor, version->micro, version->nano);
        r = libusb_init(NULL);
        if (r < 0) {
        	TRACE("libusb_init failed with code %d\n", r);
    	    return STATUS_INVALID_PARAMETER;
        }
    
        usbdev = libusb_open_device_with_vid_pid(NULL, 0x04b5, 0x6cde);
    
        if (usbdev == NULL) {
        	TRACE("hantek device not found\n");
	    	return STATUS_DEVICE_DOES_NOT_EXIST;
        }

    	driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = hantek_ioctl_usb;
	return 0;
    }

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        ERR("Socket creation error");
	return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(hantek_port);

    ERR("Hantek connecting to %s:%d\n", hantek_host_var, hantek_port);

    if (inet_pton(AF_INET, hantek_host_var, &serv_addr.sin_addr) <= 0) {
        ERR("Invalid address/ Address not supported %s\n", hantek_host_var);
        return -1;
    }

    if ((client_fd = connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) < 0) {
        ERR("Connection Failed\n");
        return -1;
    }

    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));

    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = hantek_ioctl_tcp;
    return 0;
}

NTSTATUS WINAPI DriverEntry( DRIVER_OBJECT *driver, UNICODE_STRING *path )
{
    NTSTATUS status;
    UNICODE_STRING nameW, dos_nameW;
    DEVICE_OBJECT *device;

    // d6CDE-0
    static const WCHAR hantek_deviceW[] = {'\\','D','e','v','i','c','e','\\','d','6','C','D','E','-','0',0};
    static const WCHAR hantek_dos_deviceW[] = {'\\','D','o','s','D','e','v','i','c','e','s','\\','d','6','C','D','E','-','0',0};

    if (hantek_connect(driver)){
    	return STATUS_DEVICE_DOES_NOT_EXIST;
    }

    TRACE( "(%p, %s) hantek\n", driver, debugstr_w(path->Buffer) );
    TRACE( "create device hantek (%s) dos device (%s)\n", debugstr_w(hantek_deviceW), debugstr_w(hantek_dos_deviceW));


    RtlInitUnicodeString( &nameW, hantek_deviceW );
    RtlInitUnicodeString( &dos_nameW, hantek_dos_deviceW );

    //status = IoCreateDevice( driver, 0, &nameW, 0, 0, FALSE, &device );
    status = IoCreateDevice( driver, 0, &nameW, FILE_DEVICE_DISK, 0, FALSE, &device );
    if (status) {
        FIXME( "failed to create device %x\n", status );
        return status;
    }

    status = IoCreateSymbolicLink( &dos_nameW, &nameW );
    if (status) {
        FIXME( "failed to create symlink %x\n", status );
        return status;
    }

    return STATUS_SUCCESS;
}
