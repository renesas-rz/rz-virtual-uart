// SPDX-License-Identifier: GPL-2.0
/*
 *	Renesas RZ MPU MHU driver
 *
 *	Copyright (C) 2024 Gary Yin
 *
 */
#undef DEBUG

#include <linux/clk.h>
#include <linux/console.h>
#include <linux/ctype.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/ktime.h>
#include <linux/major.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/scatterlist.h>
#include <linux/serial.h>
#include <linux/serial_sci.h>
#include <linux/sh_dma.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysrq.h>
#include <linux/timer.h>
#include <linux/version.h>

#include "mhu.h"

#define MHU_PORT_NUM_MAX	8 /* maximum MHU ports supported */
#define MHU_INTR_COUNT		(MHU_PORT_NUM_MAX * 2)

int rz_subcore_boot(void __iomem *sysc_base, uint32_t s_addr, uint32_t ns_addr);

/*
	Refer to 'R_MHU_NS_Open()' of e2studio code.
	For MSG & RSP
*/
struct mhu_channel_msg {
	uint32_t rx;
	uint32_t tx;
};

union mhu_channel_info {
	struct {
		uint16_t channel; /* channel index */
		uint16_t type; /* 0: MSG, 1: RSP */
	}c;

	uint32_t info;
};

struct mhu_info {
	struct device *dev;

	uint32_t port_count; /* maximum supported MHU ports */
	int port_used[MHU_PORT_NUM_MAX]; /* UART app opened ports */
	struct mhu_port *port[MHU_PORT_NUM_MAX]; /* MHU ports */

	resource_size_t reg_base;
	resource_size_t reg_size;
	void *reg_mapped;

	resource_size_t shm_base;
	resource_size_t shm_size; /* shared memory block size */
	void *shm_mapped;
	uint32_t comm_shm_size; /* shared memory size for data communication */

	uint32_t shm_rtos_base; /* RTOS's view of SHMEM base, 32bit PA */

	struct {
		int irq[MHU_INTR_COUNT];
		const char *irqname[MHU_INTR_COUNT];
	}intr;
};

struct subcore {
	struct device *dev;
	const char *fw_path;
	uint32_t s_addr, ns_addr; /* subcore's vector addr */
	uint32_t sv_area[2], sc_area[2]; /* offset, size */
	uint32_t nv_area[2], nc_area[2]; /* offset, size */
	void __iomem *firmware, *sysc;
	int loaded;
};

typedef irqreturn_t (* mhu_irqfn_t)(int irq, void *arg);

static struct subcore rzsubcore = {};
static struct mhu_info mhui = {};

static __inline uint32_t mhu_readl(uint32_t *reg)
{
	return ioread32((const volatile void __iomem *)reg);
}

static __inline void mhu_writel(uint32_t value, uint32_t *reg)
{
	iowrite32(value, (volatile void __iomem *)reg);
}

static __inline struct mhu_channel *get_mhu_channel(int channel, int type)
{
	struct mhu_channel *mc;

	mc = (struct mhu_channel *)mhui.reg_mapped;

	return &mc[channel * 2 + type];
}

static __inline struct mhu_channel_msg *get_msg_channel(int channel)
{
	struct mhu_channel_msg *msg;

	msg = (struct mhu_channel_msg *)mhui.shm_mapped;

	return &msg[channel];
}

/* MHU MSG & RSP shared */
static __inline uint32_t clear_mhu_msg_status(struct mhu_channel *mch)
{
	uint32_t status = mhu_readl(&mch->status);
	
	if(status)
		mhu_writel(1, &mch->clear);

	return status;
}

static irqreturn_t mhu_rx_intr(int irq, void *arg)
{
	size_t paddr = *(size_t *)arg;
	struct mhu_port *mp = (struct mhu_port *)paddr;
	uint32_t msg;

	if(clear_mhu_msg_status(mp->mch_irq_rx)) {
		msg = *mp->msg_irq_rx;
		mp->rxfn(msg, arg);
	} else {
		/* mhu_ch->status may not set yet */
	}

	return IRQ_HANDLED;
}

