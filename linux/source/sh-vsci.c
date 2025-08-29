// SPDX-License-Identifier: GPL-2.0
/*
 *	Renesas RZ MPU Virtual SCI/SCIF device driver.
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


int vsci_baud_adjust(struct vsci_device *vd, int baud)
{
#if 0
	uint32_t vcmd;

	switch(baud) {
		BAUD_OP(BRA);
		default:
			pr_info("## error[%s]: unsupported baudrate %d found, no matched vsci-baud, BR9600 is used\n", __func__, baud);
			vcmd = BAUD_ADJUST_9600();
			break;
	}
	
	vsci_send_cmd(vd, vcmd);
#endif

	return 0;
}

enum vsci_br vsci_baud_enc(int baud)
{
	switch(baud) {
		BAUD_OP(BRE);
		default:
			pr_info("## error[%s]: unsupported baudrate %d found, no matched vsci-baud, BR9600 is used\n", __func__, baud);
			return BR9600;
	}
}

int vsci_send_cmd(struct vsci_device *vd, uint32_t cmd)
{
	struct mhu_port *mp = (struct mhu_port *)vd->mp;

	return mhu_send_msg(mp, cmd);
}

/*
	generate a PA to fill the uart_port->mapbase.
*/
size_t vsci_get_mapbase(int port_type, int port_num)
{
	size_t pa;
	int b = IS_VSCIG_PORT(port_type) ? DEV_VSCIG0 : DEV_VSCIF0;
	size_t offset = offsetof(struct shared_mem_info, reserve);

	mhu_get_shm_base(&pa, NULL, NULL);

	return pa + offset + 0x20 * (b + port_num);
}

int vsci_alloc_device(struct device *devp, struct vsci_device *vd, void *sciport, int port_type, int port_num, vsci_cb rxfn, vsci_cb txfn)
{
	int devname;
	struct device *dev = (struct device *)devp;
	struct shared_mem_info *smi;
	struct mhu_port *mp;
	size_t va, offset;

	if(PORT_VSCIG == port_type) {
		devname = DEV_VSCIG0 + port_num;

		if(devname >= DEV_VSCIG_MAX) {
			dev_err(dev, "device num %d is invalid for SCIG device\n", port_num);
			goto exit0;
		}
	} else if(PORT_VSCIF == port_type) {
		devname = DEV_VSCIF0 + port_num;

		if(devname >= DEV_VSCIF_MAX) {
			dev_err(dev, "device num %d is invalid for SCIF device\n", port_num);
			goto exit0;
		}
	} else {
		dev_err(dev, "invalid device type %d was found\n", port_type);
		goto exit0;
	}

	if(-1 == mhu_alloc_port(vd, rxfn, txfn))
		goto exit0;

	mp = (struct mhu_port *)vd->mp;

	mhu_get_shm_base(NULL, &va, NULL);

	smi = (struct shared_mem_info *)va;

	/*
		install RX/TX circ buffer pointers(Linux, RTOS)
	*/
	offset = offsetof(struct shared_mem_info, circ_buffer);

	va += offset;

	if((offset + (mp->port + 1) * VSCI_BUF_SIZE * 2) > mhu_get_shm_size()) {
		dev_err(dev, "Can not allocate enough shared memory\n");
		goto exit0;
	}

	vd->devname = devname;
	vd->platdev = devp;
	vd->sciport = sciport;

	vd->rbuf = (char *)va + (mp->port * VSCI_BUF_SIZE * 2);
	vd->tbuf = vd->rbuf + VSCI_BUF_SIZE;
	
	return 0;

exit0:
	return -1;
}

void vsci_free_device(struct vsci_device *vd)
{
	mhu_free_port(vd);
}

