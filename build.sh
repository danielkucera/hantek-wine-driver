#!/bin/bash -e

make
sudo cp hantek.sys.so /usr/lib/x86_64-linux-gnu/wine/
cp hantek.sys.fake ~/.wine/drive_c/windows/system32/drivers/hantek.sys
#wine sc delete Hantek

#define SERVICE_BOOT_START   0x00000000
#define SERVICE_SYSTEM_START 0x00000001
#define SERVICE_AUTO_START   0x00000002
#define SERVICE_DEMAND_START 0x00000003
#define SERVICE_DISABLED     0x00000004

#define SERVICE_KERNEL_DRIVER      0x00000001
#define SERVICE_FILE_SYSTEM_DRIVER 0x00000002
#define SERVICE_ADAPTER            0x00000004
#define SERVICE_RECOGNIZER_DRIVER  0x00000008

# start: boot = 0 auto = 2  type: kernel = 1 filesys = 2

sleep 1

WINEDEBUG=trace+hantek wine sc create Hantek binPath= C:\\windows\\system32\\drivers\\hantek.sys type= kernel start= auto
