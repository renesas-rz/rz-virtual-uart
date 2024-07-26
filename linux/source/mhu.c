// SPDX-License-Identifier: GPL-2.0
/*
 *	Renesas RZ/G2Lx MPU MHU driver
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

#include "sh-vsci.h"
#include "mhu.h"


#define MHU_PORT_NUM_MAX	VSCI_DEVICE_NUM_MAX
#define INTR_COUNT			(MHU_PORT_NUM_MAX * 2)


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
	struct device dev;

	int ref_count;
	uint32_t max_mhu_port; /* fixed */
	
	resource_size_t reg_base;
	resource_size_t reg_size;
	void *reg_mapped;
	
	resource_size_t shm_base;
	resource_size_t shm_size;
	void *shm_mapped;
	uint32_t shm_rtos_base; /* RTOS's view of SHMEM base, 32bit PA */
	
	struct {
		int irq[INTR_COUNT];
		const char *irqname[INTR_COUNT];
	}intr;
};

typedef irqreturn_t (* mhu_irqfn_t)(int irq, void *arg);

static struct mhu_info mhui = {};

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
	uint32_t status = mch->status;
	
	if(status)
		mch->clear = 1;

	return status;
}

static irqreturn_t mhu_intr(int irq, void *arg)
{
	size_t paddr = *(size_t *)arg;
	struct mhu_port *mp = (struct mhu_port *)paddr;
	struct device *dev = &mhui.dev;
	uint32_t msg;
	int port = mp->port;

	if(clear_mhu_msg_status(mp->mch_irq_rx)) {
		msg = *mp->msg_irq_rx;
		mp->rxfn(msg, arg);
	} else if(clear_mhu_msg_status(mp->mch_irq_tx)) {
		msg = *mp->msg_irq_tx;
		mp->txfn(msg, arg);
	} else {
		dev_err(dev, "%s: unhandled MHU IRQ %d for port %d\n", __func__, irq, port);
		goto exit0;
	}

	return IRQ_HANDLED;

exit0:
	return IRQ_NONE;
}

int mhu_get_shm_base(size_t *pa, size_t *va, uint32_t *rtos_pa)
{
	struct mhu_info *mi = &mhui;

	*pa = mi->shm_base;
	*va = (size_t)mi->shm_mapped;
	*rtos_pa = mi->shm_rtos_base;

	return 0;
}
EXPORT_SYMBOL_GPL(mhu_get_shm_base);

