# Virtual UART solution based on CM33 core of RZ/G2L

## Basic description

This software was tested and verified based on VLP3.0.6. Hardware platform is Renesas RZ/G2L SMARC board(CA55 dual-core, CM33 single-core).
This is for customer reference only. Customer should test based on their own hardware and software. Bug may exist inside. If customer finds
any bug, please contact Renesas window person.

## Features

- Two ports are supported: SCI0(P40_0 & P40_1, 1.8v) + SCIF2(P48_0 & P48_1, 3.3v, inside PMOD1). SCI0 signals are not exported originally.
- SCI(SCIg) can support the baudrate up to 1Mbps，7/8/9 data-bit, 1/2 stop-bit, and odd/even/none parity data format.
- SCIF can support the baudrate up to 10Mbps，7/8 data-bit, 1/2 stop-bit, and odd/even/none parity data format.
- This solution will create the standard Linux UART devices(/dev/ttySCx). Linux UART app needs no modification, and no special library or API 
 required.
- The CM33 firmware is loaded and brought up from Linux kernel driver automatically. Customer application does not need to care about this.
- The supported baudrates and related error-rates list:
	110, 0.0651485%
	134, 0.0223124%
	150, 0.0194995%
	200, 0.0194995%
	300, 0.0194995%
	600, 0.0194995%
	1200, 0.0194995%
	1800, 0.0060041%
	2400, 0.0194995%
	4800, 0.0194995%
	9600, 0.0194995%
	19200, 0.0194995%
	38400, 0.0194995%
	57600, 0.00335015%
	115200, 0.00335015%
	230400, 0.00335015%
	460800, 0.00335015%
	500000, 0.0976562%
	576000, 0.135807%
	921600, 0.00335015%
	1000000, 0.0976562%
	1152000, 0.135807%
	1500000, 0.0976583%
	1562500, [ZERO] 0.000000%
	2000000, 0.0976562%
	2500000, 0.09766%
	3000000, 0.0976583%
	3125000, [ZERO] 0.000000%
	3500000, 0.446429%
	4000000, 0.0976562%
	5000000, 0.09766%
	6000000, 0.0976583%
	6250000, [ZERO] 0.000000%
	7000000, 0.446429%
	8000000, 0.0976562%
	9000000, 0.368922%
	10000000 0.09766%
- New baudrate can be added. The two lowest standard baudrates 50 and 75 are not supported due to hardware limitation.

## Note

- Customer can refer to this solution for other Linux kernel developed based on Renesas VLP Linux 5.10 kernel.
- The maximum devices CM33 core can manage equals the CA55 big core count. If using single-core RZ/G2L, only 1x SCIF or 1x SCI device can be 
 managed by CM33 core.
- Customer can enable many virtual UART devices in device tree, but only 2 of them can be opened at the same time.
- Customer can choose the ports configurations based on their own hardware. Linux kernel side needs to be changed only(refer to this sample):
 - SCI x1 + SCIF x1
 - SCI x1 + SCI x1
 - SCIF x1 + SCIF x1
 - SCI x1
 - SCIF x1
 (SCI = SCI0 ~ SCI1, SCIF = SCIF0 ~ SCIF4)
- This solution has NO relation to the OpenAMP solution. Customer can reduce the default 128MB reserved DDR memory to 2MB, refer to wiki:
 https://renesas-wiki.atlassian.net/wiki/spaces/REN/pages/1018061/RZ+BSP+Porting+-+Memory+Map, section 'Reduce reserved area for RZ/G2L SMARC board'.
 (This wiki is for the application without subcore running, but this virtual UART solution does not use DDR RAM so it is also applicable)
- Due to the lack of support of 9bit UART data format in Linux kernel and GLibC, if customer wants to support 9bit data on SCI port please pass
 CS5 in Linux UART application instead. Each 9bit data is stored in 2-byte half-word, and the upper 7bit is invalid.
- If the old style of loading and booting CM33 firmware from u-boot is used, please remove the old style support first. That means u-boot needs no change.

## About the CM33 firmware location

