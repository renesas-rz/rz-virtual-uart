
#ifndef __SH_VSCI_H__
#define __SH_VSCI_H__

/***************************************************************************
	 ------------------- NOTE ----------------------
	IF CHANGED ANYTHING OF THIS FILE, YOU NEED TO RECOMPILE
	e2studio project & Linux kernel AFTER UPDATING
	WITH THIS FILE(BOTH).
****************************************************************************/

#define VSCI_DEVICE_NUM_MAX		8 /* large enough for current MPU types */

enum vsci_name {
	DEV_VSCIG0 = 1,
	DEV_VSCIG1,
	DEV_VSCIG_MAX,
	
	DEV_VSCIF0, /* SCIF0 is often used by Linux side */
	DEV_VSCIF1,
	DEV_VSCIF2,
	DEV_VSCIF3,
	DEV_VSCIF4,
	DEV_VSCIF_MAX
};

#if defined(__linux__) || defined(__KERNEL__)
#define IS_VSCIG_PORT(p)		(PORT_VSCIG == (p))
#define IS_VSCIF_PORT(p)	(PORT_VSCIF == (p))
#endif

#define IS_VSCIG_DEV(d)		(((d) >= DEV_VSCIG0) && ((d) < DEV_VSCIG_MAX))
#define IS_VSCIF_DEV(d)		(((d) >= DEV_VSCIF0) && ((d) < DEV_VSCIF_MAX))

typedef int (* vsci_cb)(uint32_t msg, void *arg);

/*
	RX buffer size = VSCI_BUF_SIZE, must be 2^N
	TX buffer size = VSCI_BUF_SIZE, must be 2^N
	If you want to extend this macro, change e2studio fsp.ld accordingly:
		MHU_SHMEM_LENGTH, RAM_N_START
	Linux side MHU dts node also needs to be changed(shm size)
	then, recompile e2studio project, Linux kernel after update this header file.
*/
#define VSCI_BUF_SIZE		1024

union rbuffer {
	char c[VSCI_BUF_SIZE];
	short w[VSCI_BUF_SIZE / 2];
};

struct shared_mem_info {
	uint32_t msg_buf[VSCI_DEVICE_NUM_MAX * 2]; /* Linux-RTOS MSG memory */
	uint32_t reserve[256];
	uint64_t circ_buffer[];
};

#if defined(__linux__) || defined(__KERNEL__)
struct vsci_device {
	int devname; /* vsci device name, DEV_VSCIxxx */

	struct device *platdev;

	void *sciport;
	
	char *rbuf;
	char *tbuf;

	size_t mp; /* struct mhu_port pointer */
};
#endif

enum vsci_cmd_set {

	/*
		--- Linux -> RTOS command list ---
	*/
	VSCIC_OPEN = 1,
	VSCIC_BAUD_ADJUST,
	VSCIC_CONF,
	VSCIC_START,
	VSCIC_TXD_RDY,
	VSCIC_STOP,
	VSCIC_CLOSE,

	/*
		--- RTOS -> Linux request list ---
	*/
	VSCIR_RX_RDY, /* issue when recv data count >= (RX-CIRC-BUF / 2) size, with count */
	VSCIR_RXD_RDY, /* issue when recv data count < (RX-CIRC-BUF / 2) size, with count, recv data timeout(DRI), DRI interval = GTM interval */
	VSCIR_TX_END /* issue when the last bit of last byte in TX-CIRC-BUF was sent out(TEI) */
};

enum vsci_br {
	/*
	  * BR50 & BR75 are not supported due to 100M PCLK dividing limitation.
	  */

