 --- RZ/G2L基于CM33小核的虚拟串口方案 ---

基础说明：
软件基于瑞萨官方VLP调试通过，硬件平台是瑞萨RZ/G2L SMARC开发板（CPU是双核A55）。
仅供客户参考，验证需求，是否能满足客户需要，需要客户结合自己实际硬件软件实测，不保证没有bug。
如果客户发现了bug，请联系瑞萨FAE支持。

支持接口/特性：
 - SCI0(P40_0 & P40_1, 1.8v)
 - SCIF2(P48_0 & P48_1, 3.3v)
 - SCI可以支持到1Mbps，8bit or 9bit data，无校验位，1停止位
 - SCIF可以支持到10Mbps，8bit data，无校验位，1停止位
 - 支持的波特率，参考sh-vsci.h vsci_br（这个enum定义是给虚拟串口驱动程序和CM33固件使用的，Linux应用程序不要引用）
 - 本方案将创建标准Linux UART设备(/dev/ttySCx)，Linux UART应用程序通常不需要修改，也不需要使用特殊的库或者API。

注意：
 - 客户使用其他基于瑞萨VLP Linux 5.10 kernel修改过的kernel，也可以参考。
 - CM33能管理的最大设备数量等于CA55大核数量。如果使用单核RZ/G2L，CM33只能支持1路SCIF或者1路SCIg设备。
 - 客户可以基于自己的硬件选择SCI SCIF端口组合，仅仅需要修改Linux kernel部分，参考现有补丁即可：
     - SCI x1 + SCIF x1
	 - SCI x1 + SCI x1
	 - SCIF x1 + SCIF x1
	 - SCI x1
	 - SCIF x1
	 (SCI0 ~ SCI1, SCIF0 ~ SCIF4)
 - 此方案，跟OpenAMP没有关系，默认的128MB保留DDR内存可以减少到2MB，参考wiki：
  “Reduce reserved area for RZ/G2L SMARC board”章节，https://jira-gasg.renesas.eu/confluence/display/REN/RZ+BSP+Porting+-+Memory+Map
 - 由于gLibC和Linux kernel并不支持9bit data，配置SCI0 9bit数据时，需要传入CS7（kernel驱动会把CS7转换为9bit），8bit数据使用CS8。
   SCIF端口仅仅支持8bit数据。
 - CM33的固件（参考文件说明）需要首先在uboot下加载，然后再启动Linux kernel（可以从eMMC/SD/U盘/网络/串口等uboot支持的设备下载），例如：
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
   （使用其他加载方式时，确保加载文件名目标地址以及文件大小跟上述fatload、cp.b命令的一致）
 - kernel启动过程中（需要打补丁），确保看到如下几行Log：
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
    soc:serial@0000: ttySC1 at MMIO ... (irq = 0, base_baud = 0) is a vsci
    soc:serial@0002: ttySC3 at MMIO ... (irq = 0, base_baud = 0) is a vscif
    ... ...
    ttySC1就是/dev/ttySC1，对应SCI0，可达1Mbps
    ttySC3就是/dev/ttySC3，对应SCIF2，可达10Mbps

文件说明：
bin：
  bl2_bp.bin：Trusted-firmware，binary format
  bl2_bp.srec：Trusted-firmware，SREC format
  fip.bin：BL3（u-boot），binary format
  fip.srec：BL3（u-boot），SREC format
  Flashwriter.mot：Flashwriter for SCIF download mode
  Image：通过了测试的kernel镜像（不支持RS-485）
  r9a07g044l2-smarc.dtb：kernel对应的设备树
  rzg2_initramfs.cpio.gz：ramdisk，可选，客户可以使用其他rootfs
    - bootargs="initrd=0x70000000,32M'，首先加载到0x70000000处
  CM33固件：
    vuart_nc.bin：CM33 non-secure code
    vuart_nv.bin：CM33 non-secure vector
    vuart_sc.bin：CM33 secure code
    vuart_sv.bin：CM33 secure vector
linux/source：
  添加的驱动源码，复制所有文件到kernel/drivers/tty/serial/
linux：
  rzg2l-vlp306-cip41-vuart.diff：VLP3.0.6 CIP41 kernel patch(git diff生成)
    - 确保事先执行过make defconfig
	- 打这个补丁
	- 执行kernel menuconfig，确保选中下面两项(*)：
        Device Drivers > Character devices > Serial drivers > Message Handling Unit support
        Device Drivers > Character devices > Serial drivers > SuperH SCI(F) serial port support
    - 编译kernel和设备树，获得更新后的kernel和设备树镜像。
  rzg2l-vlp306-cip41-vuart.rs485.diff：
    - 补丁'rzg2l-vlp306-cip41-vuart.diff'基础上添加RS-485通信支持
  说明：这两个补丁二选一。
trusted-firmware：
  rzg2l-trusted-firmware-tzc.diff：Trusted firmware补丁(git diff生成）
  说明：如果使用Secure RZ/G2L且开启了Secure-Boot，才需要打这个补丁。
u-boot：
  u-boot.diff：u-boot下支持cm33命令的补丁
  cm33/cm33.c：u-boot下需要添加的代码，复制到u-boot/cm33目录

------ HISTORY ------
2024.09.12
添加了Secure RZ/G2L的Secure-Boot模式支持

2024.09.03
重写了RS-485相关代码。
准备了独立的RS-485支持补丁，针对同时需要虚拟串口和RS-485功能的客户。

2024.08.21
添加了RS-485半双工通信支持，设备树里面的"rs485-gpio"属性控制。
虚拟串口设备动态申请，设备树里面可以开启多个虚拟串口设备，但是同时打开的不超过2个。
解决了小核RX STOP或DEVICE CLOSE时序问题。

2024.07.26
添加了6.25Mbps和1.5625Mbps波特率支持（Error Rate = 0）。
修改了1.5Mbps波特率配置。
添加了各个波特率Error Rate信息到sh-vsci.h。
VSCI device数量取决于MHU设备树定义，不是用宏定义固定配置。
RX/TX/CMD MHU通道定义在MHU设备树节点里面。
其他代码优化。

2024.06.26
解决了无限长度数据包接收问题。

2024.06.25
代码优化。
VSCI_BUF_SIZE扩展到1024字节。
共享内存扩展到4KB。
增加了3.125Mbps波特率支持。
支持了所有SCI SCIF设备。
CM33 NS Vector Table base修改到0x0001F800。
 - fatload command需要按照上述内容修改

2024.06.20
解决了256字节数据块接收问题。

2024.06.04
第一版。
