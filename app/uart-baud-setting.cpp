
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <asm/termbits.h>
#include <asm/ioctls.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <signal.h>


#if (__MAX_BAUD != B4000000)
#error "__MAX_BAUD mismatch"
#endif

/*
	B50 and B75 are not supported due to 100M pclk dividing limitation.
*/
static speed_t speed_name[] = {
	B0/*B50*/, B0/*B75*/, B110,
	B134, B150, B200,
	B300, B600, B1200,
	B1800, B2400, B4800,
	B9600, B19200, B38400,
	B57600,	B115200, B230400,
	B460800, B500000, B576000,
	B921600, B1000000, B1152000,
	B1500000, B2000000, B2500000,
	B3000000, B3500000, B4000000
};

static int speed_arr[] = {
	50, 75, 110,
	134, 150, 200,
	300, 600, 1200,
	1800, 2400, 4800,
	9600, 19200, 38400,
	57600, 115200, 230400,
	460800, 500000, 576000,
	921600, 1000000, 1152000,
	1500000, 2000000, 2500000,
	3000000, 3500000, 4000000,

	/* virtual UART extended baudrates, refer to sh-vsci.h */
	1562500, 3125000, 5000000,
	6000000, 6250000, 7000000,
	8000000, 9000000, 10000000
};

static struct termios2 old;

int uart_std_speed(int speed, speed_t &std_speed)
{
	int cnt, std_cnt;
	int i;

	std_cnt = sizeof(speed_name) / sizeof(speed_name[0]);
	cnt = sizeof(speed_arr) / sizeof(speed_arr[0]);

	for(i = 0; i < cnt; i++) {
		if(speed == speed_arr[i]) {
			if(i >= std_cnt)
				return -ERANGE;
			
			if(B0 == speed_name[i])
				return -EPERM;
			
			std_speed = speed_name[i];
			return 0;
		}
	}

	return -EPERM;
}

/*
 * speed: decimal value: 9600, 115200, 5000000 etc.
 */
int uart_set_term(int fd, int speed, char dbits, char parity, char sbits)
{
	int i, r;
	speed_t std_speed;
	struct termios2 opt;

	if(-1 == ioctl(fd, TCGETS2, &old)) {
		error("TCGETS2 fail, errno = %d\n", errno);
		goto exit0;
	}

	opt = old;

	// set baudrate
	opt.c_cflag &= ~CBAUD;
	r = uart_std_speed(speed, std_speed);
	if(-ERANGE == r) {
		opt.c_cflag |= BOTHER;
		opt.c_ispeed = speed;
		opt.c_ospeed = speed;
	} else if(0 == r) {
		opt.c_cflag |= std_speed;
	} else {/* -EPERM */
		error("requested baudrate %d is not supported\n", speed);
		goto exit0;
	}

	// set data bit
	opt.c_cflag &= ~CSIZE;
	switch (dbits) {
		case '7':
		case '9':
			opt.c_cflag |= CS7; /* RZ/G2L VSCI driver code will change to 9bit config */
			break;
		case '8':
			opt.c_cflag |= CS8;
			break;
		default:
			error("unsupported data size %d\n", dbits);
			goto exit0;
	}

	// set parity
	switch (parity) {
		case 'n':
		case 'N':
			opt.c_cflag &= ~PARENB;
			opt.c_iflag &= ~INPCK;
			break;
		case 'o':
		case 'O':
			opt.c_cflag |= (PARODD | PARENB);
			opt.c_iflag |= INPCK;
			break;
		case 'e':
		case 'E':
			opt.c_cflag |= PARENB;
			opt.c_cflag &= ~PARODD;
			opt.c_iflag |= INPCK;
			break;
		default:
			error("invalid parity set param %c found\n", parity);
			goto exit0;
	}

	switch (sbits) {
		case '1':
			opt.c_cflag &= ~CSTOPB;
			break;
		case '2':
			opt.c_cflag |= CSTOPB;
			break;
		default:
			error("unsupported stop bits %d\n", sbits);
			goto exit0;
	}

	// for non O_NONBLOCK open mode(BLOCK mode)
	opt.c_cc[VTIME] = 10; // 1s
	opt.c_cc[VMIN] = 0; 

	// raw data communication(no special char)
	opt.c_iflag &= ~(BRKINT | ICRNL | ISTRIP | IXON);
	opt.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
	opt.c_oflag &= ~OPOST;
	
	/* 0x0A sending issue */
	//opt.c_iflag &= ~(INLCR | ICRNL | IGNCR);
	//opt.c_oflag &= ~(ONLCR | OCRNL | ONOCR | ONLRET);

	if(-1 == ioctl(fd, TCSETS2, &opt)) {
		error("termios set error[%d]\n", errno);
		goto exit0;
	}

	tcflush(fd, TCIFLUSH);
	tcflush(fd, TCOFLUSH);

	return 0;

exit0:
	return -1;
}

int uart_restore_term(int fd)
{
	tcdrain(fd);

	if(-1 == ioctl(fd, TCSETS2, &old)) {
		error("termios restore error[%d]\n", errno);
		return -1;
	}

	return 0;
}