	 /* baudrate, error rate */
	BR110 = 1,	/* 0.0651485% */
	BR134,		/* 0.0223124% */
	BR150,		/* 0.0194995% */
	BR200,		/* 0.0194995% */
	BR300,		/* 0.0194995% */
	BR600,		/* 0.0194995% */
	BR1200,		/* 0.0194995% */
	BR1800,		/* 0.0060041% */
	BR2400,		/* 0.0194995% */
	BR4800,		/* 0.0194995% */
	BR9600,		/* 0.0194995% */
	BR19200,		/* 0.0194995% */
	BR38400,		/* 0.0194995% */
	BR57600,		/* 0.00335015% */
	BR115200,		/* 0.00335015% */
	BR230400,		/* 0.00335015% */
	BR460800,		/* 0.00335015% */
	BR500000,		/* 0.0976562% */
	BR576000,		/* 0.135807% */
	BR921600,		/* 0.00335015% */
	BR1000000,		/* 0.0976562% */
	BR1152000,		/* 0.135807% */
	BR1500000,		/* 0.0976583% */
	BR1562500,		/* [ZERO] 0.000000% */
	BR2000000,		/* 0.0976562% */
	BR2500000,		/* 0.09766% */
	BR3000000,		/* 0.0976583% */
	BR3125000,		/* [ZERO] 0.000000% */
	BR3500000,		/* 0.446429% */
	BR4000000,		/* 0.0976562% */
	BR5000000,		/* 0.09766% */
	BR6000000,		/* 0.0976583% */
	BR6250000,		/* [ZERO] 0.000000% */
	BR7000000,		/* 0.446429% */
	BR8000000,		/* 0.0976562% */
	BR9000000,		/* 0.368922% */
	BR10000000		/* 0.09766% */
};

#define BRE(b)				case b:		\
								return BR##b

#define BRD(b)				case BR##b:	\
								return b

#define BRC(b)				case b:	\
								BAUD_SET_##b(sci, set);	\
								break

#define BRA(b)				case b:	\
								vcmd = BAUD_ADJUST_##b();	\
								break

#define BAUD_OP(op)							\
							op(110);			\
							op(134);			\
							op(150);			\
							op(200);			\
							op(300);			\
							op(600);			\
							op(1200);			\
							op(1800);			\
							op(2400);			\
							op(4800);			\
							op(9600);		\
							op(19200);		\
							op(38400);		\
							op(57600);		\
							op(115200);		\
							op(230400);		\
							op(460800);		\
							op(500000);		\
							op(576000);		\
							op(921600);		\
							op(1000000);	\
							op(1152000);	\
							op(1500000);	\
							op(1562500);	\
							op(2000000);	\
							op(2500000);	\
							op(3000000);	\
							op(3125000);	\
							op(3500000);	\
							op(4000000);	\
							op(5000000);	\
							op(6000000);	\
							op(6250000);	\
							op(7000000);	\
							op(8000000);	\
							op(9000000);	\
							op(10000000)


/* all the cmd/req contain the opcode filed, the lowest 4 bits */
#define VSCI_GET_OPCODE(m)	((m) & 0x0f)

union vscic_open {
	struct {
		uint32_t opcode : 4;
		uint32_t devname : 4;
		uint32_t dri_count : 4;
		uint32_t resv : 20;
	}c;

	uint32_t d;
};

/*
  * devname: VSCI_DEVxxx
  * dri_count: device data idle interval, unit = 100us
  */
static inline uint32_t vcmd_open(uint32_t devname)
{
	union vscic_open o;
	
	o.c.opcode = VSCIC_OPEN;
	o.c.devname = devname;
	o.c.dri_count = 5; /* 500us by default, may changed by low baudrate */
	o.c.resv = 0;

	return o.d;
}

