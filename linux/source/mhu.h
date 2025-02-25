
#ifndef __MHU_H__
#define __MHU_H__


/* according to MHU hw register layout in UM */
struct mhu_channel {
	uint32_t status;
	uint32_t set;
	uint32_t clear;
	uint32_t reserved;
};

struct mhu_port {
	int port; /* MHU port index */
	
	int irq_rx;
	int irq_tx;
	const char *irqr_name;
	const char *irqt_name;
	vsci_cb rxfn;
	vsci_cb txfn;
	void *arg;

	struct mhu_channel *mch_irq_rx; /* MHU channel for IRQ RX */
	struct mhu_channel *mch_irq_tx; /* MHU channel for IRQ TX */
	struct mhu_channel *mch_cmd_send; /* MHU channel for cmd sending to RTOS */

	uint32_t *msg_irq_rx; /* MSG of IRQ RX */
	uint32_t *msg_irq_tx; /* MSG of IRQ TX */
	uint32_t *msg_cmd_send; /* MSG for cmd sending */
};

int mhu_alloc_port(struct vsci_device *vd, vsci_cb rxfn, vsci_cb txfn);

void mhu_free_port(struct vsci_device *vd);

void mhu_get_shm_base(size_t *pa, size_t *va, uint32_t *rtos_pa);

int mhu_send_msg(struct mhu_port *mp, uint32_t msg);
#endif

