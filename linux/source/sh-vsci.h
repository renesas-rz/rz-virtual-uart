
#ifndef __SH_VSCI_H__
#define __SH_VSCI_H__

/***************************************************************************
	 ------------------- NOTE ----------------------
	IF CHANGED ANYTHING OF THIS FILE, YOU NEED TO RECOMPILE
	e2studio project & Linux kernel AFTER UPDATING
	WITH THIS FILE(BOTH).
****************************************************************************/

/*
	For RZ/G2L|LC, MHU can support 2 CH VSCI device
	For RZ/G2UL, MHU can support only 1CH VSCI device(Not tested yet)
*/
#define __MAX_DEV_NUM__		2

#define VSCI_DEV_CNT		__MAX_DEV_NUM__

enum vsci_name {
	DEV_VSCI0 = 0x01,
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
	uint32_t msg_buf[64]; /* Linux-RTOS MSG memory */
	struct vsci_circ vc[VSCI_DEV_CNT]; /* VSCI device circ buffer */
};

#ifdef __linux__
struct vsci_device {
	int enabled; /* 0: disabled, not used. 1: open, being used */

	int device; /* VSCI device index, 0, 1, 2... */

	int devname; /* vsci device name, DEV_VSCIxxx */

	struct device *platdev;

	void *sciport;

	size_t mp; /* struct mhu_port pointer */

	/* 
		using different vc base(physical) to distinguish VSCI device(for vsci probe in sh-sci.c),
		like different SCI(F) physical base address for different SCI(F) devices
	*/
	size_t vc_base; /* vsci circ buf base, physical addr */

	struct vsci_circ *vc; /* vsci circ buf base, virtual addr */
};
#endif

enum vsci_cmd_set {

	/*
		--- Linux -> RTOS command list ---
	*/
	VSCIC_OPEN = 0x01,
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
	BR9600 = 1,
	BR19200,
	BR38400,
	BR57600,
	BR115200,
	BR230400,
	BR460800,
	BR500000,
	BR576000,
	BR921600,
	BR1000000,
	BR1152000,
	BR1500000,
	BR2000000,
	BR2500000,
	BR3000000,
	BR3125000,
	BR3500000,
	BR4000000,
	BR5000000,
	BR6000000,
	BR7000000,
	BR8000000,
	BR9000000,
	BR10000000
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
							op(2000000);	\
							op(2500000);	\
							op(3000000);	\
							op(3125000);	\
							op(3500000);	\
							op(4000000);	\
							op(5000000);	\
							op(6000000);	\
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
	}c;

	uint32_t d;
};

static inline uint32_t vreq_rx_rdy(uint32_t bytes)
{
	union vscir_rx_rdy r;
	
	r.c.opcode = VSCIR_RX_RDY;
	r.c.bytes = bytes;

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
	}c;

	uint32_t d;
};

static inline uint32_t vreq_rxd_rdy(uint32_t bytes)
{
	union vscir_rxd_rdy r;
	
	r.c.opcode = VSCIR_RXD_RDY;
	r.c.bytes = bytes;

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
	}c;

	uint32_t d;
};

static inline uint32_t vreq_tx_end(void)
{
	union vscir_tx_end t;
	
	t.c.opcode = VSCIR_TX_END;
	t.c.resv = 0;

	return t.d;
}


#ifdef __linux__
/*
	For Linux 
*/
struct vsci_device *vsci_alloc_device(struct device *devp, void *sciport, int port_type, int port_num, vsci_cb rxfn, vsci_cb txfn);
void vsci_free_device(struct vsci_device *vd);
// int to enum
enum vsci_br vsci_baud_enc(int baud);
// enum to int
int vsci_baud_dec(enum vsci_br baud);

int vsci_send_cmd(struct vsci_device *vd, uint32_t cmd);
#else
/*
	For RTOS
*/
void vsci_loop(void);
#endif

#endif