static irqreturn_t mhu_tx_intr(int irq, void *arg)
{
	size_t addr = *(size_t *)arg;
	struct mhu_port *mp = (struct mhu_port *)addr;
	uint32_t msg;

	if(clear_mhu_msg_status(mp->mch_irq_tx)) {
		msg = *mp->msg_irq_tx;
		mp->txfn(msg, arg);
	} else {
		/* mhu_ch->status may not set yet */
	}

	return IRQ_HANDLED;
}

static int mhu_request_irq(struct mhu_port *mp)
{
	struct mhu_info *mi = &mhui;
	struct device *dev = mi->dev;
	int ret;

	ret = request_irq(mp->irq_rx, mhu_rx_intr, 0, mp->irqr_name, mp->arg);
	if(ret){
		dev_err(dev, "%s: IRQ request for %s port %d  fail\n", __func__, mp->irqr_name, mp->port);
		return -EIO;
	}

	ret = request_irq(mp->irq_tx, mhu_tx_intr, 0, mp->irqt_name, mp->arg);
	if(ret){
		dev_err(dev, "%s: IRQ request for %s port %d fail\n", __func__, mp->irqt_name, mp->port);
		free_irq(mp->irq_rx, mp->arg);
		return -EIO;
	}

	return 0;
}

static void mhu_free_irq(struct mhu_port *mp)
 {
	free_irq(mp->irq_tx, mp->arg);
 
	free_irq(mp->irq_rx, mp->arg);
 }

static int firmware_load(const char *name, char *mem, uint32_t offset, uint32_t size)
{
	char *pname;
	struct file *flip;
	loff_t offt = 0;
	struct subcore *sc = &rzsubcore;
	struct device *dev = sc->dev;

	pname = (char *)kzalloc(strlen(sc->fw_path) + strlen(name) + 4, GFP_KERNEL);

	if(NULL == pname)
		return -ENOMEM;

	strcpy(pname, sc->fw_path);
	strcat(pname, name);

	flip = filp_open(pname, O_RDONLY, 0);

	if(NULL == flip) {
		dev_err(dev, "firmware <%s> open fail\n", name);
		goto exit0;
	}

	if(kernel_read(flip, mem + offset, size, &offt) <= 0) {
		dev_err(dev, "firmware <%s> read fail\n", name);
		goto exit1;
	}

	filp_close(flip, NULL);
	kfree(pname);

	pr_info("subcore firmware %s loaded\n", name);

	return 0;

exit1:
	filp_close(flip, NULL);

exit0:
	kfree(pname);
	return -EIO;
}