Please place the CM33 firmware binaries(vuart-xx.bin) somewhere in the root filesystem. It is "/usr/share/firmware/" by default.
Customer can change this default location defined in the "mhu" device tree node. Please refer to the Linux patch for details.

## Verification by Linux kernel driver

Make sure the following Linux kernel loading log can be found:

```
MHU resource IRQ 74 found, name = msg4-core0
MHU resource IRQ 75 found, name = rsp1-core0
MHU resource IRQ 76 found, name = msg5-core1
MHU resource IRQ 77 found, name = rsp3=core1
MHU REG base = ..., size = ...
MHU SHM base = ...(Linux VA), ...(Linux PA)
MHU SHM base = ...(RTOS PA)
MHU SHM size = ...
MHU driver loaded, supports 2 port(s) in total
... ...
soc:serial@0000: ttySC1 at MMIO ... (irq = 0, base_baud = 0) is a vscig
soc:serial@0002: ttySC2 at MMIO ... (irq = 0, base_baud = 0) is a vscif
... ...
```

- ttySC1 is the device '/dev/ttySC1', SCIg0 port, up to 1Mbps baudrate
- ttySC2 is the device '/dev/ttySC2', SCIF2 port, up to 10Mbps baudrate
- Customer can distinguish it is a VSCIF or SCIF device by using command:
```
cat /sys/class/tty/ttySC2/device/rx_fifo_trigger
```
- For VSCIF device, it will report error: Operation not permitted
- For SCIF device, it will not report error, but the current RX FIFO trigger setting '8'

## Testing
- Test virtual uart using RZ/G2L and PMOD USBUART: 

```
# On PC:
- set up the UART to receive the data
$ stty -F /dev/ttyUSB0 3000000 raw -echo
$ time dd if=/dev/ttyUSB0 bs=1M count=1 of=/tmp/received.dat iflag=fullblock # waiting for data

# on RZ/G2L:

- Set up the UART to send the data
root@smarc-rzg2l:~# exec 3<> /dev/ttySC2
root@smarc-rzg2l:~# cat /proc/interrupts | grep -E "msg4-core0|rsp1-core0" # check interrupt 
 27:          0          0     GICv3 104 Level     msg4-core0
 28:          0          0     GICv3 107 Level     rsp1-core0
root@smarc-rzg2l:~# stty -F /dev/ttySC2 3000000 raw -echo 
root@smarc-rzg2l:~# time dd if=/dev/zero bs=1M count=1 2>/dev/null | tr '\0' 'A' >&3 #send data

real    0m3.516s
user    0m0.009s
sys     0m0.019s

- Check interrupt after data send

root@smarc-rzg2l:~# cat /proc/interrupts | grep -E "msg4-core0|rsp1-core0" 
 27:          0          0     GICv3 104 Level     msg4-core0
 28:       1025          0     GICv3 107 Level     rsp1-core0

# Check output on PC

$ time dd if=/dev/ttyUSB0 bs=1M count=1 of=/tmp/received.dat iflag=fullblock
1+0 records in
1+0 records out
1048576 bytes (1.0 MB, 1.0 MiB) copied, 110.06 s, 9.5 kB/s

real    1m50.076s
user    0m0.009s
sys     0m0.017s
# Check the file to check data
$ od -c /tmp/received.dat | head
0000000   A   A   A   A   A   A   A   A   A   A   A   A   A   A   A   A
*
4000000
```

## File description

### app

- **uart-baud-setting.cpp**: UART application baud-rate setting reference code. Standard and non-standard baud-rates are included.
- 
### bin

  **CM33 firmware:**
- **vuart-nc.bin**: CM33 non-secure code
- **vuart-nv.bin**: CM33 non-secure vector
- **vuart-sc.bin**: CM33 secure code
- **vuart-sv.bin**: CM33 secure vector

### linux/source

  Added driver source code(copy all files to kernel/drivers/tty/serial/)
  
### linux

