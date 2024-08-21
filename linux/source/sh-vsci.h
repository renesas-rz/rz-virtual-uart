
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
	DEV_VSCI0 = 1,
	DEV_VSCI1,
	DEV_VSCI_MAX,
	
	DEV_VSCIF0, /* SCIF0 is often used by Linux side */
	DEV_VSCIF1,
	DEV_VSCIF2,
	DEV_VSCIF3,
	DEV_VSCIF4,
	DEV_VSCIF_MAX
};

#ifdef __linux__
#define IS_VSCI_PORT(p)		(PORT_VSCI == (p))
#define IS_VSCIF_PORT(p)	(PORT_VSCIF == (p))
#endif

#define IS_VSCI_DEV(d)		(((d) >= DEV_VSCI0) && ((d) < DEV_VSCI_MAX))
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

struct vsci_circ {
	struct {
		uint64_t rbuf;
		uint64_t tbuf;
	}bcore; /* big core, 64-bit, Linux, VA */

	struct {
		uint32_t rbuf;
		uint32_t tbuf;
	}lcore; /* little core, 32-bit, RTOS, PA */
};

struct shared_mem_info {
	uint32_t msg_buf[VSCI_DEVICE_NUM_MAX * 2]; /* Linux-RTOS MSG memory */
	struct vsci_circ vc[VSCI_DEVICE_NUM_MAX]; /* VSCI device circ buffer pointers */
	uint8_t reserved[16]; /* gap */
	uint32_t circ_buffer[];
};

#ifdef __linux__
struct vsci_device {
	int devname; /* vsci device name, DEV_VSCIxxx */

	struct device *platdev;

	void *sciport;

	size_t mp; /* struct mhu_port pointer */

	struct vsci_circ *vc; /* vsci circ buf base, virtual addr */
};
#endif

enum vsci_cmd_set {

	/*
		--- Linux -> RTOS command list ---
	*/
	VSCIC_OPEN = 1,
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
	/* baudrate, error rate */
	BR9600 = 1,		/* 0.0194995% */
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

#define BAUD_OP(op)							\
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
#define GET_OPCODE(m)		((m) & 0x0f)

union vscic_open {
	struct {
		uint32_t opcode : 4;
		uint32_t devname : 4;
		uint32_t resv : 24;
	}c;

	uint32_t d;
};

static inline uint32_t vcmd_open(uint32_t devname)
{
	union vscic_open o;
	
	o.c.opcode = VSCIC_OPEN;
	o.c.devname = devname;
	o.c.resv = 0;

	return o.d;
}


/*
	Currently, support only 8/9 bit(VSCI), 8 bit(VSCIF)!!!
	For VSCI 9bit, using CS7 in application code instead
*/
union vscic_conf {
	struct {
		uint32_t opcode : 4;
		uint32_t baud : 5; /* vsci_baud_enc() */
		uint32_t dbits : 4; /* SCIg: 8 = 8bit, 9 = 9bit. SCIF: 7 = 7bit, 8 = 8bit */
		uint32_t parity : 2; /* 0 = none. 1 = ODD, 2 = EVEN */
		uint32_t sbits : 3; /* 1 = 1bit. 2 = 2bit */
		uint32_t resv : 14;
	}c;

	uint32_t d;
};

static inline uint32_t vcmd_conf(enum vsci_br br, uint32_t dbit, uint32_t par, uint32_t sbit)
{
	union vscic_conf c;
	
	c.c.opcode = VSCIC_CONF;
	c.c.baud = br;
	c.c.dbits = dbit;
	c.c.parity = par;
	c.c.sbits = sbit;
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


#ifdef __linux__
int vsci_alloc_device(struct device *devp, struct vsci_device *vd, void *sciport, int port_type, int port_num, vsci_cb rxfn, vsci_cb txfn);

size_t vsci_get_mapbase(int port_type, int port_num);

enum vsci_br vsci_baud_enc(int baud);

int vsci_send_cmd(struct vsci_device *vd, uint32_t cmd);

void vsci_free_device(struct vsci_device *vd);
#endif

#endif

