 --- Virtual UART solution based on CM33 core of RZ/G2L ---

Basic description:
This software was tested and verified based on VLP3.0.6. Hardware platform is Renesas RZ/G2L SMARC board(CA55 dual-core, CM33 single-core).
This is for customer reference only. Customer should test based on their own hardware and software. Bug may exist inside. If customer finds
any bug, please contact Renesas window person.

Features:
 - Two ports are supported: SCI0(P40_0 & P40_1, 1.8v) + SCIF2(P48_0 & P48_1, 3.3v, inside PMOD1). SCI0 signals are not exported originally.
 - SCI0 can support the baudrate up to 1Mbps，8bit or 9bit data format, no parity support.
 - SCIF2 can support the baudrate up to 10Mbps，8bit data format only, no parity support.
 - For supported baudrates, please refer to the 'enum vsci_br' definition inside file sh-vsci.h. This enum definition is used by kernel virtual 
   UART driver and CM33 firmware. Linux UART application should not use it.

Note:
 - This solution has NO relation to the OpenAMP solution. Customer can reduce the default 128MB reserved DDR memory to 2MB, refer to wiki:
   https://renesas.info/wiki/RZ-G/RZ-G2_BSP_MemoryMap, section 'Reduce reserved area for RZ/G2L SMARC board'.
 - Due to the lack of support of 9bit UART data format in Linux kernel and GLibC, if customer wants to support 9bit data on SCI0 port please pass
   CS7 in Linux UART application instead. For 8bit data format, CS8 is used. SCIF ports can suport 8-bit data only.
 - Customer should load CM33 firmware and bring up CM33 core first inside u-boot. Then boot into Linux system.
 - CM33 firmware loading and booting(Assume the CM33 firmware is located in VFAT partition 1, the 1st partition of eMMC):
   - mmc dev 0
   - dcache off
   - fatload mmc 0:1 $loadaddr vuart_nc.bin
   - cp.b $loadaddr 0x00010000 0x4000
   - fatload mmc 0:1 $loadaddr vuart_nv.bin
   - cp.b $loadaddr 0x0001F800 0x800
   - fatload mmc 0:1 $loadaddr vuart_sc.bin
   - cp.b $loadaddr 0x0002D400 0x3C0
   - fatload mmc 0:1 $loadaddr vuart_sv.bin
   - cp.b $loadaddr 0x0002FF80 0x80
   - dcache on
   - cm33 start_debug 0x1002FF80 0x0001F800
 - Make sure the following Linux kernel log can be found:
    MHU resource IRQ 74 found, name = msg4-core0
    MHU resource IRQ 75 found, name = rsp1-core0
    MHU resource IRQ 76 found, name = msg5-core1
    MHU resource IRQ 77 found, name = rsp3=core1
    ... ...
    soc:serial@0000: ttySC1 at MMIO 0x20100 (irq = 0, base_baud = 0) is a vsci
    soc:serial@0002: ttySC3 at MMIO 0x20118 (irq = 0, base_baud = 0) is a vscif
    ... ...
	- ttySC1 is the device '/dev/ttySC1', SCI0, up to 1Mbps baudrate
    - ttySC3 is the device '/dev/ttySC3', SCIF2, up to 10Mbps baudrate


File description:
bin：
  bl2_bp.bin：Trusted-firmware，binary format
  bl2_bp.srec：Trusted-firmware，SREC format
  fip.bin：BL3（u-boot），binary format
  fip.srec：BL3（u-boot），SREC format
  Flashwriter.mot：Flashwriter for SCIF download mode
  Image: patched Linux kernel, based on VLP3.0.6 CIP41
  r9a07g044l2-smarc.dtb：modified kernel device tree
  rzg2_initramfs.cpio.gz：ramdisk, optional. Customer can choose other rootfs.
    - bootargs="initrd=0x70000000,32M' for uboot. Load to 0x70000000 first.
  CM33 firmware:
    vuart_nc.bin：CM33 non-secure code
    vuart_nv.bin：CM33 non-secure vector
    vuart_sc.bin：CM33 secure code
    vuart_sv.bin：CM33 secure vector
linux/source：
  copy all files to kernel/drivers/tty/serial/
linux：
  rzg2l-vlp306-vuart-vXXX.diff：VLP3.0.6 CIP41 kernel patch
    - First run 'make defconfig' to generate base configuration for your kernel.
      After applying this patch, please run kernel menuconfig and make sure following items are selected(*)：
        Device Drivers > Character devices > Serial drivers > Message Handling Unit support
        Device Drivers > Character devices > Serial drivers > SuperH SCI(F) serial port support
      Build the kernel to generate updated kernel and device tree images.
    - This patch should be OK for other RZ/G2L Linux kernel based on VLP Linux kernel.
u-boot：
  u-boot.diff：u-boot patch for cm33 command support
  cm33/cm33.c：source file for cm33 command support(copy cm33.c to u-boot_src/cmd/)

------ HISTORY ------
2024.06.26
Fix the issue of very long data package receiving. Data package length is unlimted now.

2024.06.25
Code optimization
VSCI_BUF_SIZE extend to 1024 bytes
Shared memory extend to 4KB
Add support for 3.125Mbsp baudrate
Add support for all the SCIg & SCIF devices of RZ/G2L
CM33 NS Vector Table base change to 0x0001F800
 - fatload command need to be changed like above

2024.06.20
Fixed the 256-byte data block receiving issue.

2024.06.04
Initial version