/*
 * !!! NOTE !!!
 * 1) This command is for advanced customer only.
 * 2) Reason: Some UART device's baudrate is not very accurate and difficult to change.
 * 3) This command sending function vsci_baud_adjust() in sh-vsci.c is commented out by default.
 *  Because it is not needed in most cases.
 *  Re-open the VSCI device and do not send this command, the VSCI device will use the preset(default) setting.
 * 4) The macro(es) below are the preset(default) baudrate settings.
 *  Customer can adjust the preset setting to fit for their UART device baudrate.
*/
#define BAUD_ADJUST_110()				vcmd_baud_adjust(BR110, 226, 131, 1, 0, 0, 3)
#define BAUD_ADJUST_134()				vcmd_baud_adjust(BR134, 184, 130, 1, 0, 0, 3)
#define BAUD_ADJUST_150()				vcmd_baud_adjust(BR150, 163, 129, 1, 0, 0, 3)
#define BAUD_ADJUST_200()				vcmd_baud_adjust(BR200, 122, 129, 1, 0, 0, 3)
#define BAUD_ADJUST_300()				vcmd_baud_adjust(BR300, 81, 129, 1, 0, 0, 3)
#define BAUD_ADJUST_600()				vcmd_baud_adjust(BR600, 163, 129, 1, 0, 0, 2)
#define BAUD_ADJUST_1200()				vcmd_baud_adjust(BR1200, 81, 129, 1, 0, 0, 2)
#define BAUD_ADJUST_1800()				vcmd_baud_adjust(BR1800, 216, 128, 1, 0, 0, 1)
#define BAUD_ADJUST_2400()				vcmd_baud_adjust(BR2400, 163, 129, 1, 0, 0, 1)
#define BAUD_ADJUST_4800()				vcmd_baud_adjust(BR4800, 81, 129, 1, 0, 0, 1)
#define BAUD_ADJUST_9600()				vcmd_baud_adjust(BR9600, 163, 129, 1, 0, 0, 0)
#define BAUD_ADJUST_19200()			vcmd_baud_adjust(BR19200, 81, 129, 1, 0, 0, 0)
#define BAUD_ADJUST_38400()			vcmd_baud_adjust(BR38400, 40, 129, 1, 0, 0, 0)
#define BAUD_ADJUST_57600()			vcmd_baud_adjust(BR57600, 31, 151, 1, 0, 0, 0)
#define BAUD_ADJUST_115200()			vcmd_baud_adjust(BR115200, 15, 151, 1, 0, 0, 0)
#define BAUD_ADJUST_230400()			vcmd_baud_adjust(BR230400, 7, 151, 1, 0, 0, 0)
#define BAUD_ADJUST_460800()			vcmd_baud_adjust(BR460800, 3, 151, 1, 0, 0, 0)
#define BAUD_ADJUST_500000()			vcmd_baud_adjust(BR500000, 3, 164, 1, 0, 0, 0)
#define BAUD_ADJUST_576000()			vcmd_baud_adjust(BR576000, 3, 189, 1, 0, 0, 0)
#define BAUD_ADJUST_921600()			vcmd_baud_adjust(BR921600, 1, 151, 1, 0, 0, 0)
#define BAUD_ADJUST_1000000()			vcmd_baud_adjust(BR1000000, 1, 164, 1, 0, 0, 0)
#define BAUD_ADJUST_1152000()			vcmd_baud_adjust(BR1152000, 1, 189, 1, 0, 0, 0)
#define BAUD_ADJUST_1500000()			vcmd_baud_adjust(BR1500000, 1, 246, 1, 0, 0, 0)
#define BAUD_ADJUST_1562500()			vcmd_baud_adjust(BR1562500, 1, 255, 0, 0, 0, 0)
#define BAUD_ADJUST_2000000()			vcmd_baud_adjust(BR2000000, 0, 164, 1, 0, 0, 0)
#define BAUD_ADJUST_2500000()			vcmd_baud_adjust(BR2500000, 0, 205, 1, 0, 0, 0)
#define BAUD_ADJUST_3000000()			vcmd_baud_adjust(BR3000000, 0, 246, 1, 0, 0, 0)
#define BAUD_ADJUST_3125000()			vcmd_baud_adjust(BR3125000, 0, 255, 0, 0, 0, 0)
#define BAUD_ADJUST_3500000()			vcmd_baud_adjust(BR3500000, 0, 144, 1, 1, 0, 0)
#define BAUD_ADJUST_4000000()			vcmd_baud_adjust(BR4000000, 0, 164, 1, 1, 0, 0)
#define BAUD_ADJUST_5000000()			vcmd_baud_adjust(BR5000000, 0, 205, 1, 1, 0, 0)
#define BAUD_ADJUST_6000000()			vcmd_baud_adjust(BR6000000, 0, 246, 1, 1, 0, 0)
#define BAUD_ADJUST_6250000()			vcmd_baud_adjust(BR6250000, 0, 255, 0, 1, 0, 0)
#define BAUD_ADJUST_7000000()			vcmd_baud_adjust(BR7000000, 0, 144, 1, 1, 1, 0)
#define BAUD_ADJUST_8000000()			vcmd_baud_adjust(BR8000000, 0, 164, 1, 1, 1, 0)
#define BAUD_ADJUST_9000000()			vcmd_baud_adjust(BR9000000, 0, 185, 1, 1, 1, 0)
#define BAUD_ADJUST_10000000()			vcmd_baud_adjust(BR10000000, 0, 205, 1, 1, 1, 0)

