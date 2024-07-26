// SPDX-License-Identifier: GPL-2.0
/*
 *	Renesas RZ/G2Lx MPU Virtual SCI/SCIF device driver.
 *
 *	Copyright (C) 2024 Gary Yin
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
#include <linux/tty.h>
#include <linux/tty_flip.h>

#ifdef CONFIG_SUPERH
#include <asm/sh_bios.h>
#include <asm/platform_early.h>
#endif

#include "sh-vsci.h"
#include "mhu.h"


uint32_t vsci_baud_enc(int baud)
{
	switch(baud) {
		BAUD_OP(BRE);
		default:
			pr_info("## error: unsupported baudrate %d found, no matched vsci-baud, BR9600 is used\n", baud);
			return BR9600;
	}
}

int vsci_send_cmd(struct vsci_device *vd, uint32_t cmd)
{
	struct mhu_port *mp = (struct mhu_port *)vd->mp;

	return mhu_send_msg(mp, cmd);
}

struct vsci_device *vsci_alloc_device(struct device *devp, void *sciport, int port_type, int port_num, vsci_cb rxfn, vsci_cb txfn)
{
	int vscin;
	struct vsci_device *vd;
	struct device *dev = (struct device *)devp;
	struct shared_mem_info *smi;
	struct mhu_port *mp;
	size_t va, pa, offset;
	uint32_t rtos_base;

	vd = (struct vsci_device *)kzalloc(sizeof(struct vsci_device), GFP_KERNEL);

	if(NULL == vd) {
		dev_err(dev, "no memory for VSCI device allocation\n");
		goto exit0;
	}

	mp = mhu_alloc_port();

	if(NULL == mp)
		goto exit1;

	if(PORT_VSCI == port_type) {
		vscin = DEV_VSCI0 + port_num;

		if(vscin >= DEV_VSCI_MAX) {
			dev_err(dev, "device num %d is invalid for SCI device\n", port_num);
			goto exit2;
		}
	} else if(PORT_VSCIF == port_type) {
		vscin = DEV_VSCIF0 + port_num;

		if(vscin >= DEV_VSCIF_MAX) {
			dev_err(dev, "device num %d is invalid for SCIF device\n", port_num);
			goto exit2;
		}
	} else {
		dev_err(dev, "invalid device type %d was found\n", port_type);
		goto exit2;
	}

	vd->mp = (size_t)mp;

	if(-1 == mhu_get_shm_base(&pa, &va, &rtos_base))
		goto exit2;

	smi = (struct shared_mem_info *)pa;
	vd->vc_base = (size_t)&smi->vc[mp->port];

	smi = (struct shared_mem_info *)va;
	vd->vc = &smi->vc[mp->port];

	/*
		install RX/TX circ buffer pointers(Linux, RTOS)
	*/
	offset = offsetof(struct shared_mem_info, circ_buffer);

	va += offset;
	rtos_base += (uint32_t)offset;

	vd->vc->bcore.rbuf = (uint64_t)va + (mp->port * VSCI_BUF_SIZE * 2);
	vd->vc->bcore.tbuf = vd->vc->bcore.rbuf + VSCI_BUF_SIZE;

	vd->vc->lcore.rbuf = rtos_base + (mp->port * VSCI_BUF_SIZE * 2);
	vd->vc->lcore.tbuf = vd->vc->lcore.rbuf + VSCI_BUF_SIZE;

	if(-1 == mhu_request_irq(&vd->mp, rxfn, txfn))
		goto exit2;

	vd->devname = vscin;
	vd->platdev = devp;
	vd->sciport = sciport;
	
	return vd;

exit2:
	mhu_free_port(mp);

exit1:
	kfree(vd);

exit0:
	return NULL;
}

void vsci_free_device(struct vsci_device *vd)
{
	struct mhu_port *mp = (struct mhu_port *)vd->mp;

	mhu_free_irq(mp);

	mhu_free_port(mp);

	kfree(vd);
}