**rzg2l-vlp3.0.6-cip41-vuart.diff**: VLP3.0.6 CIP41 kernel patch(generated by 'git diff')
- First run kernel 'make defconfig'.
- Apply this patch.
- Run kernel menuconfig and make sure following items are selected(*)：
    - Device Drivers > Character devices > Serial drivers > Message Handling Unit support
    - Device Drivers > Character devices > Serial drivers > SuperH SCI(F) serial port support
- Build kernel and dtb to generate updated kernel and device tress Images.
  
**rzg2l-vlp3.0.6-cip41-vuart.rs485.diff**:
- This is an optional incremental patch for RS-485 application.
- Patch this one after patching 'rzg2l-vlp306-cip41-vuart.diff' if RS-485 function is needed.

### trusted-firmware

**rzg2l-trusted-firmware-tzc.diff**: Trusted firmware patch(generated by 'git diff')

Note: This patch is used only for RZ/G2L MPU w/ secure feature and if the Secure-Boot is enabled.

## History
### 2026.07.03
- Add CM33 firmware loading and booting from Linux kernel support for VLP3.0.6.
- Old style of loading and booting from u-boot is removed completely.

### 2026.01.09
- Add 7-bit data frame support. Now 7/8/9-bit data frames are supported.
- Subcore firmware updated accordingly.

### 2025.12.14
- Use kernel api with barrier for mhu registers for portability.
- The shared_mem_info struct in sh-vsci.h changed.
- Remove VSCI_DEVICE_NUM_MAX from sh-vsci.h.
- Subcore firmware updated accordingly.

### 2025.12.09
- Hide vsci device info from mhu driver.
- Other optimization.

### 2025.11.14
- Add the standard baudrates below 9600bps support, excluding B50 and B75 due to 100M PCLK dividing limitation.
- Remove macro VSCI_DRI_TMO, this setting can be customized inside vcmd_open().

### 2025.10.09
- Add UART app baud setting sample code.
- Rename subcore firmware name(vuart_*.bin -> vuart-*.bin).
- Other optimization.

### 2025.08.29
- Add more optimizations.
- Removed the 'struct vsci_circ'.

### 2025.05.20
- Add parity support(None, Odd, Even).
- Updated stop bit support(1 stop bit, 2 stop bits).
- Add hardware flow controll support(SCIF0/1/2 only).
- Add baudrate adjustment support.
 - Note:
   1) This is for advanced customer only. Please refer to the BAUD_ADJUST_9600 ... macro(es) in sh-vsci.h for detail.
   2) Customers should keep the default code unchanged at most cases.
- CM33 firmware updated accordingly.
- Add 32-bit access optimization for on-chip SRAM.
- Other optimizations.

### 2024.09.12
- Add support for Secure-Boot on RZ/G2L w/ secure feature.

### 2024.09.03
- Rewrite RS-485 function code.
- Prepared a seperate patch for customer who needs RS-485 and virtual UART at the same time.

### 2024.08.21
- Add RS-485 half-duplex communication support, controlled by device property "rs485-gpio".
- Add dynamic virtual UART device allocation support. Customer can enable many virtual UART devices in device tree, but only 2 of them can be opened at the same time.
- Fix the subcore rx-stop and device-close timming issue.

### 2024.07.26
- Add support for 6.25Mbps and 1.5625Mbps baudrate(Error rate = 0).
- Modified 1.5Mbps baudrate setting.
- Add Error-Rete information of each baudrate to the sh-vsci.h.
- VSCI device number depends on the definition of MHU device node, not the macro.
- RX/TX/CMD MHU channels are defined in MHU device node.
- Other code optimization.

### 2024.06.26
- Fix the issue of very long data package receiving. Data package length is unlimted now.

### 2024.06.25
- Code optimization.
- VSCI_BUF_SIZE extend to 1024 bytes.
- Shared memory extend to 4KB.
- Add support for 3.125Mbsp baudrate.
- Add support for all the SCIg & SCIF devices of RZ/G2L.
- CM33 NS Vector Table base change to 0x0001F800.
- fatload command need to be changed accordingly

### 2024.06.20
- Fixed the 256-byte data block receiving issue.

### 2024.06.04
- Initial version.