union vscic_baud_adjust {
	struct {
		uint32_t opcode : 4;
		uint32_t baud : 6; /* Baudrate, vsci_br type */
		uint32_t BRR : 8; /* Register BRR */
		uint32_t MDDR : 8; /* Register MDDR */
		uint32_t BRME : 1; /* Register bit SEMR.BRME */
		uint32_t BGDM : 1; /* Register bit SEMR.BGDM */
		uint32_t ABCS0 : 1; /* Register bit SEMR.ABCS0 */
		uint32_t CKS : 2; /* Register bits SMR.CKS */ 
		uint32_t resv : 1;
	}c;

	uint32_t d;
};

static inline uint32_t vcmd_baud_adjust(enum vsci_br baud, uint32_t BRR, uint32_t MDDR, uint32_t BRME, uint32_t BGDM, uint32_t ABCS0, uint32_t CKS)
{
	union vscic_baud_adjust b;
	
	b.c.opcode = VSCIC_BAUD_ADJUST;
	b.c.baud = baud;
	b.c.BRR = BRR;
	b.c.MDDR = MDDR;
	b.c.BRME = BRME;
	b.c.BGDM = BGDM;
	b.c.ABCS0 = ABCS0;
	b.c.CKS = CKS;
	b.c.resv = 0;

	return b.d;
}

enum {
	VSCI_PARITY_OFF = 0,
	VSCI_PARITY_ODD = 1,
	VSCI_PARITY_EVEN = 2,

	VSCI_HW_FLOWCTRL_OFF = 0,
	VSCI_HW_FLOWCTRL_ON = 1
};
/*
	Currently, support only 8/9 bit(VSCI), 8 bit(VSCIF)!!!
	For VSCI 9bit, using CS7 in application code instead
*/
union vscic_conf {
	struct {
		uint32_t opcode : 4;
		uint32_t baud : 6; /* Baudrate, vsci_br type */
		uint32_t dbits : 4; /* SCIg: 8 = 8bit, other = 9bit. SCIF: 8 = 8bit */
		uint32_t parity : 2; /* VSCI_PARITY_XXX */
		uint32_t sbits : 3; /* 1 = 1bit. 2 = 2bit */
		uint32_t flow : 3; /* 1 = VSCI_HW_FLOWCTRL_XXX */
		uint32_t resv : 10;
	}c;

	uint32_t d;
};

static inline uint32_t vcmd_conf(enum vsci_br baud, uint32_t dbit, uint32_t par, uint32_t sbit, uint32_t flow)
{
	union vscic_conf c;
	
	c.c.opcode = VSCIC_CONF;
	c.c.baud = baud;
	c.c.dbits = dbit;
	c.c.parity = par;
	c.c.sbits = sbit;
	c.c.flow = flow;
	c.c.resv = 0;

	return c.d;
}