static int mhu_subcore_init(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	uint32_t shm_phy_base, fw_phy_base;
	struct subcore *sc = &rzsubcore;

	if(of_property_read_u32(np, "subcore-s", &sc->s_addr)) {
		dev_err(dev, "property 'subcore-s' read fail\n");
		return -EINVAL;
	}

	if(of_property_read_u32(np, "subcore-ns", &sc->ns_addr)) {
		dev_err(dev, "property 'subcore-ns' read fail\n");
		return -EINVAL;
	}

	if(of_property_read_string(np, "firmware-path", &sc->fw_path)) {
		dev_err(dev, "property 'firmware-path' read fail\n");
		return -EINVAL;
	}

	shm_phy_base = (uint32_t)mhui.shm_base;

	/* Firmware memory block */
	sc->firmware = of_iomap(np, 2);

	if(NULL == sc->firmware) {
		dev_err(dev, "firmware memory block map fail\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	fw_phy_base = (uint32_t)res->start;

	/* SYSC reg area */
	sc->sysc = of_iomap(np, 3);

	if(NULL == sc->sysc) {
		dev_err(dev, "SYSC reg area map fail\n");
		goto exit0;
	}

	if(of_property_read_u32_array(np, "firmware-sv", sc->sv_area, 2)) {
		dev_err(dev, "property 'firmware-sv' read fail\n");
		goto exit1;
	}

	if(of_property_read_u32_array(np, "firmware-sc", sc->sc_area, 2)) {
		dev_err(dev, "property 'firmware-sc' read fail\n");
		goto exit1;
	}

	if(of_property_read_u32_array(np, "firmware-nv", sc->nv_area, 2)) {
		dev_err(dev, "property 'firmware-nv' read fail\n");
		goto exit1;
	}

	if(of_property_read_u32_array(np, "firmware-nc", sc->nc_area, 2)) {
		dev_err(dev, "property 'firmware-nc' read fail\n");
		goto exit1;
	}

	sc->sv_area[0] -= shm_phy_base;
	sc->sc_area[0] -= shm_phy_base;
	sc->nv_area[0] -= fw_phy_base;
	sc->nc_area[0] -= fw_phy_base;
	sc->dev = dev;
	sc->loaded = 0;

	return 0;

exit1:
	iounmap(sc->sysc);

exit0:
	iounmap(sc->firmware);

	return -EIO;

}

static int mhu_subcore_boot(void)
{
	struct subcore *sc = &rzsubcore;

	if(sc->loaded)
		return 0;

	/* secure-vector locates in shared memory block */
	if(firmware_load("vuart-sv.bin", (char *)mhui.shm_mapped, sc->sv_area[0], sc->sv_area[1]))
		goto exit0;

	/* secure-code locates in shared memory block */
	if(firmware_load("vuart-sc.bin", (char *)mhui.shm_mapped, sc->sc_area[0], sc->sc_area[1]))
		goto exit0;

	/* non-secure-vector locates in firmware memory block */
	if(firmware_load("vuart-nv.bin", (char *)sc->firmware, sc->nv_area[0], sc->nv_area[1]))
		goto exit0;

	/* non-secure-code locates in firmware memory block */
	if(firmware_load("vuart-nc.bin", (char *)sc->firmware, sc->nc_area[0], sc->nc_area[1]))
		goto exit0;

	if(rz_subcore_boot(sc->sysc, sc->s_addr, sc->ns_addr))
		goto exit0;

	iounmap(sc->sysc);
	iounmap(sc->firmware);
	sc->loaded = 1;

	mdelay(10); // wait for subcore ready state

	pr_info("subcore boot up successfully[%08X:%08X]\n", sc->s_addr, sc->ns_addr);

	return 0;

exit0:
	iounmap(sc->sysc);
	iounmap(sc->firmware);

	return -EIO;
}

void mhu_get_shm_base(size_t *pa, size_t *va, uint32_t *rtos_pa)
{
	struct mhu_info *mi = &mhui;

	if(pa)
		*pa = mi->shm_base;

	if(va)
		*va = (size_t)mi->shm_mapped;

	if(rtos_pa)
		*rtos_pa = mi->shm_rtos_base;
}
EXPORT_SYMBOL_GPL(mhu_get_shm_base);

uint32_t mhu_get_shm_size(void)
{
	struct mhu_info *mi = &mhui;

	return mi->comm_shm_size;	
}
EXPORT_SYMBOL_GPL(mhu_get_shm_size);

int mhu_send_msg(struct mhu_port *mp, uint32_t msg)
{
#define uDLY_CNT		50
	int c = uDLY_CNT;
	struct device *dev = mhui.dev;
	struct mhu_channel *mch = mp->mch_cmd_send;

	/* fill msg for little core */
	*mp->msg_cmd_send = msg;

	/* trigger little core interrupt */
	mhu_writel(1, &mch->set);

	/* polling */
	//	asm volatile ("isb");
	udelay(1);

	while(mhu_readl(&mch->status) && c) {
		udelay(1);
		c--;
	}

	if(!c) {
		dev_err(dev, "mhu msg status polling timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mhu_send_msg);

static DEFINE_MUTEX(mhu_port_lock);

int mhu_alloc_port(size_t *mport, int (*rxfn)(uint32_t, void *), int (*txfn)(uint32_t, void *))
{
	int c;
	struct mhu_info *mi = &mhui;
	struct device *dev = mi->dev;
	struct mhu_port *mp;

	mutex_lock(&mhu_port_lock);
	for(c = 0; c < mi->port_count; c++) {
		if(0 == mi->port_used[c]) {
			if(mhu_subcore_boot()) {
				mutex_unlock(&mhu_port_lock);
				return -EIO;
			}

			mi->port_used[c] = 1;
			break;
		}
	}
	mutex_unlock(&mhu_port_lock);

	if(c == mi->port_count) {
		dev_err(dev, "no more MHU port available, used %d port(s) in total\n", c);
		return -ERANGE;
	}

	mp = mi->port[c];
	mp->rxfn = rxfn;
	mp->txfn = txfn;
	mp->arg = (void *)mport;

	*mport = (size_t)mp;

	if(mhu_request_irq(mp)) {
		mi->port_used[c] = 0;
		return -ENODEV;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mhu_alloc_port);

void mhu_free_port(struct mhu_port *mp)
{
	struct mhu_info *mi = &mhui;

	mi->port_used[mp->port] = 0;

	mhu_free_irq(mp);
}
EXPORT_SYMBOL_GPL(mhu_free_port);

static int mhu_init_port(int num)
{
	struct mhu_info *mi = &mhui;
	struct device *dev = mi->dev;
	struct device_node *dn = dev->of_node;
	uint32_t arr[4] = {0};
	union mhu_channel_info mci[3];
	char pn[16];
	struct mhu_port *mp = mi->port[num];

	mp->port = num;

	sprintf(pn, "port-%d", num);

	if(of_property_read_u32_array(dn, pn, arr, 3)) {
		dev_err(dev, "MHU %s config read fail for port %d\n", pn, num);
		return -EINVAL;
	}

	mci[0].info = arr[0];
	mci[1].info = arr[1];
	mci[2].info = arr[2];

	mp->mch_irq_rx = get_mhu_channel(mci[0].c.channel, mci[0].c.type);
	mp->mch_irq_tx = get_mhu_channel(mci[1].c.channel, mci[1].c.type);
	mp->mch_cmd_send = get_mhu_channel(mci[2].c.channel, mci[2].c.type);

	mp->msg_irq_rx = &get_msg_channel(mci[0].c.channel)->rx;
	mp->msg_irq_tx = &get_msg_channel(mci[1].c.channel)->rx;
	mp->msg_cmd_send = &get_msg_channel(mci[2].c.channel)->tx;

	num *= 2;
	mp->irq_rx = mi->intr.irq[num];
	mp->irq_tx = mi->intr.irq[num + 1];
	mp->irqr_name = mi->intr.irqname[num];
	mp->irqt_name = mi->intr.irqname[num + 1];

	return 0;
}

static int mhu_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct device_node *dn = pdev->dev.of_node;
	struct mhu_info *mi = &mhui;
	int irq, i;

	mi->dev = dev;

	/* Map MHU register area using managed API */
	mi->reg_mapped = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mi->reg_mapped)) {
		dev_err(dev, "Failed to map MHU register area\n");
		return PTR_ERR(mi->reg_mapped);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mi->reg_base = res->start;
	mi->reg_size = resource_size(res);

	/* Map SHMEM area using managed API */
	mi->shm_mapped = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(mi->shm_mapped)) {
		dev_err(dev, "Failed to map MHU shared memory area\n");
		return PTR_ERR(mi->shm_mapped);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	mi->shm_base = res->start;
	mi->shm_size = resource_size(res);

	/* SHMEM base of RTOS */
	if(of_property_read_u32(dn, "shm-rtos-base", &mi->shm_rtos_base)) {
		dev_err(dev, "dts 'shm-rtos-base' read fail\n");
		return -EINVAL;
	}

	if(of_property_read_u32(dn, "comm-shm-size", &mi->comm_shm_size)) {
		dev_err(dev, "dts 'comm-shm-size' read fail\n");
		return -EINVAL;
	}

	/* Prepare RTOS boot */
	if(mhu_subcore_init(pdev)) {
		dev_err(dev, "subcore boot init fail\n");
		return -ENODEV;
	}

	/*
	 * MHU IRQ information
	 * Kernel 6.1+ requires reading interrupt names from device tree directly
	 * using of_property_read_string_index() as platform_get_resource()
	 * no longer populates res->name for interrupts
	 */
	for(i = 0; i < MHU_INTR_COUNT; i++) {
		const char *irq_name;
		
		irq = platform_get_irq_optional(pdev, i);
		if(irq <= 0) {
			/* End of interrupt list */
			mi->port_count = i >> 1;
			break;
		}
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
		/* Kernel 6.x: Read interrupt name from device tree */
		if(of_property_read_string_index(dn, "interrupt-names", i, &irq_name)) {
			dev_err(dev, "No interrupt-names[%d] in device tree\n", i);
			return -EINVAL;
		}
	#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
		/* Kernel 5.10-5.x: Use platform_get_resource for IRQ names */
		res = platform_get_resource(pdev, IORESOURCE_IRQ, i);
		if(!res || !res->name) {
			dev_err(dev, "Cannot get IRQ name for interrupt %d\n", i);
			return -EINVAL;
		}
		irq_name = res->name;
	#endif

		pr_info("MHU IRQ %d found, name = %s\n", irq, irq_name);

		mi->intr.irq[i] = irq;
		mi->intr.irqname[i] = irq_name;
	}

	if(0 == mi->port_count) {
		dev_err(dev, "No MHU port(s) found!\n");
		return -ENODEV;
	}

	pr_info("MHU REG base = 0x%zx, size = 0x%zx\n", (size_t)mi->reg_mapped, (size_t)mi->reg_size);

	/* MHU port allocation - use devm for automatic cleanup */
	for(i = 0; i < mi->port_count; i++) {
		mi->port[i] = devm_kzalloc(dev, sizeof(struct mhu_port), GFP_KERNEL);
		if(!mi->port[i]) {
			dev_err(dev, "MHU port mem allocation fail for port %d\n", i);
			return -ENOMEM;
		}

		if(mhu_init_port(i)) {
			dev_err(dev, "MHU port init fail for port %d\n", i);
			return -EINVAL;
		}

		mi->port_used[i] = 0;
	}

	pr_info("MHU SHM base = 0x%zx(Linux VA), 0x%zx(Linux PA)\n", (size_t)mi->shm_mapped, (size_t)mi->shm_base);
	pr_info("MHU SHM base = 0x%x(RTOS PA)\n", mi->shm_rtos_base);
	pr_info("MHU SHM size = 0x%zx\n", (size_t)mi->shm_size);	
	pr_info("MHU driver loaded, supports %d port(s) in total\n", mi->port_count);

	return 0;
}

static int __mhu_remove_internal(struct platform_device *pdev)
{
	int i;
	struct mhu_info *mi = &mhui;
	
	for(i = mi->port_count - 1; i >= 0; i--)
		kfree(mi->port[i]);
	
	iounmap(mi->shm_mapped);
	iounmap(mi->reg_mapped);
	release_mem_region(mi->shm_base, mi->shm_size);
	release_mem_region(mi->reg_base, mi->reg_size);
	
	pr_info("MHU driver removed \n");
	
	return 0;
}

/* Wrapper function with version-specific return type */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
/* Kernel 6.12+ mainline uses void return */
static void mhu_remove(struct platform_device *pdev)
{
	__mhu_remove_internal(pdev);
}

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
/* Kernel 5.10 and linux cip 6.1 uses int return */
static int mhu_remove(struct platform_device *pdev)
{
	return __mhu_remove_internal(pdev);
}

#else
#error "Unsupported kernel version for mhu_remove()"
#endif

static const char banner[] __initconst = "Renesas MHU driver initialized";

static const struct of_device_id of_mhu_match[] = {
	{
		.compatible = "renesas,mhu-r9a07g044",
	},
	{
		/* Terminator */
	},
};

MODULE_DEVICE_TABLE(of, of_mhu_match);

static struct platform_driver mhu_driver = {
	.probe		= mhu_probe,
	.remove		= mhu_remove,
	.driver		= {
		.name	= "mhu-dev",
		.of_match_table = of_match_ptr(of_mhu_match),
	},
};

static int __init mhu_init(void)
{
	pr_info("%s\n", banner);

	return platform_driver_register(&mhu_driver);
}

static void __exit mhu_exit(void)
{
	platform_driver_unregister(&mhu_driver);
}

module_init(mhu_init);
module_exit(mhu_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mhu");
MODULE_AUTHOR("Gary");
MODULE_DESCRIPTION("RZ MPU MHU driver");

