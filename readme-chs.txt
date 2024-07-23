 --- RZ/G2L基于CM33小核的虚拟串口方案 ---

基础说明：
软件基于瑞萨官方VLP调试通过，硬件平台是瑞萨RZ/G2L SMARC开发板（CPU是双核A55）。
仅供客户参考，验证需求，是否能满足客户需要，需要客户结合自己实际硬件软件实测，不保证没有bug。
如果客户发现了bug，请联系瑞萨FAE支持。

支持接口/特性：
 - SCI0(P40_0 & P40_1, 1.8v)
 - SCIF2(P48_0 & P48_1, 3.3v)
 - SCI0可以支持到1Mbps，8bit or 9bit data，不支持校验位
 - SCIF2可以支持到10Mbps，8bit data，不支持校验位
 - 支持的波特率，参考sh-vsci.h vsci_br（这个enum定义是给虚拟串口驱动程序和CM33固件使用的，Linux应用程序不要引用）

注意：
 - 此方案，跟OpenAMP没有关系，默认的128MB保留DDR内存可以减少到2MB，参考wiki：
  “Reduce reserved area for RZ/G2L SMARC board”，https://renesas.info/wiki/RZ-G/RZ-G2_BSP_MemoryMap
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
    ... ...
    soc:serial@0000: ttySC1 at MMIO 0x20100 (irq = 0, base_baud = 0) is a vsci
    soc:serial@0002: ttySC3 at MMIO 0x20118 (irq = 0, base_baud = 0) is a vscif
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
  Image：通过了测试的kernel镜像
  r9a07g044l2-smarc.dtb：kernel对应的设备树
  rzg2_initramfs.cpio.gz：ramdisk，可选，客户可以使用其他rootfs
    - bootargs="initrd=0x70000000,32M'，首先加载到0x70000000处
  CM33固件：
    vuart_nc.bin：CM33 non-secure code
    vuart_nv.bin：CM33 non-secure vector
    vuart_sc.bin：CM33 secure code
    vuart_sv.bin：CM33 secure vector
linux/source：
  复制所有文件到kernel/drivers/tty/serial/
linux：
  rzg2l-vlp306-vuart-vXXX.diff：VLP3.0.6 CIP41 kernel patch
    - 首先kernel源码目录运行make defconfig，生成初始配置。
      打了这个补丁之后，需要首先进入kernel menuconfig界面，确保选中下面两项(*)：
        Device Drivers > Character devices > Serial drivers > Message Handling Unit support
        Device Drivers > Character devices > Serial drivers > SuperH SCI(F) serial port support
      编译生成新的kernel和设备树的镜像文件。
    - 客户使用基于VLP Linux kernel修改的kernel，也可以参考。
u-boot：
  u-boot.diff：u-boot下支持cm33命令的补丁
  cm33/cm33.c：u-boot下需要添加的代码，复制到u-boot/cm33目录

------ HISTORY ------
2024.06.26
解决了无限长度数据包接收问题

2024.06.25
代码优化
VSCI_BUF_SIZE扩展到1024字节
共享内存扩展到4KB
增加了3.125Mbps波特率支持
支持了所有SCI SCIF设备
CM33 NS Vector Table base修改到0x0001F800
 - fatload command需要按照上述内容修改

2024.06.20
解决了256字节数据块接收问题

2024.06.04
第一版