/*
	rx: 0 = ignore, 1 = start
	tx: 0 = ignore, 1 = start
*/
union vscic_start {
	struct {
		uint32_t opcode : 4;
		uint32_t rx : 1;
		uint32_t tx : 1;
		uint32_t resv : 26;
	}c;

	uint32_t d;
};

static inline uint32_t vcmd_start(uint32_t rx, uint32_t tx)
{
	union vscic_start s;
	
	s.c.opcode = VSCIC_START;
	s.c.rx = rx;
	s.c.tx = tx;
	s.c.resv = 0;

	return s.d;
}

/*
	bytes: data bytes that are ready to send
*/
union vscic_txd_rdy {
	struct {
		uint32_t opcode : 4;
		uint32_t bytes : 28;
	}c;

	uint32_t d;
};

static inline uint32_t vcmd_txd_rdy(uint32_t bytes)
{
	union vscic_txd_rdy t;
	
	t.c.opcode = VSCIC_TXD_RDY;
	t.c.bytes = bytes;

	return t.d;
}

/*
	rx: 0 = ignore, 1 = stop
	tx: 0 = ignore, 1 = stop
*/
union vscic_stop {
	struct {
		uint32_t opcode : 4;
		uint32_t rx : 1;
		uint32_t tx : 1;
		uint32_t resv : 26;
	}c;

	uint32_t d;
};

static inline uint32_t vcmd_stop(uint32_t rx, uint32_t tx)
{
	union vscic_stop s;
	
	s.c.opcode = VSCIC_STOP;
	s.c.rx = rx;
	s.c.tx = tx;
	s.c.resv = 0;

	return s.d;
}

union vscic_close {
	struct {
		uint32_t opcode : 4;
		uint32_t resv : 28;
	}c;

	uint32_t d;
};

static inline uint32_t vcmd_close(void)
{
	union vscic_close c;
	
	c.c.opcode = VSCIC_CLOSE;
	c.c.resv = 0;

	return c.d;
}

/*
	bytes: data bytes that have been received,
	notify Linux to read out data
*/
union vscir_rx_rdy {
	struct {
		uint32_t opcode : 4;
		uint32_t bytes : 28;
	}r;

	uint32_t d;
};

static inline uint32_t vreq_rx_rdy(uint32_t bytes)
{
	union vscir_rx_rdy r;
	
	r.r.opcode = VSCIR_RX_RDY;
	r.r.bytes = bytes;

	return r.d;
}

/*
	bytes: last data bytes that have been received,
	notify Linux to read out and push data(DRI)
*/
union vscir_rxd_rdy {
	struct {
		uint32_t opcode : 4;
		uint32_t bytes : 28;
	}r;

	uint32_t d;
};

static inline uint32_t vreq_rxd_rdy(uint32_t bytes)
{
	union vscir_rxd_rdy r;
	
	r.r.opcode = VSCIR_RXD_RDY;
	r.r.bytes = bytes;

	return r.d;
}

/*
	bytes: last data bytes of last packet that have been  sent out to TX pin(TEI),
	notify Linux to prepare and send out next packet
*/
union vscir_tx_end {
	struct {
		uint32_t opcode : 4;
		uint32_t resv : 28;
	}r;

	uint32_t d;
};

static inline uint32_t vreq_tx_end(void)
{
	union vscir_tx_end t;
	
	t.r.opcode = VSCIR_TX_END;
	t.r.resv = 0;

	return t.d;
}


#if defined(__linux__) || defined(__KERNEL__)
int vsci_alloc_device(struct device *devp, struct vsci_device *vd, void *sciport, int port_type, int port_num, vsci_cb rxfn, vsci_cb txfn);

size_t vsci_get_mapbase(int port_type, int port_num);

int vsci_baud_adjust(struct vsci_device *vd, int baud);

enum vsci_br vsci_baud_enc(int baud);

int vsci_send_cmd(struct vsci_device *vd, uint32_t cmd);

void vsci_free_device(struct vsci_device *vd);
#endif

#endif

