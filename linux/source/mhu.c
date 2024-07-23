// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G2Lx MPU MHU driver
 *
 *  Copyright (C) 2024 Gary Yin
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

#define MHU_PORT_CNT	__MAX_DEV_NUM__

// according to dts mhu node
#define INTR_COUNT		4
#define MHU_CH_MAX		5


/*
	Refer to 'R_MHU_NS_Open()' of e2studio code.
	For MSG & RSP
*/
struct mhu_channel_msg {
	uint32_t rx;
	uint32_t tx;
};

struct mhu_info {
	int ref_count;
	struct device dev;
	
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

static __inline struct mhu_channel *get_mhu_msg_channel(int channel)
{
	struct mhu_channel *mc;

	mc = (struct mhu_channel *)mhui.reg_mapped;

	return &mc[channel * 2];
}

static __inline struct mhu_channel *get_mhu_rsp_channel(int channel)
{
	struct mhu_channel *mc;

	mc = (struct mhu_channel *)mhui.reg_mapped;

	return &mc[channel * 2 + 1];
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
	struct mhu_port *mport = (struct mhu_port *)paddr;
	int port = mport->port;
	struct device *dev = &mi->dev;

	if(port >= MHU_PORT_CNT) {
		dev_err(dev, "%s: invalid MHU port num %d\n", __func__, port);
		goto exit0;
	}

	if(request_irq(mport->irq_rx, mhu_intr, 0, mport->irqr_name, (void *)arg)) {
		dev_err(dev, "%s: IRQ request for %s fail\n", __func__, mport->irqr_name);
		goto exit0;
	}

	if(request_irq(mport->irq_tx, mhu_intr, 0, mport->irqt_name, (void *)arg)) {
		dev_err(dev, "%s: IRQ request for %s fail\n", __func__, mport->irqt_name);
		goto exit1;
	}

	mport->rxfn = rxfn;
	mport->txfn = txfn;

	return 0;
	
exit1:
	free_irq(mport->irq_rx, NULL);

exit0:
	return -1;
}
EXPORT_SYMBOL_GPL(mhu_request_irq);

 void mhu_free_irq(struct mhu_port *mp)
 {
	 int port = mp->port, irq;
	 const char *name;
	 struct mhu_info *mi = &mhui;
	 struct device *dev = &mi->dev;
 
	 if(port >= MHU_PORT_CNT) {
		 dev_err(dev, "%s: invalid MHU port num %d\n", __func__, port);
		 return;
	 }
 
	 irq = mp->irq_tx;
	 name = mp->irqt_name;
	 if(free_irq(irq, NULL)) {
		 dev_err(dev, "IRQ free for %s fail\n", name);
		 return;
	 }
 
	 irq = mp->irq_rx;
	 name = mp->irqr_name;
	 if(free_irq(irq, NULL)) {
		 dev_err(dev, "IRQ free for %s fail\n", name);
		 return;
	 }
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
	struct mhu_port *mp;

	if(mi->ref_count >= MHU_PORT_CNT) {
		dev_err(dev, "no more mhu port available\n");
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
		The RX, TX INTR is set according to the device tree definition & the mhu_alloc_port()'s order.
	
		device tree definition:
		1) Linux INTR:
			CH4 MSG, RTOS RX REQ -> Linux CORE0 --- For MHU port 0
			CH1 RSP, RTOS TX REQ -> Linux CORE0 --- For MHU port 0
			CH5 MSG, RTOS RX REQ -> Linux CORE1 --- For MHU port 1
			CH3 RSP, RTOS TX REQ -> Linux CORE1 --- For MHU port 1
		2) RTOS INTR:
			CH1 MSG, Linux CORE0 CMD -> RTOS --- For MHU port 0
			CH3 MSG, Linux CORE1 CMD -> RTOS --- For MHU port 1
		3) VSCI device 0 -- MHU port 0 (Binding)
		4) VSCI device 1 -- MHU port 1 (Binding)
	*/
	if(0 == mp->port) { /* port 0 */
		mp->mch_irq_rx = get_mhu_msg_channel(4);
		mp->mch_irq_tx = get_mhu_rsp_channel(1);
		mp->mch_cmd_send = get_mhu_msg_channel(1);

		mp->msg_irq_rx = &get_msg_channel(4)->rx;
		mp->msg_irq_tx = &get_msg_channel(1)->rx; /* IRQ TX is not MSG TX */
		mp->msg_cmd_send = &get_msg_channel(1)->tx;
	} else { /* port 1 */
		mp->mch_irq_rx = get_mhu_msg_channel(5);
		mp->mch_irq_tx = get_mhu_rsp_channel(3);
		mp->mch_cmd_send = get_mhu_msg_channel(3);

		mp->msg_irq_rx = &get_msg_channel(5)->rx;
		mp->msg_irq_tx = &get_msg_channel(3)->rx; /* IRQ TX is not MSG TX */
		mp->msg_cmd_send = &get_msg_channel(3)->tx;
	}

	c *= 2;
	mp->irq_rx = mi->intr.irq[c];
	mp->irq_tx = mi->intr.irq[c + 1];
	mp->irqr_name = mi->intr.irqname[c];
	mp->irqt_name = mi->intr.irqname[c + 1];

	return mp;

exit0:
	return NULL;
}
EXPORT_SYMBOL_GPL(mhu_alloc_port);

void mhu_free_port(struct mhu_port *mp)
{
	struct mhu_info *mi = &mhui;
	struct device *dev = &mi->dev;
	int port = mp->port;

	if(port >= MHU_PORT_CNT) {
		dev_err(dev, "port number %d exceeds\n", port);
		return;
	}

	if(mi->ref_count <= 0) {
		dev_err(dev, "no more mhu port to free\n");
		return;
	}

	kfree(mp);

	mi->ref_count--;
}
EXPORT_SYMBOL_GPL(mhu_free_port);

static int mhu_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
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
	if(of_property_read_u32(np, "shm-rtos-base", &mi->shm_rtos_base)) {
		dev_err(dev, "dts 'shm-rtos-base' read fail\n");
		goto exit2;
	}

	/* MHU IRQ information */
	for(i = 0; i < INTR_COUNT; i++) {
		irq = platform_get_irq(pdev, i);

		if(irq < 0) {
			dev_err(dev, "Can not find IORESOURCE_IRQ %d\n", i);
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