int mhu_request_irq(size_t *arg, vsci_cb rxfn, vsci_cb txfn)
{
	size_t paddr = *arg;
	struct mhu_info *mi = &mhui;
	struct mhu_port *mp = (struct mhu_port *)paddr;
	struct device *dev = &mi->dev;

	mp->rxfn = rxfn;
	mp->txfn = txfn;

	if(request_irq(mp->irq_rx, mhu_intr, 0, mp->irqr_name, (void *)arg)) {
		dev_err(dev, "%s: IRQ request for %s fail\n", __func__, mp->irqr_name);
		goto exit0;
	}

	if(request_irq(mp->irq_tx, mhu_intr, 0, mp->irqt_name, (void *)arg)) {
		dev_err(dev, "%s: IRQ request for %s fail\n", __func__, mp->irqt_name);
		goto exit1;
	}

	return 0;

exit1:
	free_irq(mp->irq_rx, NULL);

exit0:
	return -1;
}
EXPORT_SYMBOL_GPL(mhu_request_irq);

 void mhu_free_irq(struct mhu_port *mp)
 {
	free_irq(mp->irq_tx, NULL);
 
	free_irq(mp->irq_rx, NULL);
 }
 EXPORT_SYMBOL_GPL(mhu_free_irq);

 int mhu_send_msg(struct mhu_port *mp, uint32_t msg)
{
#define uDLY_CNT		50
	int c = uDLY_CNT;
	struct device *dev = &mhui.dev;
	struct mhu_channel *mch = mp->mch_cmd_send;

	/* fill msg for little core */
	*mp->msg_cmd_send = msg;

	/* trigger little core interrupt */
	mch->set = 1;

	/* polling */
	//	asm volatile ("isb");
	udelay(1);

	while(mch->status && c) {
		udelay(1);
		c--;
	}

	if(!c) {
		dev_err(dev, "mhu msg status polling timeout\n");
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mhu_send_msg);

struct mhu_port *mhu_alloc_port(void)
{
	int c;
	struct mhu_info *mi = &mhui;
	struct device *dev = &mi->dev;
	struct device_node *dn = dev->of_node;
	struct mhu_port *mp;
	uint32_t arr[4] = {0};
	union mhu_channel_info mci[3];
	char pn[16];

	if(mi->ref_count >= mi->max_mhu_port) {
		dev_err(dev, "no more MHU port available\n");
		goto exit0;
	}

	mp = (struct mhu_port *)kzalloc(sizeof(struct mhu_port), GFP_KERNEL);

	if(NULL == mp) {
		dev_err(dev, "no mem for MHU port creation\n");
		goto exit0;
	}

	c = mi->ref_count++;
	
	mp->port = c;

	/*
		The RX INTR, TX INTR, and CMD MHU channels are defined in the device tree.
	
		device tree definition:
		1) Linux INTR(L-CORE -> B-CORE):
			MSG, RTOS RX REQ -> Linux CORE0 --- For MHU port 0
			RSP, RTOS TX REQ -> Linux CORE0 --- For MHU port 0

			MSG, RTOS RX REQ -> Linux CORE1 --- For MHU port 1
			RSP, RTOS TX REQ -> Linux CORE1 --- For MHU port 1

			... ...
			... ...
		2) RTOS INTR(B-CORE -> L-CORE, share the RSP channel):
			MSG, Linux CORE0 CMD -> RTOS --- For MHU port 0
			MSG, Linux CORE1 CMD -> RTOS --- For MHU port 1
			... ...

		RTOS VSCI device 0 -- MHU port 0 (Binding)
		RTOS VSCI device 1 -- MHU port 1 (Binding)
	*/
	sprintf(pn, "port-%d", c);

	if(of_property_read_u32_array(dn, pn, arr, 3)) {
		dev_err(dev, "MHU port config read fail\n");
		goto exit1;
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

	c *= 2;
	mp->irq_rx = mi->intr.irq[c];
	mp->irq_tx = mi->intr.irq[c + 1];
	mp->irqr_name = mi->intr.irqname[c];
	mp->irqt_name = mi->intr.irqname[c + 1];

	return mp;

exit1:
	kfree(mp);

exit0:
	return NULL;
}
EXPORT_SYMBOL_GPL(mhu_alloc_port);

void mhu_free_port(struct mhu_port *mp)
{
	struct mhu_info *mi = &mhui;

	kfree(mp);

	mi->ref_count--;
}
EXPORT_SYMBOL_GPL(mhu_free_port);

static int mhu_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct device_node *dn = pdev->dev.of_node;
	struct mhu_info *mi = &mhui;
	int irq, i;
	void *mem;

	mi->dev = *dev;

	/* MHU register area */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if(NULL == res) {
		dev_err(dev, "Can not find IORESOURCE_MEM 0\n");
		goto exit0;
	}

	if(NULL == request_mem_region(res->start, resource_size(res), dev_name(dev))) {
		dev_err(dev, "MHU register region request fail\n");
		goto exit0;
	}

	mi->reg_base = res->start;
	mi->reg_size = resource_size(res);

	/* SHMEM area, MHU msg/rsp shmem + vUART CTRL struct + vUART circ buffer */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);

	if(NULL == res) {
		dev_err(dev, "Can not find IORESOURCE_MEM 1\n");
		goto exit1;
	}

	if(NULL == request_mem_region(res->start, resource_size(res), dev_name(dev))) {
		dev_err(dev, "MHU register region request fail\n");
		goto exit1;
	}

	mi->shm_base = res->start;
	mi->shm_size = resource_size(res);

	/* SHMEM base of RTOS */
	if(of_property_read_u32(dn, "shm-rtos-base", &mi->shm_rtos_base)) {
		dev_err(dev, "dts 'shm-rtos-base' read fail\n");
		goto exit2;
	}

	/* MHU IRQ information */
	for(i = 0; ; i++) {
		irq = platform_get_irq_optional(pdev, i);

		if(irq <= 0) {
			mi->max_mhu_port = i >> 1;
			break;
		}

		if(i >= INTR_COUNT) {
			dev_err(dev, "MHU interrupt resources %d exceed %d\n", irq, INTR_COUNT);
			goto exit2;
		}

		res = platform_get_resource(pdev, IORESOURCE_IRQ, i);

		pr_info("MHU resource IRQ %d found, name = %s\n", irq, res->name);

		mi->intr.irq[i] = irq;
		mi->intr.irqname[i] = res->name;
	}

	// Memory map
	mem = ioremap(mi->reg_base, mi->reg_size);

	if(NULL == mem) {
		dev_err(dev, "REG area map failed\n");
		goto exit2;
	}

	mi->reg_mapped = mem;
	pr_info("MHU REG base = 0x%zx, size = 0x%x\n", (size_t)mem, (int)mi->reg_size);

	mem = ioremap(mi->shm_base, mi->shm_size);

	if(NULL == mem) {
		dev_err(dev, "SHMEM  area map failed\n");
		goto exit3;
	}

	mi->shm_mapped = mem;
	pr_info("MHU SHM base = 0x%zx, size = 0x%x\n", (size_t)mem, (int)mi->shm_size);

	mi->ref_count = 0;
	pr_info("MHU driver loaded\n");

	return 0;

//exit4:
//	iounmap(mi->shm_mapped);

exit3:
	iounmap(mi->reg_mapped);

exit2:
	release_mem_region(mi->shm_base, mi->shm_size);

exit1:
	release_mem_region(mi->reg_base, mi->reg_size);

exit0:
	return -ENODEV;
}

static int mhu_remove(struct platform_device *pdev)
{
	struct mhu_info *mi = &mhui;

	iounmap(mi->shm_mapped);
	iounmap(mi->reg_mapped);

	release_mem_region(mi->shm_base, mi->shm_size);
	release_mem_region(mi->reg_base, mi->reg_size);
	
	return 0;
}

static const char banner[] __initconst = "RZ/G2Lx MHU driver initialized";

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
MODULE_DESCRIPTION("RZ/G2Lx MHU driver");

